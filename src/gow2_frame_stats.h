// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
//
// The game's own frame rate: with gow2_frame_log on, the player's input step (once per game frame
// in gameplay) is counted and statistics are logged every five seconds as a FRAMESTATS line. Presented
// frames are no measure on a phone: while an ImGui overlay is up the presenter repaints at the
// display's refresh rate.
#pragma once

namespace rex::glue {

void InstallFrameStats();

}  // namespace rex::glue
