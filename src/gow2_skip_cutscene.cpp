// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
#include "gow2_skip_cutscene.h"

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#include "gow2_dev_commands.h"
#include "gow2_dev_guest_calls.h"
#include "gow2_engine_tick.h"
#include "gow2_guest.h"
#include "gow2_host_ui.h"
#include "gow2_skip_key.h"

REXCVAR_DEFINE_BOOL(gow2_skip_cutscene, true, "input",
                    "Press the skip key to skip the cutscene that is playing (ends the running "
                    "matinees while the player is in cinematic mode, and stops a video).");
REXCVAR_DEFINE_STRING(gow2_skip_cutscene_key, "9", "input",
                      "Key that skips a cutscene: a digit or letter, or F1..F12. The digits also "
                      "work on the keypad.");

namespace rex::glue {

namespace {

constexpr uint32_t kGNames = 0x83136834;
constexpr uint32_t kGObjObjects = 0x83136860;
constexpr uint32_t kObjNameIndex = 44;
constexpr uint32_t kObjNameNumber = 48;
constexpr uint32_t kObjClass = 52;
constexpr uint32_t kNameEntryText = 8;
constexpr uint32_t kNameWideBit = 1;
constexpr uint32_t kMaxObjects = 1u << 22;
constexpr uint32_t kMaxNameLength = 128;
// SeqAct_Interp (Engine): Position, the flag word with bIsPlaying/bLooping, InterpData. The offsets are
// tools/ue3/layout.py's minus 4: that tool counts sizeof(UObject) as 60 and it is 56 (Class sits at 52).
constexpr uint32_t kInterpPosition = 272;
constexpr uint32_t kInterpFlags = 280;
constexpr uint32_t kInterpPlayingMask = 0x80000000u;
constexpr uint32_t kInterpLoopingMask = 0x10000000u;
constexpr uint32_t kInterpData = 296;
constexpr uint32_t kInterpDataLength = 144;  // InterpData.InterpLength
// PlayerController cinematic flags (bCinematicMode), as the co-op scene probe reads them.
constexpr uint32_t kPcCinematicFlags = 864;
constexpr uint32_t kCinematicModeMask = 0x00002000;

std::atomic<bool> g_installed{false};
std::atomic<bool> g_requested{false};

class SkipKeyListener : public rex::ui::WindowInputListener {
 public:
  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (e.prev_state()) return;  // the OS repeating a held key
    if (!REXCVAR_GET(gow2_skip_cutscene) || HostUiOpen()) return;
    static unsigned vk = 0;
    static std::string last;
    const std::string now = REXCVAR_GET(gow2_skip_cutscene_key);
    if (now != last) {
      last = now;
      vk = SkipKeyFromName(now.c_str());
    }
    if (SkipKeyMatches(vk, unsigned(e.virtual_key()))) {
      REXLOG_INFO("SKIPCUTSCENE key pressed");
      g_requested.store(true, std::memory_order_release);
    }
  }
};

SkipKeyListener g_listener;

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

bool IsDefaultObject(const uint8_t* base, uint32_t object) {
  return NameText(base, guest::LoadU32(base, object + kObjNameIndex)).rfind("Default__", 0) == 0;
}

float LoadFloat(const uint8_t* base, uint32_t addr) {
  const uint32_t bits = guest::LoadU32(base, addr);
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

void PushBE32(std::vector<uint8_t>* v, uint32_t x) {
  for (int s = 24; s >= 0; s -= 8) v->push_back(uint8_t(x >> s));
}

uint32_t FloatBits(float f) {
  uint32_t b;
  std::memcpy(&b, &f, 4);
  return b;
}

struct Playing {
  uint32_t object;
  float length;
};

// What the walk saw, for the log: why a skip found nothing.
struct Census {
  int matinees = 0, playing = 0, looping = 0, no_data = 0, finished = 0;
  float longest_position = 0;
};

// The non-looping matinees that are running now, with their lengths. The table can change under
// the reader, so this runs guarded.
void CollectPlaying(const uint8_t* base, std::vector<Playing>* out, Census* census) {
  const uint32_t objects = guest::LoadU32(base, kGObjObjects);
  const uint32_t count = guest::LoadU32(base, kGObjObjects + 4);
  if (objects == 0 || count > kMaxObjects) return;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t object = guest::LoadU32(base, objects + 4 * i);
    if (object == 0) continue;
    const std::string cls = ClassNameOf(base, object);
    if (cls != "SeqAct_Interp") continue;
    if (IsDefaultObject(base, object)) continue;
    ++census->matinees;
    const uint32_t flags = guest::LoadU32(base, object + kInterpFlags);
    if (!(flags & kInterpPlayingMask)) continue;
    ++census->playing;
    if (flags & kInterpLoopingMask) {
      ++census->looping;
      continue;
    }
    const uint32_t data = guest::LoadU32(base, object + kInterpData);
    if (data == 0) {
      ++census->no_data;
      continue;
    }
    const float length = LoadFloat(base, data + kInterpDataLength);
    const float position = LoadFloat(base, object + kInterpPosition);
    if (position > census->longest_position) census->longest_position = position;
    if (!(length > 0.0f) || !(position < length - 0.05f)) {
      ++census->finished;
      continue;
    }
    out->push_back({object, length});
  }
}

bool InCinematicMode(const uint8_t* base, uint32_t pc) {
  return pc != 0 && (guest::LoadU32(base, pc + kPcCinematicFlags) & kCinematicModeMask) != 0;
}

std::string Skip(const PPCContext& ctx, uint8_t* base) {
  const uint32_t engine = ctx.r3.u32;
  uint32_t pc = 0;
  bool cinematic = false;
  std::vector<Playing> playing;
  Census census;
  auto read = [&] {
    pc = dev_commands::LocalPlayerController(base, dev_commands::kGameGlobals, engine);
    cinematic = InCinematicMode(base, pc);
    if (cinematic) CollectPlaying(base, &playing, &census);
  };
  if (!guest_call::Guarded(read)) {
    REXLOG_WARN("SKIPCUTSCENE fault while reading the world");
    return "SKIPCUTSCENE fault while reading the world";
  }
  int skipped = 0;
  if (cinematic) {
    const std::optional<uint64_t> set_position = guest_call::MakeName(ctx, base, "SetPosition", false);
    for (const Playing& p : playing) {
      if (!set_position) break;
      const uint32_t fn = guest_call::FindFunction(ctx, base, p.object, *set_position);
      if (fn == 0) continue;
      std::vector<uint8_t> parms;  // float NewPosition, BOOL bJump
      PushBE32(&parms, FloatBits(p.length));
      PushBE32(&parms, 1);
      if (guest_call::ProcessEventWithParms(ctx, base, p.object, fn, parms, nullptr, 0)) ++skipped;
    }
  }
  // A video cutscene: GearPC.ClientStopMovie(0, false, bForceStopNonSkippable, false).
  bool movie = false;
  if (pc != 0) {
    if (const std::optional<uint64_t> stop = guest_call::MakeName(ctx, base, "ClientStopMovie", false)) {
      const uint32_t fn = guest_call::FindFunction(ctx, base, pc, *stop);
      if (fn != 0) {
        std::vector<uint8_t> parms;
        PushBE32(&parms, FloatBits(0.0f));
        PushBE32(&parms, 0);
        PushBE32(&parms, 1);
        PushBE32(&parms, 0);
        movie = guest_call::ProcessEventWithParms(ctx, base, pc, fn, parms, nullptr, 0);
      }
    }
  }
  // Whatever movie the engine itself is playing: the static Engine.StopMovie(bDelayStopUntilGameHasRendered).
  bool engine_stop = false;
  if (const std::optional<uint64_t> stop = guest_call::MakeName(ctx, base, "StopMovie", false)) {
    const uint32_t fn = guest_call::FindFunction(ctx, base, engine, *stop);
    if (fn != 0) {
      std::vector<uint8_t> parms;
      PushBE32(&parms, 0);
      engine_stop = guest_call::ProcessEventWithParms(ctx, base, engine, fn, parms, nullptr, 0);
    }
  }
  const std::string summary =
      "SKIPCUTSCENE cinematic=" + std::string(cinematic ? "true" : "false") +
      " matinees_ended=" + std::to_string(skipped) + " of " + std::to_string(playing.size()) +
      " movie_stop_sent=" + (movie ? "true" : "false") + " engine_stop=" + (engine_stop ? "true" : "false") +
      " (matinees seen " + std::to_string(census.matinees) + ", playing " + std::to_string(census.playing) +
      ", looping " + std::to_string(census.looping) + ", no data " + std::to_string(census.no_data) +
      ", finished " + std::to_string(census.finished) + ")";
  REXLOG_INFO("{}", summary);
  return summary;
}

void Tick(const PPCContext& ctx, uint8_t* base) {
  if (!g_installed.load(std::memory_order_acquire)) return;
  if (!g_requested.exchange(false, std::memory_order_acq_rel)) return;
  Skip(ctx, base);
}

[[maybe_unused]] const bool kRegistered = (AddEngineTickListener(&Tick), true);

}  // namespace

std::string SkipCutsceneNow(const PPCContext& ctx, uint8_t* base) { return Skip(ctx, base); }

void InstallSkipCutscene(rex::ui::Window* window) {
  if (!window) return;
  if (g_installed.exchange(true, std::memory_order_acq_rel)) return;
  window->AddInputListener(&g_listener, 3);
  REXLOG_INFO("SKIPCUTSCENE installed: key '{}' (gow2_skip_cutscene_key)", REXCVAR_GET(gow2_skip_cutscene_key));
}

}  // namespace rex::glue
