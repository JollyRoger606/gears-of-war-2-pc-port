// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
//
// Android counterparts of the Windows-only guards that gow2_app.h installs. The Windows versions
// rewrite x86 export thunks in rexruntime.dll or rely on SEH, so they are not built for Android:
//
// - XamInput serialization and the non-throwing UTF-8 decoder live in the SDK's Android patch
//   (sdk/patches/android: InputSystem holds one recursive lock across its entry points, and
//   utf8::next for string views decodes an invalid lead byte as one byte instead of throwing).
// - The disc-error box is replaced here: libgow2.so defines the runtime's own
//   __imp__XamShowDirtyDiscErrorUI, and the recompiled code and the function-mapping table bind
//   to this definition inside libgow2.so instead of the runtime's export.

#include "gow2_crash_watch.h"
#include "gow2_utf8_guard.h"
#include "gow2_xam_input_guard.h"

#include <dlfcn.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <cstring>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

namespace {

constexpr const char* kDiscErrorExport = "__imp__XamShowDirtyDiscErrorUI";

// GOW2_DISC_ERROR_UI=1 keeps the runtime's own box, as on Windows.
bool RuntimeDiscErrorUiWanted() {
  const char* gate = std::getenv("GOW2_DISC_ERROR_UI");
  return gate && std::strcmp(gate, "1") == 0;
}

PPCFunc* RuntimeDiscErrorUi() {
  void* runtime = dlopen("librexruntime.so", RTLD_NOW | RTLD_NOLOAD);
  if (!runtime) {
    return nullptr;
  }
  return reinterpret_cast<PPCFunc*>(dlsym(runtime, kDiscErrorExport));
}

void LogDiscErrorSuppressedOnce() {
  static std::atomic<bool> logged{false};
  if (!logged.exchange(true)) {
    REXLOG_INFO("DISCERR the game's 'Disc Read Error' box was suppressed (no disc drive is mounted by design)");
  }
}

// The guest treats this call as one that never returns (the runtime's version shows the box and
// exits the process), and the title shuts down if it does return, so the calling thread is parked:
// the state the game runs in while the box is up. Every other thread carries on.
[[noreturn]] void ParkCallingThread() {
  for (;;) {
    pause();
  }
}

}  // namespace

extern "C" REX_FUNC(__imp__XamShowDirtyDiscErrorUI) {
  if (RuntimeDiscErrorUiWanted()) {
    if (PPCFunc* original = RuntimeDiscErrorUi()) {
      original(ctx, base);
      return;
    }
  }
  LogDiscErrorSuppressedOnce();
  ParkCallingThread();
}

namespace rex::glue {

void InstallUtf8Guard() {
  REXLOG_INFO("UTF8GUARD on Android the runtime decodes invalid UTF-8 one byte at a time (SDK Android patch)");
}

void InstallXamInputGuard() {
  REXLOG_INFO("XAMINPUTGUARD on Android the runtime serializes its input system (SDK Android patch)");
}

void InstallDiscErrorSilencer() {
  if (RuntimeDiscErrorUiWanted()) {
    REXLOG_INFO("DISCERR GOW2_DISC_ERROR_UI=1: the runtime's disc error box is kept");
    return;
  }
  REXLOG_INFO("DISCERR disc error box silenced ({} defined in libgow2.so)", kDiscErrorExport);
}

void InstallCrashWatch() {
  REXLOG_INFO("CRASHWATCH not on Android: faults go to the runtime's signal handler");
}

}  // namespace rex::glue
