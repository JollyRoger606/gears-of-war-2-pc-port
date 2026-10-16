// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Frame-rate limiter pacing state (pure, no SDK).
#pragma once

#include <cstdint>

namespace rex::glue {

class FramePacer {
 public:
  int64_t SleepNanos(int64_t now_ns, int cap) {
    if (cap <= 0) {
      has_deadline_ = false;
      last_cap_ = 0;
      return 0;
    }
    const int64_t period = 1000000000LL / cap;
    if (!has_deadline_ || cap != last_cap_) {
      last_cap_ = cap;
      deadline_ = now_ns + period;
      has_deadline_ = true;
      return 0;
    }
    if (now_ns < deadline_) {
      const int64_t sleep = deadline_ - now_ns;
      deadline_ += period;
      return sleep;
    }
    if (now_ns > deadline_ + period) {
      // Too far behind to catch up without a burst, so restart the clock.
      deadline_ = now_ns + period;
      return 0;
    }
    int64_t next = deadline_ + period;
    if (next < now_ns) next = now_ns;
    deadline_ = next;
    return 0;
  }

 private:
  int64_t deadline_ = 0;
  int last_cap_ = 0;
  bool has_deadline_ = false;
};

}  // namespace rex::glue
