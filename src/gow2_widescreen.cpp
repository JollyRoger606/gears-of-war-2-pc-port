// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
#include "gow2_widescreen.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <optional>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <rex/cvar.h>
#include <rex/logging.h>

#include "gow2_dev_commands.h"
#include "gow2_dev_guest_calls.h"
#include "gow2_engine_tick.h"
#include "gow2_guest.h"
#include "gow2_widescreen_math.h"

REXCVAR_DEFINE_STRING(gow2_widescreen, "auto", "Display",
                      "Ultrawide support: auto (on when this PC's main display is wider than 16:9), off, or an "
                      "aspect such as 21:9 or 2.39. The game then renders wider instead of showing black bars; the "
                      "field of view is widened to keep the vertical view. Takes effect on the next "
                      "start. Videos and some menus are still 16:9 stretched.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(gow2_ui_projection_fix, true, "Display",
                    "With widescreen on, keep the menus the size they have at 16:9: the game's UI scene client "
                    "builds its canvas-to-screen projection with a distance that grows with the square of the "
                    "width, which shrinks every menu to (1280 / width) squared. The distance is put back at the "
                    "value that gives one canvas pixel one back buffer pixel.");

REXCVAR_DEFINE_BOOL(gow2_ui_widen, true, "Display",
                    "With widescreen on, stretch the full-width UI images (title and pause bars, backdrops), which the "
                    "game authors 1280 pixels wide, to the whole picture.");

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

namespace {

float FloatAt(const uint8_t* base, uint32_t addr) { return LoadFloat(base, addr); }

void StoreFloat(uint8_t* base, uint32_t addr, float v) {
  uint32_t bits;
  std::memcpy(&bits, &v, 4);
  guest::StoreU32(base, addr, bits);
}

// The UI scene client's canvas-to-screen matrix (client + 112): m00 = 1.8182 / aspect and m11 = -1.8182 fix the
// scale, and the translation row (+160 .. +172) ends with the perspective distance D (+172). A canvas pixel is
// one pixel on screen when D = |m11| * height / 2 (640 at 704 lines). The game computes D = (width / 2) / m00,
// which is that only at 1280 wide: at 1680 the whole UI is drawn at 640 / 1102 = 0.58 of its size.
void FixUiProjectionTick(const PPCContext& ctx, uint8_t* base) {
  (void)ctx;
  static uint32_t client = 0;
  static uint32_t counter = 0;
  if (WidescreenBackBufferWidth() <= 0 || !REXCVAR_GET(gow2_ui_projection_fix)) return;
  auto find_client = [&] {
    const uint32_t objects = guest::LoadU32(base, kGObjObjects);
    const uint32_t count = guest::LoadU32(base, kGObjObjects + 4);
    if (objects == 0 || count > kMaxObjects) return;
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t object = guest::LoadU32(base, objects + 4 * i);
      if (object == 0) continue;
      if (ClassNameOf(base, object).find("GameUISceneClient") == std::string::npos) continue;
      if (ObjectName(base, object).rfind("Default__", 0) == 0) continue;
      client = object;
      break;
    }
  };
  if (client == 0) {
    if ((counter++ % 120) != 0) return;
    (void)guest_call::Guarded(find_client);
    if (client == 0) return;
  }
  auto fix = [&] {
    const float m00 = FloatAt(base, client + 112), m11 = FloatAt(base, client + 132);
    const float tz = FloatAt(base, client + 168), tw = FloatAt(base, client + 172);
    if (!(m00 > 0.01f) || !(m11 < -0.01f)) return;
    const float wanted = std::fabs(m11) * 0.5f * float(WidescreenBackBufferHeight());
    if (std::fabs(tw - wanted) < 0.25f) return;
    StoreFloat(base, client + 172, wanted);
    StoreFloat(base, client + 168, tz + (wanted - tw));
    static std::atomic<int> logged{0};
    if (logged.fetch_add(1) < 6)
      REXLOG_INFO("WIDESCREEN UI projection distance {:.1f} -> {:.1f} (m00 {:.4f}, m11 {:.4f})", tw, wanted, m00, m11);
  };
  (void)guest_call::Guarded(fix);
}

