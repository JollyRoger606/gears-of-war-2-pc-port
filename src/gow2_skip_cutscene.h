// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Skip a cutscene with a key (default 9; gow2_skip_cutscene_key). The game's own CancelCine skips
// only matinees flagged skippable, which none of the retail cutscenes are, so this ends the running
// matinees itself (SeqAct_Interp.SetPosition to their end, the way CANCELMATINEE does) while the
// local player is in cinematic mode, and stops a playing video.
#pragma once

#include <cstdint>
#include <string>

#include <rex/ppc.h>

namespace rex::ui {
class Window;
}

namespace rex::glue {

void InstallSkipCutscene(rex::ui::Window* window);

// Runs the skip now, on the game thread (the engine tick and the "!skipcutscene" dev verb do);
// returns the summary that was logged.
std::string SkipCutsceneNow(const PPCContext& ctx, uint8_t* base);

}  // namespace rex::glue
