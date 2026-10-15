// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
//
// On-screen touch controls: fingers drive a virtual Xbox 360 pad (gow2_touch_model.h) that the SDK
// merges with real pads for guest user 0, drawn as a translucent ImGui overlay. Cvars: gow2_touch,
// gow2_touch_opacity, gow2_touch_look_gain, gow2_touch_aim_toggle, gow2_touch_scale,
// gow2_touch_hide_seconds.
#pragma once

#include <memory>

namespace rex {
struct RuntimeConfig;
}

namespace rex::ui {
class ImGuiDialog;
class ImGuiDrawer;
}  // namespace rex::ui

namespace rex::glue {

class TouchControls;

// Null when gow2_touch is off, or auto without a touchscreen.
std::shared_ptr<TouchControls> CreateTouchControls(bool has_touchscreen);

// Wraps config.input_factory: the input system it builds also gets the touch driver.
void AddTouchDriver(rex::RuntimeConfig& config, std::shared_ptr<TouchControls> touch);

std::unique_ptr<rex::ui::ImGuiDialog> CreateTouchOverlay(rex::ui::ImGuiDrawer* drawer,
                                                         std::shared_ptr<TouchControls> touch);

}  // namespace rex::glue
