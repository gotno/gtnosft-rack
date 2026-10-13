#include "ChunkedManager.hpp"

#include "OscSender.hpp"
#include "ChunkedSend/ChunkedSend.hpp"
#include "Bundler/ChunkedSendBundler.hpp"

#include <limits>
#include <optional>
#include <vector>

ChunkedManager::ChunkedManager(OscSender* sender) : osctx(sender) {
  prepWorker = std::thread(&ChunkedManager::runPrepWorker, this);
}

ChunkedManager::~ChunkedManager() {
  stopWorker();
}

void ChunkedManager::add(ChunkedSend* chunked) {
  std::shared_ptr<ChunkedSend> chunkedSend(chunked);

  if (!prepOnWorker) {
    prepare(chunkedSend);
    return;
  }

  {
    std::lock_guard<std::mutex> locker(prepMutex);
    if (prepStopping) return;
    prepQueue.push_back(std::move(chunkedSend));
    BENCH(bench::gauge("prep_queue.depth", prepQueue.size());)
  }
  prepCondition.notify_one();
}

void ChunkedManager::stopWorker() {
  std::deque<std::shared_ptr<ChunkedSend>> dropped;
  {
    std::lock_guard<std::mutex> locker(prepMutex);
    prepStopping = true;
    std::swap(dropped, prepQueue);
  }
  prepCondition.notify_one();
  if (prepWorker.joinable()) prepWorker.join();
}

void ChunkedManager::runPrepWorker() {
  while (true) {
    std::shared_ptr<ChunkedSend> chunkedSend;
    {
      std::unique_lock<std::mutex> locker(prepMutex);
      prepCondition.wait(locker, [this]() {
        return prepStopping || !prepQueue.empty();
      });
      if (prepStopping) return;
      chunkedSend = std::move(prepQueue.front());
      prepQueue.pop_front();
    }
    auto start = std::chrono::steady_clock::now();
    prepare(chunkedSend);
    int64_t elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - start
    ).count();
    int64_t smoothed = prepNs.load(std::memory_order_relaxed);
    smoothed = smoothed == 0 ? elapsed : smoothed + (elapsed - smoothed) / 8;
    prepNs.store(smoothed, std::memory_order_relaxed);
  }
}

std::chrono::nanoseconds ChunkedManager::backlog() {
  size_t prepQueued;
  {
    std::lock_guard<std::mutex> locker(prepMutex);
    prepQueued = prepQueue.size();
  }
  return std::chrono::nanoseconds(
    (int64_t)prepQueued * prepNs.load(std::memory_order_relaxed)
  ) + osctx->backlog();
}

bool ChunkedManager::backedUp() {
  if (!backPressure) return false;
  return backlog() > tickInterval();
}

void ChunkedManager::prepare(const std::shared_ptr<ChunkedSend>& chunkedSend) {
  BENCH(if (chunkedSend->trace) chunkedSend->trace->stamp(bench::Stage::PrepStart);)

  ChunkedKey key(chunkedSend->id, chunkedSend->sequenceId);
  {
    std::lock_guard<std::mutex> locker(chunkedSendsMutex);
    if (chunkedSends.count(key) != 0) return;
  }

  // init outside the lock so ack() and tick() aren't held up
  chunkedSend->init();

  std::lock_guard<std::mutex> locker(chunkedSendsMutex);
  if (chunkedSends.count(key) != 0) return;
  if (supersede) supersedeOlder(*chunkedSend);
  chunkedSends.emplace(key, chunkedSend);

  bool validSend = processChunked(chunkedSend, rtt.rto(tickInterval()));
  if (!validSend) chunkedSends.erase(key);
}

void ChunkedManager::supersedeOlder(const ChunkedSend& newer) {
  std::optional<ChunkedSend::SupersedeKey> newerKey = newer.supersedeKey();
  if (!newerKey) return;

  // sends are keyed by (id, sequenceId), so one id's sends are contiguous.
  // erasing drops resends; the bundlers' shared_ptrs keep queued chunks
  // valid, and noopCheck skips them
  auto it = chunkedSends.lower_bound(
    ChunkedKey(newer.id, std::numeric_limits<int32_t>::min())
  );
  while (it != chunkedSends.end() && it->first.first == newer.id) {
    if (it->second->supersedeKey() == newerKey) {
      it->second->superseded = true;
      BENCH(bench::count("chunked.superseded");)
      it = chunkedSends.erase(it);
    } else {
      ++it;
    }
  }
}

