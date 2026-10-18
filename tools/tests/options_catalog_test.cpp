// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Host-side test of the PC Options catalog data (no game data, no SDK).
#include "gow2_options_catalog.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace options = rex::glue::options;

int g_checks = 0;
int g_failures = 0;
void Expect(bool ok, std::string_view what) {
  ++g_checks;
  if (ok) return;
  ++g_failures;
  std::printf("FAIL: %.*s\n", int(what.size()), what.data());
}

void TestIdsUnique() {
  const std::vector<options::Setting>& catalog = options::Catalog();
  std::unordered_set<std::string_view> seen;
  for (const options::Setting& s : catalog) {
    Expect(!s.id.empty(), "id is non-empty");
    Expect(seen.insert(s.id).second, std::string("unique id: ") + std::string(s.id));
  }
}

void TestFindSetting() {
  for (const options::Setting& s : options::Catalog()) {
    const options::Setting* found = options::FindSetting(s.id);
    Expect(found != nullptr, std::string("FindSetting finds: ") + std::string(s.id));
    Expect(found != nullptr && found->id == s.id, "FindSetting returns the same row");
  }
  Expect(options::FindSetting("nope") == nullptr, "FindSetting returns nullptr for nope");
  Expect(options::FindSetting("") == nullptr, "FindSetting returns nullptr for empty");
}

void TestLabels() {
  for (const options::Setting& s : options::Catalog()) {
    Expect(!s.label_en.empty(), std::string("label_en: ") + std::string(s.id));
    Expect(!s.label_es.empty(), std::string("label_es: ") + std::string(s.id));
  }
}

void TestChoices() {
  for (const options::Setting& s : options::Catalog()) {
    if (s.kind != options::Kind::kChoice || s.dynamic_choices) continue;
    Expect(s.choices.size() >= 2, std::string(">= 2 choices: ") + std::string(s.id));
    std::unordered_set<std::string_view> values;
    for (const options::Choice& c : s.choices) {
      Expect(!c.value.empty(), std::string("choice value: ") + std::string(s.id));
      Expect(values.insert(c.value).second,
             std::string("unique choice value: ") + std::string(s.id) + "=" + std::string(c.value));
    }
  }
}

void TestNumbers() {
  for (const options::Setting& s : options::Catalog()) {
    if (s.kind != options::Kind::kNumber) continue;
    Expect(s.min < s.max, std::string("min < max: ") + std::string(s.id));
    Expect(s.step > 0, std::string("step > 0: ") + std::string(s.id));
  }
}

void TestTexts() {
  for (const options::Setting& s : options::Catalog()) {
    if (s.kind != options::Kind::kText) continue;
    Expect(s.max_length > 0, std::string("max_length > 0: ") + std::string(s.id));
  }
}

void TestVisibleIf() {
  for (const options::Setting& s : options::Catalog()) {
    if (s.visible_if_id.empty()) continue;
    Expect(options::FindSetting(s.visible_if_id) != nullptr,
           std::string("visible_if names an id: ") + std::string(s.id) + " -> " +
               std::string(s.visible_if_id));
  }
}

void TestPageTabs() {
  for (const options::Setting& s : options::Catalog()) {
    if (s.page == options::Page::kKeybinds) {
      Expect(s.tab == options::Tab::kControls, "keybinds row is on the controls tab");
    }
    if (s.page == options::Page::kHost) {
      Expect(s.tab == options::Tab::kMultiplayer, "host row is on the multiplayer tab");
    }
  }
}

