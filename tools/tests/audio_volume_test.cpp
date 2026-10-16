// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Host-side test of VolumeGain (no SDK).
#include "gow2_audio_volume.h"

#include <cstdio>
#include <string_view>

int g_checks = 0;
int g_failures = 0;
void Expect(bool ok, std::string_view what) { ++g_checks; if (ok) return; ++g_failures; std::printf("FAIL: %.*s\n", int(what.size()), what.data()); }

int main() {
  Expect(rex::glue::VolumeGain(0) == 0.0f, "0 maps to 0");
  Expect(rex::glue::VolumeGain(100) == 1.0f, "100 maps to 1");
  Expect(rex::glue::VolumeGain(50) == 0.25f, "50 maps to 0.25");
  Expect(rex::glue::VolumeGain(25) == 0.0625f, "25 maps to 0.0625");
  Expect(rex::glue::VolumeGain(-1) == 0.0f, "below 0 clamps to 0");
  Expect(rex::glue::VolumeGain(-100) == 0.0f, "far below 0 clamps to 0");
  Expect(rex::glue::VolumeGain(101) == 1.0f, "above 100 clamps to 1");
  Expect(rex::glue::VolumeGain(1000) == 1.0f, "far above 100 clamps to 1");

  std::printf("audio_volume_test: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