void ChunkedManager::ack(int64_t id, int32_t sequenceId, int32_t chunkNum) {
  std::shared_ptr<ChunkedSend> chunkedSend;
  {
    std::lock_guard<std::mutex> locker(chunkedSendsMutex);
    auto it = chunkedSends.find(ChunkedKey(id, sequenceId));
    if (it == chunkedSends.end()) {
      // already finished: a late ack for a resent chunk, or a stray
      BENCH(
        static bench::Tally& unknown = bench::tally("rx.acks.unknown");
        unknown.add();
      )
      return;
    }
    chunkedSend = it->second;
  }
  std::chrono::nanoseconds sample{0};
  bool accepted = chunkedSend->ack(chunkNum, &sample);
  if (sample.count() > 0) rtt.addSample(sample);
  BENCH(
    static bench::Tally& duplicate = bench::tally("rx.acks.duplicate");
    if (!accepted) duplicate.add();
  )
  (void)accepted;
}

void ChunkedManager::tick() {
  // resends are only checked here, so the frame interval bounds how
  // precisely a timeout can fire. smoothed, and capped so a stall (e.g. a
  // minimized window) doesn't inflate it
  auto now = std::chrono::steady_clock::now();
  if (lastTickAt.time_since_epoch().count() > 0) {
    int64_t interval = std::min<std::chrono::nanoseconds>(
      now - lastTickAt, MAX_TICK_INTERVAL
    ).count();
    int64_t smoothed = tickIntervalNs.load(std::memory_order_relaxed);
    smoothed += (interval - smoothed) / 8;
    tickIntervalNs.store(smoothed, std::memory_order_relaxed);
  }
  lastTickAt = now;

  std::chrono::nanoseconds rto = rtt.rto(tickInterval());
  BENCH(
    bench::sample("pipeline.backlog", backlog().count() / 1e6f);
    if (rtt.srtt().count() > 0) {
      bench::sample("retry.srtt", rtt.srtt().count() / 1e6f);
      bench::sample("retry.rto", rto.count() / 1e6f);
    }
  )

  int32_t firstTimeouts = 0;
  {
    std::lock_guard<std::mutex> locker(chunkedSendsMutex);

    for (auto it = chunkedSends.begin(); it != chunkedSends.end();) {
      if (processChunked(it->second, rto, &firstTimeouts)) {
        ++it;
      } else {
        it = chunkedSends.erase(it);
      }
    }
  }

  // one backoff per pass, like a single connection timer expiring
  if (firstTimeouts > 0) {
    rtt.backoff();
    BENCH(
      static bench::Tally& backoffs = bench::tally("retry.backoffs");
      backoffs.add();
    )
  }
}

bool ChunkedManager::processChunked(
  const std::shared_ptr<ChunkedSend>& chunkedSend,
  std::chrono::nanoseconds rto,
  int32_t* firstTimeouts
) {
  if (
    chunkedSend->sendFailed()
    || chunkedSend->superseded
    || chunkedSend->sendSucceeded()
  ) return false;

  std::vector<int32_t> dueChunkNums;
  int32_t timedOut = chunkedSend->takeChunksDue(
    dueChunkNums,
    adaptiveRetry,
    rto,
    RttEstimator::MAX_RTO
  );
  if (firstTimeouts) *firstTimeouts += timedOut;

  if (chunkedSend->sendFailed()) {
    WARN(
      "chunked send %lld-%d failed: a chunk went unacked through all retries",
      (long long)chunkedSend->id,
      chunkedSend->sequenceId
    );
    return false;
  }

  for (int32_t chunkNum : dueChunkNums) enqueueChunk(chunkedSend, chunkNum);

  return true;
}

void ChunkedManager::enqueueChunk(
  const std::shared_ptr<ChunkedSend>& chunkedSend,
  int32_t chunkNum
) {
  ChunkedSendBundler* bundler = chunkedSend->getBundlerForChunk(chunkNum);

  // bundler callbacks run on the sender thread and must not touch the map
  bundler->noopCheck = [chunkedSend, chunkNum]() {
    return chunkedSend->sendFailed()
      || chunkedSend->superseded
      || chunkedSend->acked(chunkNum);
  };

  bundler->onBundleSent = [chunkedSend, chunkNum]() {
    chunkedSend->registerChunkSent(chunkNum);
  };

  bundler->beforeDestroy = [chunkedSend, chunkNum]() {
    chunkedSend->registerChunkDropped(chunkNum);
  };

  osctx->enqueueChunk(bundler);
}
