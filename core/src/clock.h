// Clocks are injected so tests can move time without sleeping.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

namespace tarkov_companion {

using SystemTime = std::chrono::system_clock::time_point;
using SteadyTime = std::chrono::steady_clock::time_point;

// For in-memory expiry and latencies: it doesn't jump when the user changes
// the device's time.
using SteadyClock = std::function<SteadyTime()>;

inline SteadyClock realSteadyClock() {
  return [] { return std::chrono::steady_clock::now(); };
}

// For what is persisted and must survive a process restart: cache entries,
// token expiry, favourites' change times.
using WallClock = std::function<SystemTime()>;

inline WallClock realWallClock() {
  return [] { return std::chrono::system_clock::now(); };
}

inline int64_t millisecondsSinceEpoch(SystemTime time) {
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch());

  return static_cast<int64_t>(elapsed.count());
}

}  // namespace tarkov_companion
