// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Host-side test of the key names and the engine key bindings made from the keybind settings.
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <string_view>

#include "gow2_key_names.h"
#include "gow2_native_bindings.h"

using namespace rex::glue;

int g_checks = 0;
int g_failures = 0;
void Expect(bool ok, std::string_view what) { ++g_checks; if (ok) return; ++g_failures; std::printf("FAIL: %.*s\n", int(what.size()), what.data()); }

bool Has(const std::vector<NativeBinding>& v, const char* key, const char* command, bool shift = false) {
  return std::any_of(v.begin(), v.end(), [&](const NativeBinding& b) {
    return b.key == key && b.command == command && b.shift == shift;
  });
}

int main() {
  // Windows virtual keys.
  Expect(std::string(keynames::EngineNameForVk('W')) == "W", "W");
  Expect(std::string(keynames::EngineNameForVk(0x20)) == "SpaceBar", "space");
  Expect(std::string(keynames::EngineNameForVk('1')) == "One", "digit 1");
  Expect(std::string(keynames::EngineNameForVk(0x61)) == "NumPadOne", "numpad 1");
  Expect(std::string(keynames::EngineNameForVk(0x7B)) == "F12", "F12");
  Expect(std::string(keynames::EngineNameForVk(0x01)) == "LeftMouseButton", "left mouse button");
  Expect(keynames::EngineNameForVk(0x7C) == nullptr, "F13 has no engine key");
  Expect(keynames::EngineNameForVk(0xFF) == nullptr, "unknown vk");

  // Every keybind name of the runtime that the game has a key for.
  const char* const all[] = {"A", "Z", "0", "9", "F1", "F12", "Backtick", "Minus", "Plus", "Comma", "Period",
                             "Semicolon", "Slash", "Backslash", "LBracket", "RBracket", "Quote", "Escape",
                             "Return", "Space", "Tab", "Backspace", "Delete", "Insert", "Home", "End",
                             "PageUp", "PageDown", "Left", "Right", "Up", "Down", "Shift", "Control", "Alt",
                             "Numpad0", "Numpad9", "NumpadEnter", "NumpadPlus", "NumpadMinus", "NumpadStar",
                             "NumpadSlash", "Pause", "CapsLock", "NumLock", "ScrollLock", "LMB", "RMB", "MMB"};
  for (const char* n : all) Expect(!keynames::EngineNamesForBind(n).empty(), std::string("bind name ") + n);
  Expect(keynames::EngineNamesForBind("PrintScreen").empty(), "PrintScreen has no engine key");
  Expect(keynames::EngineNamesForBind("F13").empty(), "F13 has no engine key");
  Expect(keynames::EngineNamesForBind("Shift").size() == 2, "Shift is both shift keys");

  // Chords.
  {
    const auto c = keynames::ParseChords("Semicolon,Space");
    Expect(c.size() == 2 && c[0].engine_names[0] == "Semicolon" && c[1].engine_names[0] == "SpaceBar", "two alternatives");
  }
  {
    const auto c = keynames::ParseChords("Shift+Up");
    Expect(c.size() == 1 && c[0].shift && !c[0].control && c[0].engine_names[0] == "Up", "shift chord");
  }
  {
    const auto c = keynames::ParseChords("Ctrl+Alt+W");
    Expect(c.size() == 1 && c[0].control && c[0].alt && !c[0].shift, "ctrl alt chord");
  }
  Expect(keynames::ParseChords("PrintScreen,Q").size() == 1, "an alternative with no engine key is skipped");
  Expect(keynames::ParseChords("").empty(), "empty value");

  // The bindings.
  std::map<std::string, std::string> settings;
  const auto get = [&](const char* name) {
    auto it = settings.find(name);
    return it == settings.end() ? std::string() : it->second;
  };
  auto b = NativeKeyBindings(get);
  Expect(Has(b, "SpaceBar", "GBA_MoveAction"), "default A is space");
  Expect(Has(b, "F", "GBA_Melee") && Has(b, "V", "GBA_Melee"), "default B is F and V");
  Expect(Has(b, "W", "GBA_MoveForward") && Has(b, "S", "GBA_MoveBackward"), "default W and S move");
  Expect(Has(b, "A", "GBA_StrafeLeft") && Has(b, "D", "GBA_StrafeRight"), "default A and D strafe");
  Expect(Has(b, "LeftMouseButton", "GBA_Shoot") && Has(b, "RightMouseButton", "GBA_Target"), "mouse buttons");
  Expect(Has(b, "Left", "GBA_TurnLeft") && Has(b, "Up", "Axis aLookup Speed=0.75"), "arrow keys look");
  Expect(Has(b, "MouseScrollUp", "GBA_PrevWeapon") && Has(b, "MouseScrollDown", "GBA_NextWeapon"), "wheel switches weapons");
  Expect(Has(b, "Enter", "GBA_Pause") && Has(b, "Escape", "GBA_Pause"), "start");

  settings["keybind_a"] = "Q";
  settings["keybind_dpad_up"] = "Shift+Up";
  b = NativeKeyBindings(get);
  Expect(Has(b, "Q", "GBA_MoveAction") && !Has(b, "SpaceBar", "GBA_MoveAction"), "rebinding A");
  Expect(Has(b, "Up", "GBA_Grenades", true), "a modifier is kept");
  Expect(BindingIniValue({"SpaceBar", "GBA_MoveAction"}) == "(Name=\"SpaceBar\",Command=\"GBA_MoveAction\")", "plain ini value");
  Expect(BindingIniValue({"Up", "GBA_Grenades", true, false, false}) ==
             "(Name=\"Up\",Command=\"GBA_Grenades\",Control=False,Shift=True,Alt=False)", "ini value with a modifier");
  const auto keys = NativeBindingKeys(b);
  Expect(std::set<std::string>(keys.begin(), keys.end()).size() == keys.size(), "keys are unique");

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
