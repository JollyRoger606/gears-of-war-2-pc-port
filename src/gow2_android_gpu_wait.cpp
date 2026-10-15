// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors

#if defined(__ANDROID__)

#include <sched.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/ppc.h>

REXCVAR_DEFINE_BOOL(gow2_android_gpu_wait_yield, true, "GameFixes",
                    "Android: give the CPU core away once per poll of the game's wait for the GPU "
                    "(sub_82977688), which otherwise spins at full speed")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_82977688);

// GPU ring wait poll: yielding only adds host time; the guest's own timeout uses the time base.
REX_HOOK_RAW(sub_82977688) {
  __imp__sub_82977688(ctx, base);
  if (REXCVAR_GET(gow2_android_gpu_wait_yield)) sched_yield();
}

#endif