// Full-width UI images (title and pause bars, backdrops) are authored 0 .. 1280 pixels wide: Position.Value[2]
// (+68) of the UIImage is 1280. They are widened to the real canvas width. Position.bInvalidated[2] (+82) makes
// the engine resolve the new value.
void WidenUiImagesTick(const PPCContext& ctx, uint8_t* base) {
  (void)ctx;
  static uint32_t counter = 0;
  static std::unordered_map<uint32_t, bool> is_image;  // class pointer -> class is UIImage
  const int width = WidescreenBackBufferWidth();
  if (width <= 1280 || !REXCVAR_GET(gow2_ui_widen)) return;
  // Widgets appear when a scene opens: walk when the object table grew, and now and then for reused slots.
  static uint32_t last_count = 0;
  const uint32_t table = guest::LoadU32(base, kGObjObjects + 4);
  const bool periodic = (counter++ % 120) == 0;
  if (table == last_count && !periodic) return;
  last_count = table;
  auto walk = [&] {
    const uint32_t objects = guest::LoadU32(base, kGObjObjects);
    const uint32_t count = guest::LoadU32(base, kGObjObjects + 4);
    if (objects == 0 || count > kMaxObjects) return;
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t object = guest::LoadU32(base, objects + 4 * i);
      if (object == 0) continue;
      const uint32_t cls = guest::LoadU32(base, object + kObjClass);
      auto it = is_image.find(cls);
      if (it == is_image.end()) it = is_image.emplace(cls, ClassNameOf(base, object) == "UIImage").first;
      if (!it->second || FloatAt(base, object + 68) != 1280.0f ||
          FloatAt(base, object + 60) != 0.0f)
        continue;
      StoreFloat(base, object + 68, float(width));
      guest::StoreU8(base, object + 82, 1);
      static std::atomic<int> logged{0};
      if (logged.fetch_add(1) < 40)
        REXLOG_INFO("WIDESCREEN UI image {} widened: L {:.0f} T {:.0f} R {:.0f}->{} B {:.0f} types {:08X}",
                    ObjectName(base, object), FloatAt(base, object + 60), FloatAt(base, object + 64), 1280.0, width,
                    FloatAt(base, object + 72), guest::LoadU32(base, object + 76));
    }
  };
  (void)guest_call::Guarded(walk);
}

[[maybe_unused]] const bool kUiWidenRegistered = (AddEngineTickListener(&WidenUiImagesTick), true);

[[maybe_unused]] const bool kUiTickRegistered = (AddEngineTickListener(&FixUiProjectionTick), true);

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

uint32_t FindGuestObject(const uint8_t* base, const char* class_name, bool scene_only) {
  uint32_t found = 0;
  auto walk = [&] {
    const uint32_t objects = guest::LoadU32(base, kGObjObjects);
    const uint32_t count = guest::LoadU32(base, kGObjObjects + 4);
    if (objects == 0 || count > kMaxObjects) return;
    for (uint32_t i = 0; i < count && found == 0; ++i) {
      const uint32_t object = guest::LoadU32(base, objects + 4 * i);
      if (object == 0) continue;
      if (ClassNameOf(base, object) != class_name) continue;
      if (ObjectName(base, object).rfind("Default__", 0) == 0) continue;
      if (scene_only && guest::LoadU32(base, object + 424) == 0) continue;
      found = object;
    }
  };
  (void)guest_call::Guarded(walk);
  return found;
}

