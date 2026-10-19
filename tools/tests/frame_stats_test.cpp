// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
#include "gow2_frame_stats_core.h"

#include <cmath>
#include <cstdio>
#include <string_view>

namespace {

int g_checks = 0;
int g_failures = 0;

void Expect(bool ok, std::string_view what) {
  ++g_checks;
  if (ok) return;
  ++g_failures;
  std::printf("FAIL: %.*s\n", int(what.size()), what.data());
}

bool Near(double actual, double expected) {
  return std::abs(actual - expected) < 0.000001;
}

void TestFirstFrameAndFiveSecondBoundary() {
  rex::glue::FrameStatsWindow window;
  Expect(!window.RecordFrame(0), "first frame starts the window even at timestamp zero");
  Expect(!window.RecordFrame(4'999'999), "window does not emit before five seconds");
  const auto stats = window.RecordFrame(5'000'000);
  Expect(stats.has_value(), "window emits at exactly five seconds");
  if (!stats) return;
  Expect(stats->frames == 2, "initial timestamp is excluded from complete intervals");
  Expect(Near(stats->average_fps, 0.4), "fps uses the full elapsed window");
}

void TestSteadyFrames() {
  rex::glue::FrameStatsWindow window;
  window.RecordFrame(100);
  for (int i = 1; i < 100; ++i) {
    Expect(!window.RecordFrame(100 + i * 50'000), "steady window emits only once");
  }
  const auto stats = window.RecordFrame(5'000'100);
  Expect(stats.has_value(), "steady window emits after 100 intervals");
  if (!stats) return;
  Expect(stats->frames == 100 && Near(stats->average_fps, 20), "100 frames in five seconds is 20 fps");
  Expect(Near(stats->p50_ms, 50) && Near(stats->p95_ms, 50) && Near(stats->p99_ms, 50),
         "steady frame percentiles agree");
  Expect(stats->over_33_ms == 100 && stats->over_50_ms == 0 && stats->over_100_ms == 0,
         "steady 50 ms frames exceed only the 33 ms threshold");
}

void TestNearestRankPercentiles() {
  rex::glue::FrameStatsWindow window;
  int64_t now = 1;
  window.RecordFrame(now);
  for (int sample = 1; sample < 100; ++sample) {
    const int duration_ms = sample == 1 ? 2 : sample == 2 ? 1 : sample;
    now += duration_ms * 1000;
    Expect(!window.RecordFrame(now), "unordered sample stays below five seconds");
  }
  const auto stats = window.RecordFrame(now + 100'000);
  Expect(stats.has_value(), "100 varied frames complete the window");
  if (!stats) return;
  Expect(stats->frames == 100 && Near(stats->average_fps, 100.0 / 5.05),
         "fps includes the interval crossing the five-second deadline");
  Expect(Near(stats->p50_ms, 50) && Near(stats->p95_ms, 95) && Near(stats->p99_ms, 99),
         "nearest-rank percentiles sort the samples");
  Expect(stats->over_33_ms == 67 && stats->over_50_ms == 50 && stats->over_100_ms == 0,
         "threshold counts include every sample");
}

void TestStrictThresholdsAndFractionalMilliseconds() {
  rex::glue::FrameStatsWindow window;
  int64_t now = 0;
  window.RecordFrame(now);
  constexpr int64_t durations[] = {33'000, 33'001, 50'000, 50'001, 100'000, 100'001};
  for (const int64_t duration : durations) {
    now += duration;
    Expect(!window.RecordFrame(now), "threshold samples do not finish the window");
  }
  const auto stats = window.RecordFrame(5'000'000);
  Expect(stats.has_value(), "threshold window completes");
  if (!stats) return;
  Expect(stats->over_33_ms == 6 && stats->over_50_ms == 4 && stats->over_100_ms == 2,
         "over thresholds are strict with microsecond precision");
  Expect(Near(stats->p50_ms, 50.001), "percentiles preserve fractional milliseconds");
  Expect(Near(stats->p95_ms, 4633.997) && Near(stats->p99_ms, 4633.997),
         "small sample high percentiles select the longest frame");
}

void TestWindowResetAndSingleSlowFrame() {
  rex::glue::FrameStatsWindow window;
  window.RecordFrame(0);
  const auto first = window.RecordFrame(6'000'000);
  Expect(first.has_value(), "single slow frame completes a window");
  if (!first) return;
  Expect(first->frames == 1 && Near(first->average_fps, 1.0 / 6),
         "single slow frame uses actual elapsed seconds");
  Expect(Near(first->p50_ms, 6000) && Near(first->p95_ms, 6000) && Near(first->p99_ms, 6000),
         "single sample is every percentile");
  Expect(!window.RecordFrame(8'000'000), "next window starts at previous boundary");
  const auto next = window.RecordFrame(11'000'000);
  Expect(next.has_value(), "next five-second window emits");
  if (!next) return;
  Expect(next->frames == 2 && Near(next->average_fps, 0.4), "window reset clears previous frames");
  Expect(Near(next->p50_ms, 2000) && Near(next->p99_ms, 3000),
         "window reset clears previous durations");
  Expect(next->over_100_ms == 2, "window reset clears previous threshold counts");
}

}  // namespace

int main() {
  TestFirstFrameAndFiveSecondBoundary();
  TestSteadyFrames();
  TestNearestRankPercentiles();
  TestStrictThresholdsAndFractionalMilliseconds();
  TestWindowResetAndSingleSlowFrame();
  std::printf("frame_stats_test: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
