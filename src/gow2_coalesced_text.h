// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// UTF-8 to UTF-16 conversion and the per-locale PC Options entry text.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace rex::glue {

inline std::u16string Utf8ToUtf16(std::string_view s) {
  std::u16string r;
  r.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      r.push_back(char16_t(c));
      ++i;
      continue;
    }
    char32_t cp = 0;
    size_t len = 0;
    if ((c & 0xE0) == 0xC0) {
      cp = char32_t(c & 0x1F);
      len = 2;
    } else if ((c & 0xF0) == 0xE0) {
      cp = char32_t(c & 0x0F);
      len = 3;
    } else if ((c & 0xF8) == 0xF0) {
      cp = char32_t(c & 0x07);
      len = 4;
    } else {
      r.push_back(u'\uFFFD');  // stray continuation byte or 0xF8 and up
      ++i;
      continue;
    }
    if (i + len > s.size()) {
      r.push_back(u'\uFFFD');  // truncated sequence
      break;
    }
    bool ok = true;
    for (size_t j = 1; j < len; ++j) {
      const unsigned char d = static_cast<unsigned char>(s[i + j]);
      if ((d & 0xC0) != 0x80) {
        ok = false;
        break;
      }
      cp = (cp << 6) | char32_t(d & 0x3F);
    }
    if (!ok) {
      r.push_back(u'\uFFFD');
      ++i;
      continue;
    }
    constexpr char32_t kMin[] = {0, 0, 0x80, 0x800, 0x10000};
    if (cp < kMin[len] || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
      r.push_back(u'\uFFFD');  // overlong, surrogate or out of range
      i += len;
      continue;
    }
    if (cp > 0xFFFF) {
      cp -= 0x10000;
      r.push_back(char16_t(0xD800 + (cp >> 10)));
      r.push_back(char16_t(0xDC00 + (cp & 0x3FF)));
    } else {
      r.push_back(char16_t(cp));
    }
    i += len;
  }
  return r;
}

struct LocalizedPair {
  std::string_view label;
  std::string_view description;
};

inline bool LocaleIs(std::string_view locale, const char* want) {
  if (locale.size() != 3) return false;
  for (size_t i = 0; i < 3; ++i) {
    char c = locale[i];
    if (c >= 'A' && c <= 'Z') c = char(c + ('a' - 'A'));
    if (c != want[i]) return false;
  }
  return true;
}

// Text for the Options menu's Storage Location button, repurposed as the PC Options entry.
// Unknown locales fall back to English.
inline LocalizedPair PcOptionsEntryText(std::string_view locale) {
  if (LocaleIs(locale, "esn") || LocaleIs(locale, "esm"))
    return {"Opciones de PC", "Pantalla, gráficos, controles, red y partidas en este PC."};
  if (LocaleIs(locale, "deu"))
    return {"PC-Optionen", "Anzeige, Grafik, Steuerung, Netzwerk und Spielstände auf diesem PC."};
  if (LocaleIs(locale, "fra"))
    return {"Options PC", "Affichage, graphismes, commandes, réseau et sauvegardes sur ce PC."};
  if (LocaleIs(locale, "ita"))
    return {"Opzioni PC", "Schermo, grafica, controlli, rete e salvataggi su questo PC."};
  if (LocaleIs(locale, "pol"))
    return {"Opcje PC", "Ekran, grafika, sterowanie, sieć i zapisy na tym komputerze."};
  if (LocaleIs(locale, "cze"))
    return {"Možnosti PC", "Obraz, grafika, ovládání, síť a uložené pozice na tomto PC."};
  if (LocaleIs(locale, "hun"))
    return {"PC-beállítások", "Kép, grafika, irányítás, hálózat és mentések ezen a PC-n."};
  if (LocaleIs(locale, "rus"))
    return {"Настройки ПК", "Экран, графика, управление, сеть и сохранения на этом ПК."};
  if (LocaleIs(locale, "kor")) return {"PC 옵션", "이 PC의 화면, 그래픽, 조작, 네트워크 및 저장 설정."};
  if (LocaleIs(locale, "cht")) return {"PC 選項", "此電腦的顯示、圖形、控制、網路與存檔設定。"};
  return {"PC Options", "Display, graphics, controls, network and saves on this PC."};
}

}  // namespace rex::glue
