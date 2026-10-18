// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Host-side test of the skip-cutscene key names (no SDK).
#include "gow2_skip_key.h"

#include <cstdio>
#include <string_view>

int g_checks = 0;
int g_failures = 0;
void Expect(bool ok, std::string_view what) { ++g_checks; if (ok) return; ++g_failures; std::printf("FAIL: %.*s\n", int(what.size()), what.data()); }

int main() {
  using namespace rex::glue;
  Expect(SkipKeyFromName("9") == '9', "digit 9 is VK 0x39");
  Expect(SkipKeyFromName("0") == '0', "digit 0");
  Expect(SkipKeyFromName("x") == 'X', "letters are upper case virtual keys");
  Expect(SkipKeyFromName("F1") == 0x70, "F1");
  Expect(SkipKeyFromName("f12") == 0x7B, "f12 lower case");
  Expect(SkipKeyFromName("F13") == 0, "F13 is not a key here");
  Expect(SkipKeyFromName("F0") == 0, "F0 is not a key");
  Expect(SkipKeyFromName("") == 0, "empty names no key");
  Expect(SkipKeyFromName(nullptr) == 0, "null names no key");
  Expect(SkipKeyFromName("99") == 0, "two digits name no key");
  Expect(SkipKeyFromName(";") == 0, "punctuation names no key");

  Expect(SkipKeyMatches('9', '9'), "the key itself matches");
  Expect(SkipKeyMatches('9', 0x69), "keypad 9 matches digit 9");
  Expect(!SkipKeyMatches('9', 0x68), "keypad 8 does not match digit 9");
  Expect(!SkipKeyMatches('9', '8'), "digit 8 does not match 9");
  Expect(!SkipKeyMatches('X', 0x60 + 'X' - '0'), "letters have no keypad twin");
  Expect(!SkipKeyMatches(0, 0), "no configured key never matches");
  Expect(SkipKeyMatches(0x74, 0x74), "F5 matches itself");

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
