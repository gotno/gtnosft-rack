#include "rack.hpp"
#pragma once

#include <future>
#include <chrono>
#include <thread>
#include <functional>

struct Interval {
  Interval(): delay(1000), callback(nullptr) {}

  Interval(
    uint32_t _delay,
    std::function<void()> _callback,
    bool _runOnce = false
  ) :
    delay(std::chrono::milliseconds(_delay)),
    callback(_callback),
    runOnce(_runOnce) {}

  Interval(Interval&& other) noexcept
    :
    delay(other.delay),
    callback(std::move(other.callback)),
    runOnce(other.runOnce) {}

  Interval& operator=(Interval&& other) noexcept {
    if (this != &other) {
      delay = other.delay;
      callback = std::move(other.callback);
      runOnce = other.runOnce;
    }
    return *this;
  }

  ~Interval() {
    clear();
  }

  void start() {
    callbackPromise = std::promise<void>();
    worker = std::thread(
      [&, this]() {
        work(callbackPromise.get_future());
      }
    );
  }

  void clear() {
    try {
      callbackPromise.set_value();
    } catch (const std::future_error& e) {
      // this enables calling clear() on intervals that are not running
    }

    if (worker.joinable()) worker.join();
  }

private:
  void work(std::future<void> killSwitch) {
    bool done = false;

    while (!done && killSwitch.wait_for(delay) == std::future_status::timeout) {
      try {
        callback();
        if (runOnce) done = true;
      } catch (const std::exception& e) {
        WARN("interval callback exception: %s", e.what());
        done = true;
      } catch (...) {
        WARN("unknown interval callback exception");
        done = true;
      }
    }
  }

  std::chrono::milliseconds delay;
  std::function<void()> callback;
  bool runOnce{false};

  std::promise<void> callbackPromise;
  std::thread worker;
};

struct Timer {
  static Interval setInterval(uint32_t delay, std::function<void()> callback) {
    return Interval(delay, callback);
  }

  static Interval setTimeout(uint32_t delay, std::function<void()> callback) {
    return Interval(delay, callback, true);
  }
};
