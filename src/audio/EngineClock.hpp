#pragma once
#include <rack.hpp>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace bcast {

// steps the Rack engine in real time while `module` is the engine's master
// module, the way an Audio module's device callback does. idles otherwise.
class EngineClock {
public:
  explicit EngineClock(rack::engine::Module* module);
  ~EngineClock();

private:
  struct State {
    std::mutex mutex;
    std::condition_variable cv;
    bool stop = false;
    bool exited = false;
  };

  std::shared_ptr<State> state;
  std::thread thread;

  static void run(
    std::shared_ptr<State> state,
    rack::Context* context,
    rack::engine::Module* module
  );
};

} // namespace bcast
