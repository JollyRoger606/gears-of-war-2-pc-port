// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Native keyboard input. While a level is being played (a pawn exists, the game clock runs, no PC
// Options menu), key and mouse-button events go to the engine as the keys they are
// (UGameViewportClient::InputKey with "SpaceBar", "W", "LeftMouseButton"...), where the bindings
// that src/gow2_native_bindings.cpp adds to the game's Xe-GearInput.ini turn them into the game's
// commands. With gow2_native_menus (default) the menus and the pause menu use the native path too, with no emulated pad
// (SDK patch 0022 switches it off): keys the menu widgets know go as themselves, Escape as Back, and the others as the pad
// button their keybind_* setting stands for, because the game's menu scripts read pad button names (see MenuRoutes).
#pragma once

#include <vector>

#include "gow2_native_bindings.h"

namespace rex::ui {
class Window;
}

namespace rex::glue {

// gow2_native_keys is on and native mouse look is on (the hook it rides on).
bool NativeKeysWanted();
void InstallNativeKeys(rex::ui::Window* window);
// True while keys are sent natively (for tests and logs).
bool NativeKeysActive();
// The bindings the game's input bundle was made with at start. Keys the player changes later in
// the options are routed to these (RouteKey), so a change applies while playing.
void RememberBakedBindings(std::vector<NativeBinding> bindings);

}  // namespace rex::glue
