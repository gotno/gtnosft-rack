#include <engine/Engine.hpp>
#undef PRIVATE
#include <rack.hpp>

#include "test.hpp"
#include "osc/ChunkedSend/ChunkedSend.hpp"
#include "osc/Bundler/ChunkedSendBundler.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

struct TestSend : ChunkedSend {
  explicit TestSend(int64_t size) : ChunkedSend(new uint8_t[size](), size) {}

  ChunkedSendBundler* getBundlerForChunk(int32_t chunkNum) override {
    return new ChunkedSendBundler(
      "/set/texture/chunk", id, sequenceId, chunkNum, numChunks, chunkSize,
      size, data, 0
    );
  }
};

std::unique_ptr<TestSend> threeChunkSend() {
  TestSend probe(1);
  probe.init();
  auto send = std::make_unique<TestSend>(2 * (int64_t)probe.chunkSize + 1);
  send->init();
  return send;
}

std::vector<int32_t> takeFixed(ChunkedSend& send) {
  std::vector<int32_t> due;
  send.takeChunksDue(due, false, 0ns, 0ns);
  return due;
}

void sendAll(ChunkedSend& send, const std::vector<int32_t>& chunks) {
  for (int32_t n : chunks) send.registerChunkSent(n);
}

} // namespace

TEST(chunked_send_init_splits_into_chunks) {
  auto send = threeChunkSend();
  CHECK_EQ(send->numChunks, 3);
  CHECK(send->chunkSize > 0);
  CHECK(!send->sendSucceeded());
  CHECK(!send->sendFailed());
}

TEST(chunked_send_takes_pending_chunks_once) {
  auto send = threeChunkSend();
  CHECK(takeFixed(*send) == std::vector<int32_t>({0, 1, 2}));
  CHECK(takeFixed(*send).empty());
}

TEST(chunked_send_rejects_invalid_and_duplicate_acks) {
  auto send = threeChunkSend();
  CHECK(!send->ack(-1));
  CHECK(!send->ack(3));
  auto chunks = takeFixed(*send);
  sendAll(*send, chunks);
  CHECK(send->ack(1));
  CHECK(!send->ack(1));
  CHECK(send->acked(1));
  CHECK(!send->acked(0));
  CHECK(!send->acked(99));
}

TEST(chunked_send_succeeds_when_every_chunk_is_acked) {
  auto send = threeChunkSend();
  auto chunks = takeFixed(*send);
  sendAll(*send, chunks);
  CHECK(send->ack(0));
  CHECK(send->ack(1));
  CHECK(send->ack(2));
  CHECK(send->sendSucceeded());
}

TEST(chunked_send_dropped_chunk_returns_to_pending) {
  auto send = threeChunkSend();
  takeFixed(*send);
  send->registerChunkDropped(1);
  CHECK(takeFixed(*send) == std::vector<int32_t>({1}));
}

TEST(chunked_send_reports_rtt_only_on_the_completing_ack) {
  auto send = threeChunkSend();
  auto chunks = takeFixed(*send);
  sendAll(*send, chunks);
  std::this_thread::sleep_for(2ms);
  std::chrono::nanoseconds rtt{0};
  CHECK(send->ack(0, &rtt));
  CHECK_EQ(rtt.count(), int64_t{0});
  CHECK(send->ack(1, &rtt));
  CHECK_EQ(rtt.count(), int64_t{0});
  CHECK(send->ack(2, &rtt));
  CHECK(rtt >= 2ms);
}

TEST(chunked_send_excludes_resent_chunks_from_rtt) {
  auto send = threeChunkSend();
  auto chunks = takeFixed(*send);
  sendAll(*send, chunks);
  std::this_thread::sleep_for(3ms);
  std::vector<int32_t> due;
  CHECK_EQ(send->takeChunksDue(due, true, 1ms, 1ms), 3);
  CHECK(due == std::vector<int32_t>({0, 1, 2}));
  sendAll(*send, due);
  std::chrono::nanoseconds rtt{0};
  CHECK(send->ack(0, &rtt));
  CHECK(send->ack(1, &rtt));
  CHECK(send->ack(2, &rtt));
  CHECK_EQ(rtt.count(), int64_t{0});
}

TEST(chunked_send_fixed_mode_waits_for_retry_timeout) {
  auto send = threeChunkSend();
  auto chunks = takeFixed(*send);
  sendAll(*send, chunks);
  CHECK(takeFixed(*send).empty());
}

TEST(chunked_send_ack_while_queued_for_resend_stays_acked) {
  auto send = threeChunkSend();
  takeFixed(*send);
  CHECK(send->ack(0));
  send->registerChunkSent(0);
  CHECK(send->acked(0));
  auto due = takeFixed(*send);
  CHECK(std::find(due.begin(), due.end(), 0) == due.end());
}

TEST(chunked_send_adaptive_gives_up_after_limit) {
  auto send = threeChunkSend();
  auto chunks = takeFixed(*send);
  sendAll(*send, chunks);
  std::this_thread::sleep_for(ChunkedSend::GIVE_UP_AFTER + 20ms);
  std::vector<int32_t> due;
  send->takeChunksDue(due, true, 1ms, 1ms);
  CHECK(send->sendFailed());
}
