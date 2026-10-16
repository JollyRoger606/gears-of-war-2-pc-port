// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Master volume cvar wiring.
#include "gow2_audio_volume.h"

#include <charconv>
#include <string_view>
#include <system_error>

#include <rex/audio/downmix.h>
#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_INT32(gow2_audio_volume, 100, "Audio",
                     "Master volume in percent (0-100). Takes effect at once.")
    .range(0, 100);

namespace rex::glue {
namespace {

void ApplyVolume(int percent) { rex::audio::SetOutputGain(VolumeGain(percent)); }

}  // namespace

void InstallAudioVolume() {
  static bool installed = false;
  if (installed) return;
  installed = true;
  const int current = REXCVAR_GET(gow2_audio_volume);
  ApplyVolume(current);
  if (current != 100) REXLOG_INFO("AUDIOVOL {}%", current);
  rex::cvar::RegisterChangeCallback(
      "gow2_audio_volume", [](std::string_view name, std::string_view new_value) {
        (void)name;
        int parsed = 0;
        const char* begin = new_value.data();
        const char* end = begin + new_value.size();
        const auto [ptr, ec] = std::from_chars(begin, end, parsed);
        if (ec != std::errc() || ptr != end) return;
        ApplyVolume(parsed);
        REXLOG_INFO("AUDIOVOL {}%", parsed);
      });
}

}  // namespace rex::glue
