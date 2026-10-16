// gow2 - ReXGlue Recompiled Project
//
// The coop-scenes patch group of docs/COOP_STAGE2_SCENES.md (cinematics, teleports, checkpoints
// and co-op splits for players 3+). It registers itself; the hook applies it after Stage 1.

#include <optional>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "gow2_script_patch_groups.h"

REXCVAR_DEFINE_BOOL(gow2_coop_scenes, true, "Coop",
                    "Stage 2 scene patches for players 3+ (cinematics, teleports, checkpoints, "
                    "co-op splits); only with gow2_coop_players 3 or more");

namespace rex::glue::script_patch {

namespace {

#include "gow2_script_patch_scenes_rows.inc"

std::optional<PatchGroup> MakeCoopScenesGroup() {
  if (!REXCVAR_GET(gow2_coop_scenes)) {
    REXLOG_INFO("SCRIPTPATCH coop-scenes: off (gow2_coop_scenes=false)");
    return std::nullopt;
  }
  return CoopScenesGroup();
}

[[maybe_unused]] const bool kCoopScenesRegistered =
    RegisterCoopGroup("coop-scenes", &MakeCoopScenesGroup);

}  // namespace

}  // namespace rex::glue::script_patch
