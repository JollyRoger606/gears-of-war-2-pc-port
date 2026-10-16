// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Master volume percent-to-gain mapping (pure, no SDK).
#pragma once

namespace rex::glue {

inline float VolumeGain(int percent) {
  if (percent <= 0) return 0.0f;
  if (percent >= 100) return 1.0f;
  const float f = static_cast<float>(percent) / 100.0f;
  return f * f;
}

void InstallAudioVolume();

}  // namespace rex::glue
