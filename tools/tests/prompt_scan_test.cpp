// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Host-side test of the [ControllerFont] string and FString header finders (no SDK).
#include "gow2_prompt_scan.h"

#include <cstdint>
#include <cstdio>
#include <string_view>

using namespace rex::glue::promptscan;

int g_checks = 0;
int g_failures = 0;
void Expect(bool ok, std::string_view what) { ++g_checks; if (ok) return; ++g_failures; std::printf("FAIL: %.*s\n", int(what.size()), what.data()); }

void PutBe32(std::vector<uint8_t>& m, size_t at, uint32_t v) {
  for (int i = 0; i < 4; ++i) m[at + size_t(i)] = uint8_t(v >> (24 - 8 * i));
}

void PutText(std::vector<uint8_t>& m, size_t at, const std::u16string& s) {
  for (size_t i = 0; i < s.size(); ++i) {
    m[at + i * 2] = uint8_t(s[i] >> 8);
    m[at + i * 2 + 1] = uint8_t(s[i]);
  }
}

int main() {
  constexpr uint32_t kBase = 0x40000000;
  const std::u16string a = u"<Fonts:Warfare_HUD.Xbox360_18pt>A<Fonts:/>";
  const std::u16string b = u"<Fonts:Warfare_HUD.Xbox360_18pt>B<Fonts:/>";
  std::vector<uint8_t> mem(0x400, 0xEE);  // junk, so nothing matches by accident
  PutText(mem, 0x100, a);                 // the text, NUL ending it
  mem[0x100 + a.size() * 2] = mem[0x100 + a.size() * 2 + 1] = 0;
  PutBe32(mem, 0x40, kBase + 0x100);      // its FString: Data, Num, Max
  PutBe32(mem, 0x44, uint32_t(a.size() + 1));
  PutBe32(mem, 0x48, uint32_t(a.size() + 1));
  // The same glyph inside a longer text (no NUL right after): not a string of its own.
  PutText(mem, 0x200, a + u" Accept");
  mem[0x200 + (a.size() + 7) * 2] = mem[0x200 + (a.size() + 7) * 2 + 1] = 0;

  const auto strings = FindStrings(mem.data(), mem.size(), kBase, {a, b});
  Expect(strings.size() == 1, "one exact string found");
  Expect(!strings.empty() && strings[0].addr == kBase + 0x100 && strings[0].index == 0,
         "string address and index");
  Expect(FindStrings(mem.data(), mem.size(), kBase + 1, {a, b}).empty(), "odd base refused");

  std::unordered_map<uint32_t, uint32_t> lengths{{kBase + 0x100, uint32_t(a.size() + 1)}};
  auto headers = FindHeaders(mem.data(), mem.size(), kBase, lengths);
  Expect(headers.size() == 1 && headers[0].header == kBase + 0x40 && headers[0].chars == kBase + 0x100,
         "header found");
  Expect(headers.size() == 1 && headers[0].num == a.size() + 1 && headers[0].max == a.size() + 1,
         "header Num and Max");

  PutBe32(mem, 0x44, 5);  // Num no longer matches the text
  Expect(FindHeaders(mem.data(), mem.size(), kBase, lengths).empty(), "wrong Num refused");
  PutBe32(mem, 0x44, uint32_t(a.size() + 1));
  PutBe32(mem, 0x48, 3);  // Max below Num
  Expect(FindHeaders(mem.data(), mem.size(), kBase, lengths).empty(), "Max below Num refused");

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
