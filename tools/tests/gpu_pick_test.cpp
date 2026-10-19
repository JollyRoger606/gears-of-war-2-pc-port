// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Host-side test of the integrated-GPU decision (no game data, no SDK).
#include "gow2_gpu_pick.h"

#include <cstdint>
#include <cstdio>
#include <string_view>

int g_checks = 0;
int g_failures = 0;
void Expect(bool ok, std::string_view what) { ++g_checks; if (ok) return; ++g_failures; std::printf("FAIL: %.*s\n", int(what.size()), what.data()); }

int main() {
  constexpr uint64_t kMiB = 1024ULL * 1024;
  Expect(rex::glue::LooksIntegrated(true, true, 16ULL * 1024 * kMiB), "known UMA wins over a big shared-memory reservation");
  Expect(!rex::glue::LooksIntegrated(true, false, 0), "known non-UMA is dedicated even with no dedicated memory");
  Expect(rex::glue::LooksIntegrated(false, false, 511 * kMiB), "unknown UMA under 512 MiB is integrated");
  Expect(rex::glue::LooksIntegrated(false, true, 511 * kMiB), "UMA flag is ignored when unknown");
  Expect(!rex::glue::LooksIntegrated(false, false, 512 * kMiB), "unknown UMA at exactly 512 MiB is dedicated");
  Expect(!rex::glue::LooksIntegrated(false, false, 8ULL * 1024 * kMiB), "unknown UMA with gigabytes is dedicated");
  Expect(!rex::glue::GpuChoice{}.integrated, "a fresh GpuChoice is not integrated");

  std::printf("gpu_pick_test: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