void TestUiTexts() {
  const std::vector<options::TextId> ids = {
      options::TextId::kTitle,
      options::TextId::kTabDisplay,
      options::TextId::kTabGraphics,
      options::TextId::kTabControls,
      options::TextId::kTabTouch,
      options::TextId::kTabMultiplayer,
      options::TextId::kTabSaves,
      options::TextId::kTabAudio,
      options::TextId::kTabSystem,
      options::TextId::kPageKeybinds,
      options::TextId::kPageHost,
      options::TextId::kOn,
      options::TextId::kOff,
      options::TextId::kApplyNow,
      options::TextId::kApplyNextLevel,
      options::TextId::kApplyRestart,
      options::TextId::kRestartBanner,
      options::TextId::kUnsavedTitle,
      options::TextId::kUnsavedSave,
      options::TextId::kUnsavedDiscard,
      options::TextId::kUnsavedCancel,
      options::TextId::kSaved,
      options::TextId::kSaveFailed,
      options::TextId::kPressAKey,
      options::TextId::kHintChange,
      options::TextId::kHintBack,
      options::TextId::kHintDefault,
      options::TextId::kHintSave,
      options::TextId::kHintTabs,
      options::TextId::kHintClose,
      options::TextId::kEditTextTitle,
      options::TextId::kEditTextOk,
      options::TextId::kEditTextCancel,
  };
  Expect(ids.size() == 33, "all 33 TextIds are listed");
  for (options::TextId id : ids) {
    Expect(!options::Text(id, options::Lang::kEn).empty(), "Text EN is non-empty");
    Expect(!options::Text(id, options::Lang::kEs).empty(), "Text ES is non-empty");
  }
}

void TestPlatformMenuNames() {
#if defined(__ANDROID__)
  constexpr std::string_view title_en = "DEVICE OPTIONS";
  constexpr std::string_view title_es = "OPCIONES DEL DISPOSITIVO";
  constexpr std::string_view entry_en = "Options > Device Options";
  constexpr std::string_view entry_es = "Opciones > Opciones del dispositivo";
#else
  constexpr std::string_view title_en = "PC OPTIONS";
  constexpr std::string_view title_es = "OPCIONES DE PC";
  constexpr std::string_view entry_en = "Options > PC Options";
  constexpr std::string_view entry_es = "Opciones > Opciones de PC";
#endif
  Expect(options::Text(options::TextId::kTitle, options::Lang::kEn) == title_en,
         "English menu title matches the platform");
  Expect(options::Text(options::TextId::kTitle, options::Lang::kEs) == title_es,
         "Spanish menu title matches the platform");
  const options::Setting* shortcut = options::FindSetting("menu_shortcut");
  Expect(shortcut != nullptr, "menu shortcut exists");
  if (!shortcut) return;
  Expect(shortcut->help_en.find(entry_en) != std::string_view::npos,
         "English shortcut help names the platform menu");
  Expect(shortcut->help_es.find(entry_es) != std::string_view::npos,
         "Spanish shortcut help names the platform menu");
}

int CountTab(options::Tab tab) {
  int n = 0;
  for (const options::Setting& s : options::Catalog()) {
    if (s.tab == tab && s.page == options::Page::kMain) ++n;
  }
  return n;
}

int CountPage(options::Page page) {
  int n = 0;
  for (const options::Setting& s : options::Catalog()) {
    if (s.page == page) ++n;
  }
  return n;
}

void TestCounts() {
  Expect(CountTab(options::Tab::kDisplay) == 8, "display has 8 rows");
  Expect(CountTab(options::Tab::kGraphics) == 21, "graphics has 21 rows");
  Expect(CountTab(options::Tab::kControls) == 11, "controls has 11 rows");
  Expect(CountTab(options::Tab::kTouch) == 6, "touch has 6 rows");
  Expect(CountTab(options::Tab::kMultiplayer) == 10, "multiplayer has 10 rows");
  Expect(CountTab(options::Tab::kSaves) == 5, "saves has 5 rows");
  Expect(CountTab(options::Tab::kAudio) == 3, "audio has 3 rows");
  Expect(CountTab(options::Tab::kSystem) == 8, "system has 8 rows");
  Expect(CountPage(options::Page::kKeybinds) == 24, "keybinds page has 24 rows");
  Expect(CountPage(options::Page::kHost) == 7, "host page has 7 rows");
}

int main() {
  try {
    TestIdsUnique();
    TestFindSetting();
    TestLabels();
    TestChoices();
    TestNumbers();
    TestTexts();
    TestVisibleIf();
    TestPageTabs();
    TestUiTexts();
    TestPlatformMenuNames();
    TestCounts();
  } catch (const std::exception& e) {
    ++g_checks;
    ++g_failures;
    std::printf("FAIL: exception: %s\n", e.what());
  } catch (...) {
    ++g_checks;
    ++g_failures;
    std::printf("FAIL: unknown exception\n");
  }
  std::printf("options_catalog_test: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
