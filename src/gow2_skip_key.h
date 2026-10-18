// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// The key of the skip-cutscene feature (gow2_skip_cutscene_key): name to Windows virtual key and
// the match against a pressed key. Header only, so a host test can include it.
#pragma once

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace rex::glue {

// The virtual key a value names ("9", "F5", "x"), or 0 when it names none.
inline unsigned SkipKeyFromName(const char* name) {
  if (!name || !*name) return 0;
  const size_t n = std::strlen(name);
  if (n == 1) {
    const unsigned char c = static_cast<unsigned char>(name[0]);
    if (std::isdigit(c)) return c;                          // VK_0..VK_9 are the ASCII digits
    if (std::isalpha(c)) return unsigned(std::toupper(c));  // VK_A..VK_Z
    return 0;
  }
  if ((name[0] == 'F' || name[0] == 'f') && n <= 3) {
    const int f = std::atoi(name + 1);
    if (f >= 1 && f <= 12) return 0x70u + unsigned(f - 1);  // VK_F1..VK_F12
  }
  return 0;
}

// A digit also matches the same number on the keypad (VK_NUMPAD0..9 = 0x60..0x69).
inline bool SkipKeyMatches(unsigned configured_vk, unsigned pressed_vk) {
  if (configured_vk == 0) return false;
  if (pressed_vk == configured_vk) return true;
  return configured_vk >= '0' && configured_vk <= '9' && pressed_vk == 0x60u + (configured_vk - '0');
}

}  // namespace rex::glue
