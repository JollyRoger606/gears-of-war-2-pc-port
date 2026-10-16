// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
//
// State shared by the PC Options menu and the input paths it must silence while it is open
// (native mouse look, touch controls), and the menu's own read of the real pads.
#pragma once

#include <cstdint>

namespace rex::ui {
class Window;
}

namespace rex::glue {

bool HostUiOpen();
void SetHostUiOpen(bool open);
void ToggleRuntimeSettings(rex::ui::Window* window);

struct HostPad {
  bool connected = false;
  uint16_t buttons = 0;  // XInput button bits
  float lx = 0;          // -1..1
  float ly = 0;          // -1..1, up is positive
  float rx = 0, ry = 0;
  uint8_t lt = 0, rt = 0;
};

// User 0's pad as the runtime's input system reports it, through the same lock the guest's input
// calls take (gow2_xam_input_guard on Windows, the SDK's own lock on Android).
HostPad ReadHostPad();

}  // namespace rex::glue
