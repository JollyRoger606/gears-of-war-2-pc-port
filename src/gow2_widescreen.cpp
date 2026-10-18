// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
#include "gow2_widescreen.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <optional>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <rex/cvar.h>
#include <rex/logging.h>

#include "gow2_dev_commands.h"
#include "gow2_dev_guest_calls.h"
#include "gow2_guest.h"
#include "gow2_widescreen_math.h"

REXCVAR_DEFINE_STRING(gow2_widescreen, "off", "Display",
                      "Ultrawide support: off, auto (the aspect of this PC's main display), an aspect such "
                      "as 21:9 or 2.39. The game then renders wider instead of showing black bars; the "
                      "field of view is widened to keep the vertical view. Takes effect on the next "
                      "start. Videos and some menus are still 16:9 stretched.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rex::glue {
namespace {

std::atomic<int> g_site_calls[8];     // how often each FOV site ran (diagnostics for !viewinfo)
std::atomic<float> g_site_last[8];    // the last FOV (degrees) each saw

double DisplayAspect() {
#if defined(_WIN32)
  const int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
  if (w > 0 && h > 0) return double(w) / double(h);
#endif
  return 0.0;
}

constexpr uint32_t kGNames = 0x83136834;
constexpr uint32_t kGObjObjects = 0x83136860;
constexpr uint32_t kObjNameIndex = 44;
constexpr uint32_t kObjNameNumber = 48;
constexpr uint32_t kObjClass = 52;
constexpr uint32_t kNameEntryText = 8;
constexpr uint32_t kNameWideBit = 1;
constexpr uint32_t kMaxObjects = 1u << 22;
constexpr uint32_t kMaxNameLength = 128;

std::string NameText(const uint8_t* base, uint32_t index) {
  const uint32_t names = guest::LoadU32(base, kGNames);
  const uint32_t count = guest::LoadU32(base, kGNames + 4);
  if (names == 0 || index >= count) return "?";
  const uint32_t entry = guest::LoadU32(base, names + 4 * index);
  if (entry == 0) return "?";
  const bool wide = guest::LoadU32(base, entry) & kNameWideBit;
  std::string out;
  for (uint32_t i = 0; i < kMaxNameLength; ++i) {
    const uint32_t at = entry + kNameEntryText + i * (wide ? 2 : 1);
    const uint32_t c = wide ? guest::LoadU16(base, at) : guest::LoadU8(base, at);
    if (c == 0) break;
    out.push_back(c < 128 ? char(c) : '?');
  }
  return out;
}

std::string ClassNameOf(const uint8_t* base, uint32_t object) {
  const uint32_t cls = guest::LoadU32(base, object + kObjClass);
  return cls == 0 ? std::string("?") : NameText(base, guest::LoadU32(base, cls + kObjNameIndex));
}

std::string ObjectName(const uint8_t* base, uint32_t object) {
  return NameText(base, guest::LoadU32(base, object + kObjNameIndex)) + "_" +
         std::to_string(guest::LoadU32(base, object + kObjNameNumber));
}

float LoadFloat(const uint8_t* base, uint32_t addr) {
  const uint32_t bits = guest::LoadU32(base, addr);
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

float BE32ToFloat(const std::vector<uint8_t>& v, size_t at) {
  const uint32_t bits = (uint32_t(v[at]) << 24) | (uint32_t(v[at + 1]) << 16) | (uint32_t(v[at + 2]) << 8) | v[at + 3];
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

}  // namespace

double WidescreenAspect() {
  static const double aspect = widescreen::ParseAspect(REXCVAR_GET(gow2_widescreen), DisplayAspect());
  return aspect;
}

int WidescreenBackBufferHeight() {
  return rex::cvar::Query<bool>("gow2_ao_upscaling_fix") ? 704 : 720;
}

int WidescreenBackBufferWidth() {
  static const int width = [] {
    return widescreen::BackBufferWidth(WidescreenAspect(), WidescreenBackBufferHeight());
  }();
  return width;
}

void PrepareWidescreen() {
  const int w = WidescreenBackBufferWidth();
  if (w <= 0) return;
  const int h = WidescreenBackBufferHeight();
  // Explicit video mode settings of the player win.
  if (rex::cvar::GetFlagSource("video_mode_width") == rex::cvar::Source::kDefault)
    (void)rex::cvar::SetFlagByName("video_mode_width", std::to_string(w));
  if (rex::cvar::GetFlagSource("video_mode_height") == rex::cvar::Source::kDefault)
    (void)rex::cvar::SetFlagByName("video_mode_height", std::to_string(h));
  REXLOG_INFO("WIDESCREEN gow2_widescreen '{}' -> aspect {:.4f}, back buffer {}x{}", REXCVAR_GET(gow2_widescreen),
              WidescreenAspect(), w, h);
}

int WidescreenMaxShadowResolution() {
  // The game's 800 is only safe at 1280 wide: at 1680 the shadow map overwrote part of the scene and left
  // dark rectangles on characters.
  return widescreen::MaxShadowResolution(WidescreenBackBufferWidth(), WidescreenBackBufferHeight());
}

double WidescreenHalfFov(double half_fov, int site) {
  const int w = WidescreenBackBufferWidth();
  if (w <= 0) return half_fov;
  const double widened = widescreen::WidenHalfFov(half_fov, w);
  if (site >= 0 && site < 8) {
    g_site_calls[site].fetch_add(1, std::memory_order_relaxed);
    g_site_last[site].store(float(half_fov * 360.0 / 3.14159265358979), std::memory_order_relaxed);
  }
  static std::atomic<int> logged[8];
  if (site >= 0 && site < 8 && logged[site].fetch_add(1) < 3)
    REXLOG_INFO("WIDESCREEN site {} half fov {:.4f} rad ({:.1f} deg) -> {:.4f} rad ({:.1f} deg)", site, half_fov,
                half_fov * 360.0 / 3.14159265358979, widened, widened * 360.0 / 3.14159265358979);
  return widened;
}

std::string ViewInfo(const PPCContext& ctx, uint8_t* base) {
  std::string out = "VIEWINFO";
  uint32_t w = 0, h = 0;
  auto read = [&] {
    // The screen size the game's device setup stored (sub_82806A80): width, then height.
    w = guest::LoadU32(base, 0x830C9420);
    h = guest::LoadU32(base, 0x830C9424);
  };
  if (guest_call::Guarded(read)) out += " screen=" + std::to_string(w) + "x" + std::to_string(h);
  std::vector<uint32_t> viewports;
  auto walk = [&] {
    const uint32_t objects = guest::LoadU32(base, kGObjObjects);
    const uint32_t count = guest::LoadU32(base, kGObjObjects + 4);
    if (objects == 0 || count > kMaxObjects) return;
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t object = guest::LoadU32(base, objects + 4 * i);
      if (object == 0) continue;
      if (ClassNameOf(base, object).find("ViewportClient") == std::string::npos) continue;
      if (ObjectName(base, object).rfind("Default__", 0) == 0) continue;
      viewports.push_back(object);
    }
  };
  if (!guest_call::Guarded(walk)) return out + " fault while walking the object table";
  const std::optional<uint64_t> name = guest_call::MakeName(ctx, base, "GetViewportSize", false);
  for (uint32_t viewport : viewports) {
    out += " | viewport " + ObjectName(base, viewport);
    if (!name) continue;
    const uint32_t fn = guest_call::FindFunction(ctx, base, viewport, *name);
    if (fn == 0) continue;
    std::vector<uint8_t> parms_out;
    if (guest_call::ProcessEventWithParms(ctx, base, viewport, fn, std::vector<uint8_t>(8, 0), &parms_out, 8) &&
        parms_out.size() >= 8) {
      char text[80];
      std::snprintf(text, sizeof text, " size=%.0fx%.0f", BE32ToFloat(parms_out, 0), BE32ToFloat(parms_out, 4));
      out += text;
    }
  }
  out += " | fov sites:";
  for (int i = 1; i < 8; ++i) {
    char t[48];
    std::snprintf(t, sizeof t, " %d=%dx(%.0f)", i, g_site_calls[i].load(), double(g_site_last[i].load()));
    out += t;
  }
  REXLOG_INFO("{}", out);
  return out;
}

}  // namespace rex::glue
