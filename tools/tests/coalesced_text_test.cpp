// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Host-side test of the Coalesced UTF-8 conversion and locale text (no game data, no SDK).
#include "gow2_coalesced_text.h"

#include <cstdio>
#include <string>
#include <string_view>

int g_checks = 0;
int g_failures = 0;
void Expect(bool ok, std::string_view what) {
  ++g_checks;
  if (ok) return;
  ++g_failures;
  std::printf("FAIL: %.*s\n", int(what.size()), what.data());
}

void TestUtf8() {
  using rex::glue::Utf8ToUtf16;
  Expect(Utf8ToUtf16("").empty(), "empty stays empty");
  Expect(Utf8ToUtf16("PC Options") == u"PC Options", "ascii passes through");
  Expect(Utf8ToUtf16("caf\xC3\xA9") == u"caf\u00E9", "2-byte sequence decodes");
  Expect(Utf8ToUtf16("\xE2\x82\xAC") == u"\u20AC", "3-byte sequence decodes");
  const std::u16string smile = Utf8ToUtf16("\xF0\x9F\x98\x80");
  Expect(smile == u"\U0001F600", "4-byte sequence decodes to U+1F600");
  // Surrogates have no \u escape, so the expected halves are numeric.
  const std::u16string pair{char16_t(0xD83D), char16_t(0xDE00)};
  Expect(smile == pair, "astral plane uses a surrogate pair");
  Expect(smile.size() == 2 && smile[0] >= char16_t(0xD800) && smile[0] <= char16_t(0xDBFF) &&
             smile[1] >= char16_t(0xDC00) && smile[1] <= char16_t(0xDFFF),
         "surrogate halves are in range");
  Expect(Utf8ToUtf16("\xF4\x8F\xBF\xBF") ==
             std::u16string{char16_t(0xDBFF), char16_t(0xDFFF)},
         "U+10FFFF still encodes");
  Expect(Utf8ToUtf16("\xFF") == u"\uFFFD", "0xFF becomes U+FFFD");
  Expect(Utf8ToUtf16("\x80") == u"\uFFFD", "lone continuation becomes U+FFFD");
  Expect(Utf8ToUtf16("\xE2\x82") == u"\uFFFD", "truncated sequence becomes U+FFFD");
  Expect(Utf8ToUtf16("\xC0\xAF") == u"\uFFFD", "overlong encoding becomes U+FFFD");
  Expect(Utf8ToUtf16("\xED\xA0\x80") == u"\uFFFD", "encoded surrogate becomes U+FFFD");
  Expect(Utf8ToUtf16("\xF4\x90\x80\x80") == u"\uFFFD", "past U+10FFFF becomes U+FFFD");
  Expect(Utf8ToUtf16("a\xFF"
                     "b") == u"a\uFFFDb",
         "bad byte inside text becomes one U+FFFD");
}

struct Case {
  const char* locale;
  const char* label;
  const char* description;
};

void TestLocales() {
  using rex::glue::PcOptionsEntryText;
  static const Case kCases[] = {
      {"int", "PC Options", "Display, graphics, controls, network and saves on this PC."},
      {"esn", "Opciones de PC", "Pantalla, gráficos, controles, red y partidas en este PC."},
      {"esm", "Opciones de PC", "Pantalla, gráficos, controles, red y partidas en este PC."},
      {"deu", "PC-Optionen", "Anzeige, Grafik, Steuerung, Netzwerk und Spielstände auf diesem PC."},
      {"fra", "Options PC", "Affichage, graphismes, commandes, réseau et sauvegardes sur ce PC."},
      {"ita", "Opzioni PC", "Schermo, grafica, controlli, rete e salvataggi su questo PC."},
      {"pol", "Opcje PC", "Ekran, grafika, sterowanie, sieć i zapisy na tym komputerze."},
      {"cze", "Možnosti PC", "Obraz, grafika, ovládání, síť a uložené pozice na tomto PC."},
      {"hun", "PC-beállítások", "Kép, grafika, irányítás, hálózat és mentések ezen a PC-n."},
      {"rus", "Настройки ПК", "Экран, графика, управление, сеть и сохранения на этом ПК."},
      {"kor", "PC 옵션", "이 PC의 화면, 그래픽, 조작, 네트워크 및 저장 설정."},
      {"cht", "PC 選項", "此電腦的顯示、圖形、控制、網路與存檔設定。"},
  };
  for (const Case& c : kCases) {
    const rex::glue::LocalizedPair got = PcOptionsEntryText(c.locale);
    char what[64];
    std::snprintf(what, sizeof(what), "locale %s has its own text", c.locale);
    Expect(got.label == std::string_view(c.label) &&
               got.description == std::string_view(c.description),
           what);
  }
  static const char* const kFolded[] = {"INT", "ESN", "ESM", "DEU", "FRA", "ITA",
                                        "POL", "CZE", "HUN", "RUS", "KOR", "CHT"};
  for (size_t i = 0; i < 12; ++i) {
    const rex::glue::LocalizedPair upper = PcOptionsEntryText(kFolded[i]);
    char what[64];
    std::snprintf(what, sizeof(what), "locale %s matches case-insensitively", kFolded[i]);
    Expect(upper.label == std::string_view(kCases[i].label) &&
               upper.description == std::string_view(kCases[i].description),
           what);
  }
  Expect(PcOptionsEntryText("EsN").label == PcOptionsEntryText("esn").label,
         "mixed case matches too");
  for (const char* unknown : {"jpn", "xxx", "", "e", "esnn", "in"}) {
    const rex::glue::LocalizedPair got = PcOptionsEntryText(unknown);
    char what[64];
    std::snprintf(what, sizeof(what), "unknown locale '%s' falls back to int", unknown);
    Expect(got.label == std::string_view("PC Options") &&
               got.description ==
                   std::string_view("Display, graphics, controls, network and saves on this PC."),
           what);
  }
  Expect(PcOptionsEntryText("esm").label == PcOptionsEntryText("esn").label &&
             PcOptionsEntryText("esm").description == PcOptionsEntryText("esn").description,
         "esm shares the esn text");
}

void TestLocalizedConversion() {
  using rex::glue::PcOptionsEntryText;
  using rex::glue::Utf8ToUtf16;
  Expect(Utf8ToUtf16(PcOptionsEntryText("kor").label) == u"PC \uC635\uC158",
         "korean label converts to the expected code units");
  Expect(Utf8ToUtf16(PcOptionsEntryText("rus").label) ==
             u"\u041D\u0430\u0441\u0442\u0440\u043E\u0439\u043A\u0438 \u041F\u041A",
         "russian label converts to the expected code units");
  static const char* const kLocales[] = {"int", "esn", "esm", "deu", "fra", "ita",
                                         "pol", "cze", "hun", "rus", "kor", "cht"};
  for (const char* loc : kLocales) {
    const rex::glue::LocalizedPair t = PcOptionsEntryText(loc);
    const std::u16string label = Utf8ToUtf16(t.label);
    const std::u16string desc = Utf8ToUtf16(t.description);
    char what[64];
    std::snprintf(what, sizeof(what), "locale %s converts with no U+FFFD", loc);
    Expect(!t.label.empty() && !t.description.empty() &&
               label.find(u'\uFFFD') == std::u16string::npos &&
               desc.find(u'\uFFFD') == std::u16string::npos,
           what);
  }
}

int main() {
  TestUtf8();
  TestLocales();
  TestLocalizedConversion();
  std::printf("coalesced_text_test: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
