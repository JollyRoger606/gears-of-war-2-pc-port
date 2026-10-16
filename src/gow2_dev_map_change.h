// gow2 - ReXGlue Recompiled Project
//
// The map change verbs of gow2_dev_command_file: run the chapter-end Kismet natives
// (SeqAct_PrepareMapChange / SeqAct_CommitMapChange) on the host without the Kismet around them.
#pragma once

#include <cstdint>

#include <rex/ppc.h>

#include "gow2_dev_commands.h"
#include "gow2_dev_host_verbs.h"

namespace rex::glue::dev_verbs {

VerbResult ListMapChanges(const PPCContext& ctx, uint8_t* base, uint32_t engine,
                          const dev_commands::Command& command);
VerbResult MapChange(const PPCContext& ctx, uint8_t* base, uint32_t engine,
                     const dev_commands::Command& command);
VerbResult MapCommit(const PPCContext& ctx, uint8_t* base, uint32_t engine,
                     const dev_commands::Command& command);

}  // namespace rex::glue::dev_verbs
