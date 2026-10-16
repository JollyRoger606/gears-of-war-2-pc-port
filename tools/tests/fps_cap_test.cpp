// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Host-side test of FramePacer (no SDK).
#include "gow2_fps_cap.h"

#include <cstdint>
#include <cstdio>
#include <string_view>

int g_checks = 0;
int g_failures = 0;
void Expect(bool ok, std::string_view what) { ++g_checks; if (ok) return; ++g_failures; std::printf("FAIL: %.*s\n", int(what.size()), what.data()); }

bool Near(int64_t got, int64_t want, int64_t tol) {
  const int64_t diff = got >= want ? got - want : want - got;
  return diff <= tol;
}

int main() {
  constexpr int64_t kPeriod60 = 1000000000LL / 60;
  constexpr int64_t kPeriod30 = 1000000000LL / 30;

  {
    rex::glue::FramePacer p;
    Expect(p.SleepNanos(0, 60) == 0, "first call returns 0");
  }
  {
    rex::glue::FramePacer p;
    int64_t now = 1000000000LL;
    Expect(p.SleepNanos(now, 60) == 0, "steady first returns 0");
    for (int i = 0; i < 5; ++i) {
      const int64_t sleep = p.SleepNanos(now, 60);
      Expect(Near(sleep, kPeriod60, 1000), "steady 60fps sleep near one period");
      now += sleep;
    }
  }
  {
    rex::glue::FramePacer p;
    Expect(p.SleepNanos(0, 60) == 0, "late setup returns 0");
    Expect(p.SleepNanos(kPeriod60 + 1000, 60) == 0, "late frame returns 0");
  }
  {
    rex::glue::FramePacer p;
    Expect(p.SleepNanos(0, 60) == 0, "resync setup first returns 0");
    Expect(p.SleepNanos(0, 60) == kPeriod60, "resync setup sleeps one period");
    Expect(p.SleepNanos(4 * kPeriod60, 60) == 0, "two periods late returns 0");
    const int64_t sleep = p.SleepNanos(4 * kPeriod60 + 1000, 60);
    Expect(sleep > 0, "resync next sleep is not zero");
    Expect(Near(sleep, kPeriod60 - 1000, 1000), "resync next sleep near one period");
  }
  {
    rex::glue::FramePacer p;
    Expect(p.SleepNanos(0, 60) == 0, "cap change setup returns 0");
    Expect(p.SleepNanos(0, 60) == kPeriod60, "cap change setup sleeps one period");
    Expect(p.SleepNanos(1000, 30) == 0, "cap change returns 0");
    Expect(Near(p.SleepNanos(1000, 30), kPeriod30, 1000), "cap change resets deadline");
  }
  {
    rex::glue::FramePacer p;
    Expect(p.SleepNanos(0, 60) == 0, "cap 0 setup returns 0");
    Expect(p.SleepNanos(1000, 0) == 0, "cap 0 returns 0");
    Expect(p.SleepNanos(1000, -5) == 0, "negative cap returns 0");
    Expect(p.SleepNanos(2000, 60) == 0, "cap 0 forgets the deadline");
    Expect(Near(p.SleepNanos(2000, 60), kPeriod60, 1000), "after cap 0 next sleep near one period");
  }

  std::printf("fps_cap_test: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
