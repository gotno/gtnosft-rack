#include "EngineClock.hpp"
#include "BlockClock.hpp"
#include "UdpAudioSocket.hpp"

namespace bcast {

EngineClock::EngineClock(rack::engine::Module* module) : state(std::make_shared<State>()) {
  rack::Context* context = rack::contextGet();
  if (!context) return;
  thread = std::thread(run, state, context, module);
}

EngineClock::~EngineClock() {
  if (!thread.joinable()) return;
  bool exited;
  {
    std::unique_lock<std::mutex> lock(state->mutex);
    state->stop = true;
    state->cv.notify_all();
    exited = state->cv.wait_for(lock, std::chrono::seconds(1), [&]() { return state->exited; });
  }
  if (exited) {
    thread.join();
  } else {
    // stuck on the engine lock because we're being deleted under it
    WARN("bCast: engine clock thread busy at teardown, detaching");
    thread.detach();
  }
}

void EngineClock::run(std::shared_ptr<State> state, rack::Context* context, rack::engine::Module* module) {
  rack::system::setThreadName("bCast clock");
  rack::contextSet(context);

  BlockClock pacer;
  bool clocking = false;
  std::unique_lock<std::mutex> lock(state->mutex);
  while (!state->stop) {
    rack::engine::Engine* engine = context->engine;
    if (!engine || engine->getMasterModule() != module) {
      if (clocking) {
        setHighResolutionTimer(false);
        clocking = false;
      }
      pacer.reset();
      state->cv.wait_for(lock, std::chrono::milliseconds(20), [&]() { return state->stop; });
      continue;
    }
    if (!clocking) {
      setHighResolutionTimer(true);
      clocking = true;
    }

    double sampleRate = engine->getSampleRate();
    int frames = BlockClock::blockFrames(sampleRate);
    lock.unlock();
    engine->stepBlock(frames);
    BlockClock::clock::time_point deadline = pacer.next(frames, sampleRate, BlockClock::clock::now());
    lock.lock();
    state->cv.wait_until(lock, deadline, [&]() { return state->stop; });
  }
  if (clocking) setHighResolutionTimer(false);
  state->exited = true;
  state->cv.notify_all();
}

}
