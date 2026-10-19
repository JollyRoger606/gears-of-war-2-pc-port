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

constexpr int64_t kPeriod60 = 16666667;
constexpr int64_t kPeriod30 = 33333334;

void TestFirstFrame() {
  rex::glue::FramePacer p;
  Expect(p.SleepNanos(0, 60) == 0, "first call returns 0");
}

void TestSteadyFrames() {
  rex::glue::FramePacer p;
  int64_t now = 1000000000LL;
  Expect(p.SleepNanos(now, 60) == 0, "steady first returns 0");
  for (int i = 0; i < 5; ++i) {
    const int64_t sleep = p.SleepNanos(now, 60);
    Expect(sleep == kPeriod60, "steady 60fps sleeps a full rounded-up period");
    now += sleep;
    p.RecordFrameStart(now);
  }
}

void TestSlightlyLateFrame() {
  rex::glue::FramePacer p;
  Expect(p.SleepNanos(0, 60) == 0, "late setup returns 0");
  Expect(p.SleepNanos(kPeriod60 + 1000, 60) == 0, "late frame returns 0");
  Expect(p.SleepNanos(kPeriod60 + 1000, 60) == kPeriod60,
         "slightly late frame starts a full new period");
}

void TestExactDeadline() {
  rex::glue::FramePacer p;
  p.SleepNanos(0, 60);
  Expect(p.SleepNanos(kPeriod60, 60) == 0, "exact deadline returns 0");
  Expect(p.SleepNanos(kPeriod60, 60) == kPeriod60,
         "exact deadline starts a full new period");
}

void TestVeryLateFrame() {
  rex::glue::FramePacer p;
  Expect(p.SleepNanos(0, 60) == 0, "resync setup first returns 0");
  Expect(p.SleepNanos(0, 60) == kPeriod60, "resync setup sleeps one period");
  Expect(p.SleepNanos(4 * kPeriod60, 60) == 0, "two periods late returns 0");
  const int64_t sleep = p.SleepNanos(4 * kPeriod60 + 1000, 60);
  Expect(sleep == kPeriod60 - 1000, "resync next sleep follows the late frame");
}

void TestJitterDoesNotCauseCatchUp() {
  rex::glue::FramePacer p;
  int64_t frame_start = 0;
  p.SleepNanos(frame_start, 60);
  constexpr int64_t work_times[] = {
      kPeriod60 + 1000, 1000, 3 * kPeriod60, kPeriod60 - 1, kPeriod60, 0};
  for (const int64_t work : work_times) {
    const int64_t now = frame_start + work;
    const int64_t sleep = p.SleepNanos(now, 60);
    const int64_t expected = work < kPeriod60 ? kPeriod60 - work : 0;
    Expect(sleep == expected, "jitter sleep depends only on the previous frame start");
    const int64_t next_start = now + sleep;
    Expect(next_start - frame_start >= kPeriod60,
           "slow and jittery frames never cause a catch-up interval");
    frame_start = next_start;
    p.RecordFrameStart(frame_start);
  }
}

void TestOversleepStartsANewPeriod() {
  rex::glue::FramePacer p;
  p.SleepNanos(0, 60);
  Expect(p.SleepNanos(1000, 60) == kPeriod60 - 1000,
         "oversleep setup sleeps until deadline");
  const int64_t wake = kPeriod60 + 2000000;
  p.RecordFrameStart(wake);
  Expect(p.SleepNanos(wake + 1000, 60) == kPeriod60 - 1000,
         "OS oversleep cannot shorten the following frame interval");
}

void TestOneFpsAndRounding() {
  rex::glue::FramePacer p;
  p.SleepNanos(0, 1);
  Expect(p.SleepNanos(0, 1) == 1000000000LL, "one fps sleeps a full second");
  p.SleepNanos(0, 144);
  Expect(p.SleepNanos(0, 144) == 6944445, "144 fps period rounds upward");
}

void TestCapChange() {
  rex::glue::FramePacer p;
  Expect(p.SleepNanos(0, 60) == 0, "cap change setup returns 0");
  Expect(p.SleepNanos(0, 60) == kPeriod60, "cap change setup sleeps one period");
  Expect(p.SleepNanos(1000, 30) == 0, "cap change returns 0");
  p.RecordFrameStart(2000);
  Expect(p.SleepNanos(2000, 30) == kPeriod30, "cap change resets deadline and wake period");
}

void TestDisabledCap() {
  rex::glue::FramePacer p;
  Expect(p.SleepNanos(0, 60) == 0, "cap 0 setup returns 0");
  Expect(p.SleepNanos(1000, 0) == 0, "cap 0 returns 0");
  Expect(p.SleepNanos(1000, -5) == 0, "negative cap returns 0");
  p.RecordFrameStart(1500);
  Expect(p.SleepNanos(2000, 60) == 0, "cap 0 forgets the deadline");
  Expect(p.SleepNanos(2000, 60) == kPeriod60, "after cap 0 next sleep is a full period");
}

int main() {
  TestFirstFrame();
  TestSteadyFrames();
  TestSlightlyLateFrame();
  TestExactDeadline();
  TestVeryLateFrame();
  TestJitterDoesNotCauseCatchUp();
  TestOversleepStartsANewPeriod();
  TestOneFpsAndRounding();
  TestCapChange();
  TestDisabledCap();

  std::printf("fps_cap_test: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
