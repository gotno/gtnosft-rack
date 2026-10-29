#include "AudioStreamer.hpp"
#include "../bCast.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bcast {

void AudioStreamer::start() {
  if (running.exchange(true)) return;
  thread = std::thread([this]() { run(); });
}

void AudioStreamer::stop() {
  running = false;
  if (thread.joinable()) thread.join();
}

void AudioStreamer::run() {
  uint64_t seenVersion = std::numeric_limits<uint64_t>::max();
  int seenRate = -1;

  while (running.load()) {
    bCastSnapshot snap = module->snapshot();
    int rate = (int)std::lround(module->engineSampleRate.load());

    if (snap.version != seenVersion || rate != seenRate) {
      seenVersion = snap.version;
      seenRate = rate;
      reset(snap.config, rate);
    } else if (
      wantOpen && !socket.isOpen()
        && clock::now() - lastOpenAttempt >= std::chrono::seconds(1)
    ) {
      if (openSocket()) setStreaming(true);
    }

    if (module->streaming.load()) {
      pump();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }

  setStreaming(false);
  socket.close();
}

void AudioStreamer::setStreaming(bool on) {
  module->streaming = on;
  if (on != highResTimer) {
    setHighResolutionTimer(on);
    highResTimer = on;
  }
}

void AudioStreamer::reset(const bCastConfig& config, int rate) {
  // process() may land one more frame after the clear; it's harmless
  setStreaming(false);
  module->ring.clear();
  pending.clear();
  accum.clear();
  history.clear();
  seq = 0;
  epoch = ++module->epoch;
  sendFailures = 0;
  module->sendFailing = false;
  socket.close();

  streamId = module->streamId();
  preset = presetById(config.preset);
  host = config.host;
  port = config.port;
  inRate = rate;
  openWarned = false;
  wantOpen = config.enabled && rate > 0;
  if (!wantOpen) return;

  if (!encoder.reset(preset)) {
    WARN("bCast %u: can't create the encoder for preset %d", streamId, preset.id);
    module->sendFailing = true;
    wantOpen = false;
    return;
  }
  src.setChannels(2);
  src.setRates(inRate, SAMPLE_RATE);
  src.refreshState();  // drop filter history from the previous epoch

  if (openSocket()) setStreaming(true);
}

bool AudioStreamer::openSocket() {
  lastOpenAttempt = clock::now();
  if (socket.open(host, port)) {
    INFO(
      "bCast %u: streaming %s to %s:%d, epoch %u%s",
      streamId, preset.name, host.c_str(), port, (unsigned)epoch,
      socket.priorityMarked() ? "" : " (no priority marking)"
    );
    module->sendFailing = false;
    openWarned = false;
    return true;
  }
  if (!openWarned) {
    WARN("bCast %u: can't open %s:%d: %s", streamId, host.c_str(), port, socket.lastError().c_str());
    openWarned = true;
  }
  module->sendFailing = true;
  return false;
}

void AudioStreamer::pump() {
  rack::dsp::Frame<2> chunk[256];
  while (size_t n = std::min<size_t>(module->ring.size(), 256)) {
    module->ring.shiftBuffer(chunk, n);
    pending.insert(pending.end(), chunk, chunk + n);
  }
  if (pending.empty()) return;

  int inFrames = (int)pending.size();
  int outFrames = (int)((int64_t)inFrames * SAMPLE_RATE / inRate) + 64;
  resampled.resize(outFrames);
  src.process(pending.data(), &inFrames, resampled.data(), &outFrames);
  // speex may leave input it couldn't fit; keep it for the next pass
  pending.erase(pending.begin(), pending.begin() + inFrames);

  const float* out = (const float*)resampled.data();
  accum.insert(accum.end(), out, out + (size_t)outFrames * CHANNELS);

  const size_t frameFloats = (size_t)preset.frameSamples * CHANNELS;
  size_t offset = 0;
  while (accum.size() - offset >= frameFloats) {
    emitFrame(&accum[offset]);
    offset += frameFloats;
  }
  accum.erase(accum.begin(), accum.begin() + offset);
}

void AudioStreamer::emitFrame(const float* pcm) {
  EncodedFrame frame(maxFrameBytes(1 + preset.redundancy));
  int bytes = encoder.encode(pcm, frame.data(), (int)frame.size());
  if (bytes < 0) {
    // the seq still advances, so the client sees the gap and conceals it
    WARN("bCast %u: encode failed at seq %u", streamId, seq);
    history.clear();
    ++seq;
    return;
  }
  frame.resize(bytes);
  history.push_front(std::move(frame));
  while ((int)history.size() > 1 + preset.redundancy) history.pop_back();

  std::vector<const EncodedFrame*> frames;
  for (const EncodedFrame& f : history) frames.push_back(&f);

  PacketHeader header{
    streamId, epoch, preset.codec,
    (uint16_t)preset.frameSamples, (uint8_t)preset.id, seq,
  };
  datagram.resize(MAX_DATAGRAM);
  size_t size = writePacket(datagram.data(), datagram.size(), header, frames);
  ++seq;
  if (size == 0) {
    // can't happen while frames are capped at maxFrameBytes
    history.clear();
    return;
  }

  bool ok = socket.send(datagram.data(), size);
  sendFailures = ok ? 0 : sendFailures + 1;
  module->sendFailing = sendFailures >= FAILING_AFTER;
}

}
