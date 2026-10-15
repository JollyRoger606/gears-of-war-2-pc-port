// Tests for the focus-free test input core (src/gow2_test_input.cpp): line parsing and the
// big-endian XINPUT_STATE merge. Build target gow2_test_input_test.
#include "gow2_test_input.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace {

using rex::glue::test_input::OverlayPad;
using rex::glue::test_input::PadState;
using rex::glue::test_input::ParsePadLine;

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* what) {
  ++g_checks;
  if (ok) return;
  ++g_failures;
  std::printf("FAIL: %s\n", what);
}

using State = std::array<uint8_t, 16>;

uint16_t Be16(const State& s, size_t at) { return uint16_t((s[at] << 8) | s[at + 1]); }

uint32_t Be32(const State& s, size_t at) {
  return (uint32_t(s[at]) << 24) | (uint32_t(s[at + 1]) << 16) | (uint32_t(s[at + 2]) << 8) | s[at + 3];
}

void PutBe16(State* s, size_t at, uint16_t v) {
  (*s)[at] = uint8_t(v >> 8);
  (*s)[at + 1] = uint8_t(v);
}

void TestNeutralLines() {
  Check(ParsePadLine("") == PadState{}, "empty line is neutral");
  Check(ParsePadLine("# only a comment A B") == PadState{}, "comment-only line is neutral");
  Check(ParsePadLine("  \t , ") == PadState{}, "separators only are neutral");
}

void TestButtons() {
  Check(ParsePadLine("A start").buttons == 0x1010, "A start -> 0x1010");
  Check(ParsePadLine("lb,RB\ty").buttons == 0x8300, "lb,RB<tab>y -> 0x8300");
  Check(ParsePadLine("JUMP A").buttons == 0x1000, "unknown token ignored");
  Check(ParsePadLine("A # B").buttons == 0x1000, "text after # ignored");
  Check(ParsePadLine("up down left right back ls rs x b").buttons == 0x60EF, "every other button");
  const PadState triggers = ParsePadLine("LT RT");
  Check(triggers.left_trigger == 255 && triggers.right_trigger == 255 && triggers.buttons == 0,
        "LT RT -> both triggers full, no buttons");
}

void TestAxes() {
  const PadState pad = ParsePadLine("LX=1 LY=-1 RX=0.5 RY=2");
  Check(pad.lx == 32767, "LX=1 -> 32767");
  Check(pad.ly == -32767, "LY=-1 -> -32767");
  Check(pad.rx == 16384, "RX=0.5 -> 16384");
  Check(pad.ry == 32767, "RY=2 clamps to 32767");
  Check(ParsePadLine("LX=abc").lx == 0, "malformed LX stays 0");
  Check(ParsePadLine("ly=0.25").ly == 8192, "lower-case axis name");
  Check(ParsePadLine("LX=").lx == 0, "empty value stays 0");
}

void TestOverlayMerges() {
  State s{};
  PutBe16(&s, 0, 0);
  s[3] = 7;
  PutBe16(&s, 4, 0x0001);
  s[6] = 10;
  PutBe16(&s, 8, 1000);
  PadState pad;
  pad.buttons = 0x1000;
  pad.right_trigger = 255;
  pad.ly = 500;
  uint32_t result = 0;
  OverlayPad(s.data(), pad, 2, &result);
  Check(Be32(s, 0) == 9, "packet number bumped by 2");
  Check(Be16(s, 4) == 0x1001, "buttons ORed");
  Check(s[6] == 10 && s[7] == 255, "triggers take the larger value");
  Check(Be16(s, 8) == 1000, "a zero pad axis keeps the real stick");
  Check(Be16(s, 10) == 500, "a pad axis replaces the real stick");
  Check(result == 0, "a successful result stays 0");
}

void TestOverlayOnFailedCall() {
  State s;
  s.fill(0xAA);
  PadState pad;
  pad.buttons = 0x0010;
  pad.rx = -2;
  uint32_t result = 0x48F;
  OverlayPad(s.data(), pad, 3, &result);
  Check(result == 0, "a failed call becomes success");
  Check(Be32(s, 0) == 3, "packet number is the bump alone");
  Check(Be16(s, 4) == 0x0010, "only the pad's buttons");
  Check(s[6] == 0 && s[7] == 0, "triggers cleared");
  Check(Be16(s, 8) == 0 && Be16(s, 10) == 0 && Be16(s, 14) == 0, "other axes cleared");
  Check(Be16(s, 12) == 0xFFFE, "RX is -2 in big-endian two's complement");
}

}  // namespace

int main() {
  TestNeutralLines();
  TestButtons();
  TestAxes();
  TestOverlayMerges();
  TestOverlayOnFailedCall();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
