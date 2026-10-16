// gow2 - ReXGlue Recompiled Project
//
// Test input that does not need window focus: a text file names the pad buttons and stick values
// held for guest user 0, and they are merged into what XamInputGetState returns. Lets a script
// drive one of several instances on one PC, or an instance behind a system dialog.
//
// Line format, case-insensitive, separated by spaces, tabs or commas, '#' starts a comment:
//   A B X Y LB RB LT RT START BACK UP DOWN LEFT RIGHT LS RS   LX=<-1..1> LY= RX= RY=
#pragma once

#include <cstdint>
#include <string_view>

namespace rex::glue {

// Called after the runtime's XamInputGetState; result is its return value (0 = success).
void ApplyTestInput(uint8_t* base, uint32_t user_index, uint32_t state_addr, uint32_t* result);

}  // namespace rex::glue

namespace rex::glue::test_input {

struct PadState {
  uint16_t buttons = 0;
  uint8_t left_trigger = 0;
  uint8_t right_trigger = 0;
  int16_t lx = 0, ly = 0, rx = 0, ry = 0;
  bool operator==(const PadState&) const = default;
};

PadState ParsePadLine(std::string_view line);

bool IsNeutral(const PadState& pad);

bool CurrentPad(PadState* out);

// state: a 16-byte big-endian XINPUT_STATE in host memory. A failed call (result != 0) is turned
// into a connected pad that holds only what the file says.
void OverlayPad(uint8_t* state, const PadState& pad, uint32_t packet_bump, uint32_t* result);

}  // namespace rex::glue::test_input
