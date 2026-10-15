// gow2 - ReXGlue Recompiled Project
//
// Applies the script patch groups of gow2_script_patches.h right after the engine has loaded its
// native script packages, by replacing the recompiled FStartupPackages::LoadAll.

#include "gow2_script_patches.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>

#include "gow2_guest.h"

REXCVAR_DEFINE_INT32(gow2_coop_players, 2, "Coop",
                     "Campaign co-op player cap. 2 keeps the retail game; 3 to 255 apply the Stage 1 "
                     "script patches of docs/COOP_N_PLAYERS.md for that many players")
    .range(2, 255);
REXCVAR_DEFINE_BOOL(gow2_coop_marcus_clones, false, "Coop",
                    "Debug (row D-1): players 3+ spawn as Marcus clones instead of Ben Carmine or a "
                    "RedShirt. Breaks Kismet's hero lookups; for network bring-up only");
REXCVAR_DEFINE_BOOL(gow2_coop_patch_fail_test, false, "Coop",
                    "Debug: corrupt one expected byte of the co-op patch rows. The whole group must "
                    "then be skipped and the game stays retail");

namespace rex::glue {

namespace {

using script_patch::GroupResult;
using script_patch::PatchGroup;

constexpr uint32_t kGNames = 0x83136834;
constexpr uint32_t kGObjObjects = 0x83136860;
constexpr uint32_t kGMalloc = 0x8310B12C;
constexpr uint32_t kMallocSlot = 4;
// The alignment the engine passes when it allocates a package buffer (0x8226F610).
constexpr uint32_t kMallocAlign = 8;
constexpr int kFirstCoopCount = 3;
constexpr int kLastCoopCount = 255;

uint32_t GuestMalloc(PPCContext& ctx, uint8_t* base, uint32_t size) {
  rex::Runtime* runtime = rex::Runtime::instance();
  rex::runtime::FunctionDispatcher* dispatcher = runtime ? runtime->function_dispatcher() : nullptr;
  const uint32_t gmalloc = guest::LoadU32(base, kGMalloc);
  if (!dispatcher || gmalloc == 0) return 0;
  const uint32_t vtable = guest::LoadU32(base, gmalloc);
  if (vtable == 0) return 0;
  PPCFunc* malloc_fn = dispatcher->GetFunction(guest::LoadU32(base, vtable + kMallocSlot));
  if (!malloc_fn) return 0;
  PPCContext call = ctx;
  call.r3.u64 = gmalloc;
  call.r4.u64 = size;
  call.r5.u64 = kMallocAlign;
  malloc_fn(call, base);
  return call.r3.u32;
}

void CorruptFirstExpectedByte(PatchGroup* group) {
  if (group->scripts.empty() || group->scripts[0].checks.empty()) return;
  script_patch::BytesCheck& check = group->scripts[0].checks[0];
  if (check.bytes.empty()) return;
  check.bytes[0] = uint8_t(~check.bytes[0]);
  REXLOG_WARN("SCRIPTPATCH coop: fail test on, row {} expects {:02X} at +0x{:04X}",
              group->scripts[0].id, check.bytes[0], check.offset);
}

struct ApplyCall {
  uint8_t* base;
  const PatchGroup* group;
  int players;
  const script_patch::GuestAlloc* alloc;
  GroupResult* result;
};

void RunApply(ApplyCall* call) {
  *call->result = script_patch::ApplyGroup(call->base, {kGNames, kGObjObjects}, *call->group,
                                           call->players, *call->alloc);
}

// False when reading the guest tables faulted (a wrong layout), before anything was written.
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

void LogResult(const GroupResult& result, int players) {
  if (!result.applied) {
    REXLOG_WARN("SCRIPTPATCH coop: skipped ({}: {}); the game stays retail", result.failed_row,
                result.reason);
    return;
  }
  for (const std::string& line : result.lines) REXLOG_INFO("SCRIPTPATCH coop {}", line);
  REXLOG_INFO("SCRIPTPATCH coop: {} rows applied for {} players, set hash {:016x}",
              result.lines.size(), players, result.set_hash);
}

void ApplyStartupScriptPatches(PPCContext& ctx, uint8_t* base) {
  REXLOG_INFO("SCRIPTPATCH startup script packages loaded");
  const int players = REXCVAR_GET(gow2_coop_players);
  if (players < kFirstCoopCount || players > kLastCoopCount) {
    REXLOG_INFO("SCRIPTPATCH coop: off (gow2_coop_players={})", players);
    return;
  }
  PatchGroup group = script_patch::CoopStage1Group(REXCVAR_GET(gow2_coop_marcus_clones));
  if (REXCVAR_GET(gow2_coop_patch_fail_test)) CorruptFirstExpectedByte(&group);
  const script_patch::GuestAlloc alloc = [&ctx, base](uint32_t size) {
    return GuestMalloc(ctx, base, size);
  };
  GroupResult result;
  ApplyCall call{base, &group, players, &alloc, &result};
  if (!GuardedApply(&call)) {
    REXLOG_WARN("SCRIPTPATCH coop: skipped (fault while reading guest objects); the game stays retail");
    return;
  }
  LogResult(result, players);
}

}  // namespace

}  // namespace rex::glue

REX_EXTERN(__imp__sub_8283F780);

// FStartupPackages::LoadAll (one call, 0x8284075C): when it returns, Core, Engine, GameFramework,
// IpDrv, GearGame and OnlineSubsystemLive are loaded and no gameplay script has run.
REX_HOOK_RAW(sub_8283F780) {
  __imp__sub_8283F780(ctx, base);
  rex::glue::ApplyStartupScriptPatches(ctx, base);
}
