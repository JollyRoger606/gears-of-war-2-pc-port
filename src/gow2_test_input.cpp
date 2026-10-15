// gow2 - ReXGlue Recompiled Project
//
// Portable core of the focus-free test input (gow2_test_input.h): parsing and the XINPUT_STATE merge.

#include "gow2_test_input.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

namespace rex::glue::test_input {
namespace {

struct ButtonName {
  std::string_view name;
  uint16_t bit;
};

constexpr std::array<ButtonName, 14> kButtons = {{
    {"UP", 0x0001}, {"DOWN", 0x0002}, {"LEFT", 0x0004}, {"RIGHT", 0x0008},
    {"START", 0x0010}, {"BACK", 0x0020}, {"LS", 0x0040}, {"RS", 0x0080},
    {"LB", 0x0100}, {"RB", 0x0200}, {"A", 0x1000}, {"B", 0x2000},
    {"X", 0x4000}, {"Y", 0x8000},
}};

constexpr uint8_t kFullTrigger = 255;
constexpr double kStickMax = 32767.0;

std::string ToUpper(std::string_view text) {
  std::string out(text);
  for (char& c : out) c = char(std::toupper(static_cast<unsigned char>(c)));
  return out;
}

bool IsSeparator(char c) { return c == ' ' || c == '\t' || c == ',' || c == '\r' || c == '\n'; }

std::string_view WithoutComment(std::string_view line) {
  const size_t hash = line.find('#');
  return hash == std::string_view::npos ? line : line.substr(0, hash);
}

// strtod on a copy: the token is not NUL-terminated, and a malformed number must give 0.
bool ParseAxisValue(std::string_view text, int16_t* out) {
  const std::string copy(text);
  if (copy.empty()) return false;
  char* end = nullptr;
  const double value = std::strtod(copy.c_str(), &end);
  if (end != copy.c_str() + copy.size() || !std::isfinite(value)) return false;
  *out = int16_t(std::lround(std::clamp(value, -1.0, 1.0) * kStickMax));
  return true;
}

int16_t* AxisSlot(PadState* pad, std::string_view name) {
  if (name == "LX") return &pad->lx;
  if (name == "LY") return &pad->ly;
  if (name == "RX") return &pad->rx;
  if (name == "RY") return &pad->ry;
  return nullptr;
}

void ApplyAxisToken(PadState* pad, std::string_view token) {
  const size_t eq = token.find('=');
  int16_t* slot = AxisSlot(pad, token.substr(0, eq));
  if (!slot) return;
  int16_t value = 0;
  *slot = ParseAxisValue(token.substr(eq + 1), &value) ? value : 0;
}

void ApplyButtonToken(PadState* pad, std::string_view token) {
  if (token == "LT") pad->left_trigger = kFullTrigger;
  if (token == "RT") pad->right_trigger = kFullTrigger;
  for (const ButtonName& b : kButtons) {
    if (b.name == token) pad->buttons |= b.bit;
  }
}

void ApplyToken(PadState* pad, std::string_view token) {
  const std::string upper = ToUpper(token);
  if (upper.find('=') != std::string::npos) {
    ApplyAxisToken(pad, upper);
  } else {
    ApplyButtonToken(pad, upper);
  }
}

uint16_t LoadBe16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }

uint32_t LoadBe32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

void StoreBe16(uint8_t* p, uint16_t v) {
  p[0] = uint8_t(v >> 8);
  p[1] = uint8_t(v);
}

void StoreBe32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v >> 24);
  p[1] = uint8_t(v >> 16);
  p[2] = uint8_t(v >> 8);
  p[3] = uint8_t(v);
}

void OverlayAxis(uint8_t* p, int16_t value) {
  if (value != 0) StoreBe16(p, uint16_t(value));
}

}  // namespace

PadState ParsePadLine(std::string_view line) {
  PadState pad;
  const std::string_view text = WithoutComment(line);
  size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && IsSeparator(text[i])) ++i;
    const size_t start = i;
    while (i < text.size() && !IsSeparator(text[i])) ++i;
    if (i > start) ApplyToken(&pad, text.substr(start, i - start));
  }
  return pad;
}

bool IsNeutral(const PadState& pad) { return pad == PadState{}; }

void OverlayPad(uint8_t* state, const PadState& pad, uint32_t packet_bump, uint32_t* result) {
  if (*result != 0) {
    std::memset(state, 0, 16);
    *result = 0;
  }
  StoreBe32(state, LoadBe32(state) + packet_bump);
  StoreBe16(state + 4, uint16_t(LoadBe16(state + 4) | pad.buttons));
  state[6] = std::max(state[6], pad.left_trigger);
  state[7] = std::max(state[7], pad.right_trigger);
  OverlayAxis(state + 8, pad.lx);
  OverlayAxis(state + 10, pad.ly);
  OverlayAxis(state + 12, pad.rx);
  OverlayAxis(state + 14, pad.ry);
}

}  // namespace rex::glue::test_input
