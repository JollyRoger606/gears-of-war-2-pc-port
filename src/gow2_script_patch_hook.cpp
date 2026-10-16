// gow2 - ReXGlue Recompiled Project
//
// Applies the script patch groups of gow2_script_patches.h right after the engine has loaded its
// native script packages, by replacing the recompiled FStartupPackages::LoadAll.

#include "gow2_script_patches.h"
#include "gow2_script_patch_groups.h"
#include "gow2_script_patch_survival.h"
#include "gow2_script_patch_body.h"
#include "gow2_coop_census.h"
#include "gow2_coop_network.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdint>
#include <optional>
#include <string_view>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/xmemory.h>

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
REXCVAR_DEFINE_BOOL(gow2_coop_survival, true, "Coop",
                    "With gow2_coop_players 3+: players 3+ keep their pawn through chapter travel, "
                    "respawn near a teammate instead of ending the game, leave without an AI Dom "
                    "taking their body, and do not block checkpoints while dead (Stage 2 group A, "
                    "docs/COOP_STAGE2_SURVIVAL.md). false keeps Stage 1 only");
REXCVAR_DEFINE_BOOL(gow2_coop_split_revive, false, "Coop",
                    "Debug, row S2A-4: in co-op splits a standing player 3+ of the downed "
                    "player's squad counts as a reviver");
REXCVAR_DEFINE_BOOL(gow2_coop_extra_carmine, false, "Coop",
                    "Players 3+ are Ben Carmine where his class is loaded (chapters 2, 5-11) and "
                    "GearPawn_COGGear elsewhere; off makes them always GearPawn_COGGear with Dom's "
                    "body, which exists in every chapter on every peer");
REXCVAR_DEFINE_BOOL(gow2_coop_vehicle_ride, true, "Coop",
                    "While Marcus or Dom rides a vehicle, players 3+ on foot cannot be hurt, and they "
                    "are brought to him when he gets out (rows CR-1 and CR-2)");
REXCVAR_DEFINE_BOOL(gow2_coop_body, true, "Coop",
                    "Players 3+ get Dom's body on GearPawn_COGGear, the pistol and Gnasher at every "
                    "spawn, and spawn without Right Trigger (rows CB-1 to CB-5); off keeps the "
                    "Stage 1 behaviour");