std::string DevAlert(const PPCContext& ctx, uint8_t* base, uint32_t type, uint32_t id) {
  uint32_t manager = 0;
  auto find = [&] {
    const uint32_t objects = guest::LoadU32(base, kGObjObjects);
    const uint32_t count = guest::LoadU32(base, kGObjObjects + 4);
    if (objects == 0 || count > kMaxObjects) return;
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t object = guest::LoadU32(base, objects + 4 * i);
      if (object == 0) continue;
      if (ClassNameOf(base, object) != "GearAlertManager") continue;
      if (ObjectName(base, object).rfind("Default__", 0) == 0) continue;
      manager = object;
    }
  };
  if (!guest_call::Guarded(find) || manager == 0) return "no GearAlertManager object";
  const std::optional<uint64_t> name = guest_call::MakeName(ctx, base, "MadeProgress", false);
  if (!name) return "no name MadeProgress";
  const uint32_t fn = guest_call::FindFunction(ctx, base, manager, *name);
  if (fn == 0) return "no function MadeProgress";
  std::vector<uint8_t> parms(8, 0);
  parms[0] = uint8_t(type);
  parms[4] = uint8_t(id >> 24); parms[5] = uint8_t(id >> 16); parms[6] = uint8_t(id >> 8); parms[7] = uint8_t(id);
  std::vector<uint8_t> out;
  const bool ok = guest_call::ProcessEventWithParms(ctx, base, manager, fn, parms, &out, 8);
  return std::string("MadeProgress ") + (ok ? "called" : "failed");
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
  // The UI scale natives: where their code is (the UFunction's words that point into the game's code).
  auto native_ptrs = [&](const char* function_name) {
    std::string text;
    auto walk_fn = [&] {
      const uint32_t objects = guest::LoadU32(base, kGObjObjects);
      const uint32_t count = guest::LoadU32(base, kGObjObjects + 4);
      if (objects == 0 || count > kMaxObjects) return;
      for (uint32_t i = 0; i < count; ++i) {
        const uint32_t object = guest::LoadU32(base, objects + 4 * i);
        if (object == 0) continue;
        if (ObjectName(base, object).rfind(function_name, 0) != 0) continue;
        if (ClassNameOf(base, object) != "Function") continue;
        char t[64];
        std::snprintf(t, sizeof t, " %s@%08X:", function_name, object);
        text += t;
        for (uint32_t off = 56; off < 200; off += 4) {
          const uint32_t w = guest::LoadU32(base, object + off);
          if (w >= 0x82000000 && w < 0x83000000) {
            std::snprintf(t, sizeof t, " +%u=%08X", off, w);
            text += t;
          }
        }
      }
    };
    (void)guest_call::Guarded(walk_fn);
    return text;
  };
  out += " | natives:" + native_ptrs("GetAspectRatioAutoScaleFactor") + native_ptrs("GetViewportSize") + native_ptrs("GetCanvasToScreen") + native_ptrs("CanvasToScreen") + native_ptrs("GetAutoScaleValue") + native_ptrs("SetAutoScaling");
  // UI scenes: the viewport size a scene hands to its widgets (UIScreenObject::GetViewportSize reads two floats
  // at +1104 / +1108 of the scene).
  {
    std::string scenes;
    auto walk_scenes = [&] {
      const uint32_t objects = guest::LoadU32(base, kGObjObjects);
      const uint32_t count = guest::LoadU32(base, kGObjObjects + 4);
      if (objects == 0 || count > kMaxObjects) return;
      int shown = 0;
      for (uint32_t i = 0; i < count && shown < 8; ++i) {
        const uint32_t object = guest::LoadU32(base, objects + 4 * i);
        if (object == 0) continue;
        const std::string cls = ClassNameOf(base, object);
        if (cls.find("UIScene") == std::string::npos && cls.find("SceneClient") == std::string::npos) continue;
        const std::string name = ObjectName(base, object);
        if (name.rfind("Default__", 0) == 0) continue;
        if (guest::LoadU32(base, object + 424) == 0) continue;  // not set up (no scene client): not on screen
        char t[200];
        std::snprintf(t, sizeof t, " [%s %s %.1fx%.1f|%u,%u]", cls.c_str(), name.c_str(),
                      LoadFloat(base, object + 1104), LoadFloat(base, object + 1108),
                      guest::LoadU32(base, object + 1104), guest::LoadU32(base, object + 1108));
        scenes += t;
        ++shown;
      }
    };
    (void)guest_call::Guarded(walk_scenes);
    out += " | ui scenes:" + scenes;
    // Debug aid: the raw words of every active scene and scene client, one UIDUMP line each, to compare two runs.
    auto dump_objects = [&] {
      const uint32_t objects = guest::LoadU32(base, kGObjObjects);
      const uint32_t count = guest::LoadU32(base, kGObjObjects + 4);
      if (objects == 0 || count > kMaxObjects) return;
      for (uint32_t i = 0; i < count; ++i) {
        const uint32_t object = guest::LoadU32(base, objects + 4 * i);
        if (object == 0) continue;
        const std::string cls = ClassNameOf(base, object);
        const bool client = cls.find("UISceneClient") != std::string::npos;
        const bool scene = cls.find("UIScene") != std::string::npos;
        if (!client && !scene) continue;
        const std::string name = ObjectName(base, object);
        if (name.rfind("Default__", 0) == 0) continue;
        if (!client && scene && guest::LoadU32(base, object + 424) == 0) continue;
        std::string hex;
        char t[16];
        for (uint32_t off = 0; off < 0xA00; off += 4) {
          std::snprintf(t, sizeof t, "%08X", guest::LoadU32(base, object + off));
          hex += t;
        }
        REXLOG_INFO("UIDUMP {} {} {}", cls, name, hex);
      }
    };
    (void)guest_call::Guarded(dump_objects);
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
