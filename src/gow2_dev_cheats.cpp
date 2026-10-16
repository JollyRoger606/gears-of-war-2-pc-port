// gow2 - ReXGlue Recompiled Project
//
// Dev-only cheat manager for the co-op host, applied once on the engine tick.
#include "gow2_dev_cheats.h"

#include "gow2_engine_tick.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdint>
#include <string>

REXCVAR_DEFINE_BOOL(gow2_dev_cheats, false, "Testing",
                    "Dev only: the co-op host gets a cheat manager (GameInfo.AllowCheats is also true on a listen server), so God, Teleport, DBNOMe, PrepMap and other cheats work from gow2_dev_command_file. Off = retail; needs a restart");

namespace rex::glue {
namespace {

using script_patch::GroupResult;
using script_patch::PatchGroup;

constexpr uint32_t kGNames = 0x83136834u;
constexpr uint32_t kGObjObjects = 0x83136860u;

struct ApplyCall {
  uint8_t* base;
  const PatchGroup* group;
  const script_patch::GuestAlloc* alloc;
  GroupResult* result;
};

void RunApply(ApplyCall* call) {
  *call->result = script_patch::ApplyGroup(call->base, {kGNames, kGObjObjects}, *call->group,
                                           0, *call->alloc);
}

bool GuardedApply(ApplyCall* call) {
#ifdef _WIN32
  __try {
    RunApply(call);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
#else
  RunApply(call);
  return true;
#endif
}

void LogResult(const GroupResult& result) {
  if (!result.applied) {
    REXLOG_WARN("SCRIPTPATCH devcheats: skipped ({}: {}); cheats stay off", result.failed_row,
                result.reason);
    return;
  }
  for (const std::string& line : result.lines) REXLOG_INFO("SCRIPTPATCH devcheats {}", line);
  REXLOG_INFO("SCRIPTPATCH devcheats: {} rows applied, set hash {:016x}", result.lines.size(),
              result.set_hash);
}

void ApplyDevCheatsOnce(const PPCContext&, uint8_t* base) {
  static bool done = false;
  if (done) return;
  done = true;
  if (!REXCVAR_GET(gow2_dev_cheats)) return;
  PatchGroup group = script_patch::DevCheatsGroup();
  // the row never grows
  const script_patch::GuestAlloc alloc = [](uint32_t) { return 0u; };
  GroupResult result;
  ApplyCall call{base, &group, &alloc, &result};
  if (!GuardedApply(&call)) {
    REXLOG_WARN("SCRIPTPATCH devcheats: skipped (fault while reading guest objects); cheats stay off");
    return;
  }
  LogResult(result);
}

[[maybe_unused]] const bool kRegistered =
    (rex::glue::AddEngineTickListener(&ApplyDevCheatsOnce), true);

}  // namespace
}  // namespace rex::glue
