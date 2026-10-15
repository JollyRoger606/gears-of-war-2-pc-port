// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors

#include "gow2_frame_stats.h"

#include "gow2_android_thermal.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>

REXCVAR_DEFINE_BOOL(gow2_frame_log, false, "debug",
                    "Log the game's frame rate every five seconds (FRAMES lines), counted at the "
                    "player's input step, which runs once per game frame in gameplay");

namespace rex::glue {

namespace {

// PlayerInput's ProcessInput and its vtable slot, the same pair src/gow2_mouse_aim.cpp hooks.
constexpr uint32_t kProcessInputFn = 0x827FC468;
constexpr uint32_t kProcessInputSlot = 0x821763D0;
constexpr int64_t kWindowUs = 5'000'000;
constexpr double kUsPerMs = 1000.0;
constexpr double kUsPerSecond = 1'000'000.0;

PPCFunc* g_process_input = nullptr;

int64_t NowUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

uint32_t LoadGuestU32(const uint8_t* base, uint32_t addr) {
  const uint8_t* p = base + addr;
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

struct FrameWindow {
  int64_t start_us = 0;
  int64_t last_us = 0;
  int64_t longest_us = 0;
  uint32_t frames = 0;
};

// The input step moves between guest threads across level loads.
std::mutex g_window_mutex;
FrameWindow g_window;

void LogAndRestart(int64_t now) {
  const double seconds = static_cast<double>(now - g_window.start_us) / kUsPerSecond;
  REXLOG_INFO("FRAMES {:.1f} s: {} frames, {:.1f} fps, longest frame {:.1f} ms, {}", seconds,
              g_window.frames, g_window.frames / seconds,
              static_cast<double>(g_window.longest_us) / kUsPerMs, DescribeGpuAndThermalState());
  g_window = FrameWindow{now, now, 0, 0};
}

void CountFrame() {
  const int64_t now = NowUs();
  std::lock_guard<std::mutex> lock(g_window_mutex);
  if (g_window.start_us == 0) {
    g_window = FrameWindow{now, now, 0, 0};
    return;
  }
  g_window.longest_us = std::max(g_window.longest_us, now - g_window.last_us);
  g_window.last_us = now;
  ++g_window.frames;
  if (now - g_window.start_us >= kWindowUs) LogAndRestart(now);
}

void ProcessInputHook(PPCContext& ctx, uint8_t* base) {
  CountFrame();
  g_process_input(ctx, base);
}

}  // namespace

void InstallFrameStats() {
  if (!REXCVAR_GET(gow2_frame_log)) return;
  rex::Runtime* runtime = rex::Runtime::instance();
  rex::runtime::FunctionDispatcher* dispatcher =
      runtime ? runtime->function_dispatcher() : nullptr;
  uint8_t* base = (runtime && runtime->memory()) ? runtime->memory()->virtual_membase() : nullptr;
  if (!dispatcher || !base || LoadGuestU32(base, kProcessInputSlot) != kProcessInputFn) {
    REXLOG_WARN("FRAMES frame log off: the input step was not found");
    return;
  }
  g_process_input = dispatcher->GetFunction(kProcessInputFn);
  if (!g_process_input || !dispatcher->SetFunction(kProcessInputFn, &ProcessInputHook)) {
    REXLOG_WARN("FRAMES frame log off: the input step could not be hooked");
    return;
  }
  REXLOG_INFO("FRAMES frame log on (every {} s)", kWindowUs / 1'000'000);
}

}  // namespace rex::glue
