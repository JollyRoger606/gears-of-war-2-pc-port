// Adapted from gears-judgement-recomp (BSD-3-Clause).
// gow2 - ReXGlue Recompiled Project
//
// Serialization + instrumentation around the guest's XamInput* entry points.
// See gow2_xam_input_guard.cpp.

#pragma once

#include <functional>

namespace rex::glue {

void InstallXamInputGuard();
bool WithXamInputLock(const std::function<void()>& fn);
// Suppresses the system's startup "Disc Read Error" box (no disc drive is mounted by design).
void InstallDiscErrorSilencer();

}  // namespace rex::glue
