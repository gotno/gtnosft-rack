#pragma once
#include <rack.hpp>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace bcast {

// Steps the Rack engine in real time while `module` is the engine's master
// module, the way an Audio module's device callback does. Idles otherwise.
//
// The engine calls onRemove() (and sometimes deletes modules) while holding
// its write lock, and the thread may be waiting on that lock inside
// stepBlock(), so the thread is never joined from onRemove(). It never
// dereferences the module, only compares it against getMasterModule(); the
// destructor waits for it to exit and detaches it if the engine is locked.
// With the GUI, modules are deleted outside the lock (ModuleWidget removes,
// then deletes; the scene goes before the engine), so it always joins. The
// detach only happens when the engine deletes modules itself (headless), and
// a detached thread still in stepBlock() can outlive an engine being destroyed;
// Core Audio's device threads have the same exposure there.
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

  static void run(std::shared_ptr<State> state, rack::Context* context, rack::engine::Module* module);
};

}