namespace rex::glue {

static_assert(guest::PhysicalHostOffset(0xDFFFFFFFu) ==
                      rex::memory::detail::PhysicalHostOffset(0xDFFFFFFFu) &&
                  guest::PhysicalHostOffset(0xE0000000u) ==
                      rex::memory::detail::PhysicalHostOffset(0xE0000000u),
              "gow2_guest.h must place the 0xE0000000 heap where the runtime maps it");

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

bool ApplyPatchGroup(uint8_t* base, const PatchGroup& group, int players,
                     const script_patch::GuestAlloc& alloc, GroupResult* result) {
  ApplyCall call{base, &group, players, &alloc, result};
  return GuardedApply(&call);
}

void LogResult(std::string_view tag, std::string_view when_skipped, const GroupResult& result,
               int players) {
  if (!result.applied) {
    REXLOG_WARN("SCRIPTPATCH {}: skipped ({}: {}); {}", tag, result.failed_row, result.reason,
                when_skipped);
    return;
  }
  for (const std::string& line : result.lines) REXLOG_INFO("SCRIPTPATCH {} {}", tag, line);
  REXLOG_INFO("SCRIPTPATCH {}: {} rows applied for {} players, set hash {:016x}", tag,
              result.lines.size(), players, result.set_hash);
}

bool ApplyAndLogGroup(uint8_t* base, const PatchGroup& group, int players,
                      const script_patch::GuestAlloc& alloc, std::string_view when_skipped) {
  GroupResult result;
  if (!ApplyPatchGroup(base, group, players, alloc, &result)) {
    REXLOG_WARN("SCRIPTPATCH {}: skipped (fault while reading guest objects); {}", group.name,
                when_skipped);
    return false;
  }
  LogResult(group.name, when_skipped, result, players);
  return result.applied;
}

void ApplySurvivalGroups(uint8_t* base, const script_patch::GuestAlloc& alloc, bool coop_applied,
                         int players) {
  if (!coop_applied) {
    REXLOG_INFO("SCRIPTPATCH survival: off (the coop group was not applied)");
    return;
  }
  if (!REXCVAR_GET(gow2_coop_survival)) {
    REXLOG_INFO("SCRIPTPATCH survival: off (gow2_coop_survival=false)");
    return;
  }
  const bool survival_applied = ApplyAndLogGroup(base, script_patch::CoopSurvivalGroup(), players,
                                                 alloc, "Stage 1 stays applied");
  if (survival_applied && REXCVAR_GET(gow2_coop_split_revive)) {
    ApplyAndLogGroup(base, script_patch::CoopSplitReviveGroup(), players, alloc,
                     "the survival group stays applied");
  }
}

void ApplyRegisteredGroup(const script_patch::CoopGroupEntry& entry, uint8_t* base,
                          const script_patch::GuestAlloc& alloc, bool coop_applied, int players) {
  if (!coop_applied) {
    REXLOG_INFO("SCRIPTPATCH {}: off (the coop group was not applied)", entry.name);
    return;
  }
  std::optional<PatchGroup> group = entry.make();
  if (group) ApplyAndLogGroup(base, *group, players, alloc, "the other groups stay applied");
}

void ApplyRegisteredGroups(uint8_t* base, const script_patch::GuestAlloc& alloc, bool coop_applied,
                           int players) {
  for (const script_patch::CoopGroupEntry& entry : script_patch::CoopGroupsByName()) {
    ApplyRegisteredGroup(entry, base, alloc, coop_applied, players);
  }
}

constexpr std::string_view kBodyFallback = "players 3+ keep the Stage 1 behaviour";
constexpr std::string_view kRideFallback = "vehicles stay retail";

void ApplyCoopBody(uint8_t* base, int players, const script_patch::GuestAlloc& alloc) {
  if (!REXCVAR_GET(gow2_coop_body)) {
    REXLOG_INFO("SCRIPTPATCH coop-body: off (gow2_coop_body=false)");
    return;
  }
  const PatchGroup body =
      script_patch::CoopBodyGroup(REXCVAR_GET(gow2_coop_extra_carmine),
                                  REXCVAR_GET(gow2_coop_marcus_clones));
  ApplyAndLogGroup(base, body, players, alloc, kBodyFallback);
}

void ApplyCoopFollowUps(uint8_t* base, bool coop_applied, int players,
                        const script_patch::GuestAlloc& alloc) {
  if (!coop_applied) {
    REXLOG_INFO("SCRIPTPATCH coop-body: off (the coop group was not applied)");
    REXLOG_INFO("SCRIPTPATCH coop-ride: off (the coop group was not applied)");
    return;
  }
  ApplyCoopBody(base, players, alloc);
  if (!REXCVAR_GET(gow2_coop_vehicle_ride)) {
    REXLOG_INFO("SCRIPTPATCH coop-ride: off (gow2_coop_vehicle_ride=false)");
    return;
  }
  ApplyAndLogGroup(base, script_patch::CoopRideGroup(), players, alloc, kRideFallback);
}

void ApplyStartupScriptPatches(PPCContext& ctx, uint8_t* base) {
  REXLOG_INFO("SCRIPTPATCH startup script packages loaded");
  StartCoopCensus(base);
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
  const bool coop_applied = ApplyAndLogGroup(base, group, players, alloc, "the game stays retail");
  ApplySurvivalGroups(base, alloc, coop_applied, players);
  ApplyCoopFollowUps(base, coop_applied, players, alloc);
  ApplyRegisteredGroups(base, alloc, coop_applied, players);
}

}  // namespace

void EnableSystemLinkForCoop() {
  const int players = REXCVAR_GET(gow2_coop_players);
  if (players < kFirstCoopCount) return;
  if (rex::cvar::GetFlagSource("net_link_active") != rex::cvar::Source::kDefault) return;
  if (rex::cvar::SetFlagByName("net_link_active", "true"))
    REXLOG_INFO("SCRIPTPATCH coop: gow2_coop_players={} turns System Link on (net_link_active=true)",
                players);
}

}  // namespace rex::glue

REX_EXTERN(__imp__sub_8283F780);

// FStartupPackages::LoadAll (one call, 0x8284075C): when it returns, Core, Engine, GameFramework,
// IpDrv, GearGame and OnlineSubsystemLive are loaded and no gameplay script has run.
REX_HOOK_RAW(sub_8283F780) {
  __imp__sub_8283F780(ctx, base);
  rex::glue::ApplyStartupScriptPatches(ctx, base);
}
