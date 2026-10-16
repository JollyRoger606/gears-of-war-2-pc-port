// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Host menu state and access to the runtime's input and settings.

#include "gow2_host_ui.h"
#include "gow2_mouse_aim.h"
#include "gow2_test_input.h"
#include "gow2_xam_input_guard.h"

#include <algorithm>
#include <atomic>
#include <string>

#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>

namespace rex::glue {
namespace {

std::atomic<bool> g_host_ui_open{false};

float NormalizeStick(int16_t value) {
  return static_cast<float>(value) / (value < 0 ? 32768.0f : 32767.0f);
}

}  // namespace

bool HostUiOpen() {
  return g_host_ui_open.load(std::memory_order_acquire);
}

void SetHostUiOpen(bool open) {
  g_host_ui_open.store(open, std::memory_order_release);
  if (open) DiscardNativeMouseInput();
}

HostPad ReadHostPad() {
  rex::Runtime* runtime = rex::Runtime::instance();
  if (!runtime || !runtime->input_system()) return {};
  rex::input::X_INPUT_STATE state{};
  X_RESULT result = X_ERROR_DEVICE_NOT_CONNECTED;
  auto* input = static_cast<rex::input::InputSystem*>(runtime->input_system());
  const auto read = [&] { result = input->GetState(0, &state); };
#ifdef _WIN32
  WithXamInputLock(read);
#else
  read();
#endif
  HostPad pad{};
  if (result == X_ERROR_SUCCESS) {
    pad = {true, static_cast<uint16_t>(state.gamepad.buttons),
           NormalizeStick(static_cast<int16_t>(state.gamepad.thumb_lx)),
           NormalizeStick(static_cast<int16_t>(state.gamepad.thumb_ly)),
           NormalizeStick(static_cast<int16_t>(state.gamepad.thumb_rx)),
           NormalizeStick(static_cast<int16_t>(state.gamepad.thumb_ry)),
           state.gamepad.left_trigger, state.gamepad.right_trigger};
  }
  test_input::PadState scripted;
  if (test_input::CurrentPad(&scripted)) {
    pad.buttons |= scripted.buttons;
    if (scripted.lx != 0 || scripted.ly != 0) {
      pad.lx = NormalizeStick(scripted.lx);
      pad.ly = NormalizeStick(scripted.ly);
    }
    if (scripted.rx != 0 || scripted.ry != 0) {
      pad.rx = NormalizeStick(scripted.rx);
      pad.ry = NormalizeStick(scripted.ry);
    }
    pad.lt = std::max(pad.lt, scripted.left_trigger);
    pad.rt = std::max(pad.rt, scripted.right_trigger);
    pad.connected = true;
  }
  return pad;
}

void ToggleRuntimeSettings(rex::ui::Window* window) {
  if (!window) return;
  const std::string key = rex::cvar::Query<std::string>("bind_settings");
  const rex::ui::VirtualKey virtual_key = rex::ui::ParseVirtualKey(key);
  if (virtual_key == rex::ui::VirtualKey::kNone) {
    REXLOG_WARN("PCOPTIONS runtime settings: bind_settings '{}' is not a key", key);
    return;
  }
  rex::ui::KeyEvent event(window, virtual_key, 1, false, false, false, false, false);
  if (!rex::ui::ProcessKeyEvent(event)) {
    REXLOG_WARN("PCOPTIONS runtime settings: no bind answered {}", key);
  }
}

}  // namespace rex::glue
