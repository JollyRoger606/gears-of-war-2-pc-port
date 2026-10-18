// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Host-side test of the keybind -> keycap character mapping (no SDK).
#include "gow2_keycaps.h"

#include <cstdio>
#include <map>
#include <set>
#include <string_view>

using namespace rex::glue::keycap;

int g_checks = 0;
int g_failures = 0;
void Expect(bool ok, std::string_view what) { ++g_checks; if (ok) return; ++g_failures; std::printf("FAIL: %.*s\n", int(what.size()), what.data()); }

int main() {
  {
    const Binding b = ParseBinding("Semicolon,Space");
    Expect(b.key == "Semicolon" && b.modifiers.empty(), "first alternative wins");
  }
  {
    const Binding b = ParseBinding(" Shift+Up ");
    Expect(b.key == "Up" && b.modifiers.size() == 1 && b.modifiers[0] == "Shift", "modifier prefix");
  }
  {
    const Binding b = ParseBinding("Ctrl+Alt+W,Q");
    Expect(b.key == "W" && b.modifiers.size() == 2 && b.modifiers[0] == "Control" && b.modifiers[1] == "Alt",
           "Ctrl is Control, two modifiers");
  }
  Expect(ParseBinding("").key.empty(), "empty list");

  Expect(CharFor("Space") != 0 && CharFor("W") != 0 && CharFor("LMB") != 0, "common keys have caps");
  Expect(CharFor("NoSuchKey") == 0, "unknown key has none");
  std::set<char16_t> seen;
  for (const char* id : {"A", "B", "Space", "Return", "F24", "Numpad7", "NumpadEnter", "CL_WASD", "IC_MOUSE"})
    Expect(seen.insert(CharFor(id)).second, std::string("distinct character for ") + id);

  std::map<std::string, std::string> settings;
  const auto get = [&](const char* name) { auto it = settings.find(name); return it == settings.end() ? std::string() : it->second; };

  for (const std::string& k : ControllerKeys())
    Expect(!PromptFor(k, get).full.empty(), "default binding has a cap: " + k);

  Expect(PromptFor("Controller_A", get).full == Markup(std::u16string(1, CharFor("Space"))), "A is Space by default");
  settings["keybind_a"] = "Semicolon,Space";
  Expect(PromptFor("Controller_A", get).full == Markup(std::u16string(1, CharFor("Semicolon"))), "rebinding changes the cap");
  settings["keybind_a"] = "Q";
  Expect(PromptFor("Controller_A", get).full == Markup(std::u16string(1, CharFor("Q"))), "rebinding to a letter");

  Expect(PromptFor("Controller_LeftStick", get).full == Markup(std::u16string(1, CharFor("CL_WASD"))), "WASD cluster");
  settings["keybind_lstick_up"] = "Up"; settings["keybind_lstick_left"] = "Left";
  settings["keybind_lstick_down"] = "Down"; settings["keybind_lstick_right"] = "Right";
  Expect(PromptFor("Controller_LeftStick", get).full == Markup(std::u16string(1, CharFor("CL_ARROWS"))), "arrow cluster");
  Expect(PromptFor("Controller_UpDown", get).full == Markup(std::u16string(1, CharFor("PR_UPDOWN"))), "arrow pair");
  settings["keybind_lstick_up"] = "I";
  Expect(PromptFor("Controller_LeftStick", get).full == Markup(std::u16string(1, CharFor("IC_MOVE"))), "other keys: generic move glyph");

  settings["mnk_mouse"] = "false";
  Expect(PromptFor("Controller_RightStick", get).full != Markup(std::u16string(1, CharFor("IC_MOUSE"))), "no mouse look: not the mouse glyph");
  settings["mnk_mouse"] = "true";
  Expect(PromptFor("Controller_RightStick", get).full == Markup(std::u16string(1, CharFor("IC_MOUSE"))), "mouse look: mouse glyph");

  settings["keybind_dpad_up"] = "Shift+Up";
  const Prompt chord = PromptFor("Controller_DPadUp", get);
  Expect(chord.full.size() == chord.brief.size() + 1, "a chord adds the modifier cap");
  Expect(chord.brief == Markup(std::u16string(1, CharFor("Up"))), "brief is the main key");

  settings["keybind_x"] = "NoSuchKey";
  const Prompt none = PromptFor("Controller_X", get);
  Expect(none.full.empty() && none.brief.empty(), "an unknown key falls back to text");

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
