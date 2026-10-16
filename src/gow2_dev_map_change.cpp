// gow2 - ReXGlue Recompiled Project
//
// Run the Prepare/Commit map change natives without the Kismet chain.
#include "gow2_dev_map_change.h"

#include "gow2_dev_commands.h"
#include "gow2_dev_guest_calls.h"
#include "gow2_guest.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rex/logging.h>
#include <rex/ppc.h>

namespace rex::glue::dev_verbs {
namespace {

constexpr uint32_t kPrepareActivated = 0x8256C2A8;
constexpr uint32_t kCommitActivated = 0x8256C870;
constexpr uint32_t kClientPrepareEvent = 0x8256FE10;
constexpr uint32_t kVtableSlots = 256;

struct PrepareAction {
  uint32_t object = 0;
  std::string path;
  std::string main_level;
  int secondary = 0;
  bool high_priority = false;
  bool activator = false;
};

PrepareAction DescribePrepareAction(const uint8_t* base, uint32_t object) {
  PrepareAction action;
  action.object = object;
  action.path = dev_commands::ObjectPath(base, dev_commands::kGameGlobals, object);
  const uint32_t index =
      guest::LoadU32(base, object + dev_commands::layout::kPrepareMainLevel);
  const uint32_t number =
      guest::LoadU32(base, object + dev_commands::layout::kPrepareMainLevel + 4);
  action.main_level = dev_commands::NameText(base, dev_commands::kGameGlobals, index, number);
  action.secondary =
      int(guest::LoadU32(base, object + dev_commands::layout::kPrepareSecondaryNum));
  const uint32_t flags = guest::LoadU32(base, object + dev_commands::layout::kPrepareFlags);
  // bit 31 carries bIsHighPriority in the retail action layout.
  action.high_priority = (flags & 0x80000000u) != 0;
  action.activator =
      dev_commands::VtableHasFunction(base, object, kPrepareActivated, kVtableSlots);
  return action;
}

std::vector<PrepareAction> ReadPrepareActions(const uint8_t* base) {
  std::vector<PrepareAction> out;
  const std::vector<uint32_t> objects = dev_commands::ObjectsOfClass(
      base, dev_commands::kGameGlobals, "SeqAct_PrepareMapChange");
  for (uint32_t object : objects) out.push_back(DescribePrepareAction(base, object));
  return out;
}

uint32_t FindCommitAction(const uint8_t* base) {
  const std::vector<uint32_t> objects = dev_commands::ObjectsOfClass(
      base, dev_commands::kGameGlobals, "SeqAct_CommitMapChange");
  for (uint32_t object : objects) {
    if (dev_commands::VtableHasFunction(base, object, kCommitActivated, kVtableSlots))
      return object;
  }
  return 0;
}

size_t CountCommitActions(const uint8_t* base) {
  return dev_commands::ObjectsOfClass(base, dev_commands::kGameGlobals,
                                      "SeqAct_CommitMapChange")
      .size();
}

bool MapChangePending(const uint8_t* base, uint32_t engine) {
  const int32_t pending =
      int32_t(guest::LoadU32(base, engine + dev_commands::layout::kEnginePendingLevels));
  return pending > 0;
}

char FoldAscii(char c) {
  if (c >= 'A' && c <= 'Z') return char(c + ('a' - 'A'));
  return c;
}

bool LevelsEqualFold(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (FoldAscii(a[i]) != FoldAscii(b[i])) return false;
  }
  return true;
}

std::vector<PrepareAction> SelectCandidates(const std::vector<PrepareAction>& actions,
                                            std::string_view level) {
  std::vector<PrepareAction> out;
  for (const PrepareAction& action : actions) {
    if (!action.activator) continue;
    if (!level.empty() && !LevelsEqualFold(action.main_level, level)) continue;
    out.push_back(action);
  }
  return out;
}

std::string DescribeActivated(const PrepareAction& action) {
  return "activated " + action.path + " (main " + action.main_level + ", " +
         std::to_string(action.secondary) + " secondary)";
}

VerbResult ActivatePrepare(const PPCContext& ctx, uint8_t* base,
                            const PrepareAction& action) {
  if (!guest_call::Invoke(ctx, base, kPrepareActivated, {action.object}))
    return Fail("guest call failed");
  return Ok(DescribeActivated(action));
}

VerbResult ActivateSingleOrAsk(const PPCContext& ctx, uint8_t* base,
                               const std::vector<PrepareAction>& actions,
                               const std::vector<PrepareAction>& candidates) {
  if (candidates.size() != 1)
    return Fail(std::to_string(actions.size()) +
                " prepare actions loaded; name one (see !listmapchanges)");
  return ActivatePrepare(ctx, base, candidates[0]);
}

VerbResult BroadcastPrepare(const PPCContext& ctx, uint8_t* base, uint32_t world_info,
                            const std::string& level) {
  const std::optional<uint64_t> name = guest_call::MakeName(ctx, base, level, true);
  if (!name) return Fail("cannot make the name " + level);
  std::vector<uint32_t> pcs;
  auto reader = [&] {
    pcs = dev_commands::PlayerControllers(base, dev_commands::kGameGlobals, world_info);
  };
  if (!guest_call::Guarded(reader)) return Fail("fault while reading guest objects");
  for (uint32_t pc : pcs) guest_call::Invoke(ctx, base, kClientPrepareEvent, {pc, *name, 1, 1});
  return Ok("no Kismet action for " + level + "; ClientPrepareMapChange sent to " +
            std::to_string(pcs.size()) + " player controllers");
}

std::optional<VerbResult> FailUnlessPreparing(const uint8_t* base, uint32_t engine) {
  bool pending = false;
  auto reader = [&] { pending = MapChangePending(base, engine); };
  if (!guest_call::Guarded(reader)) return Fail("fault while reading guest objects");
  if (!pending) return Fail("no map change is being prepared (run !mapchange first)");
  return std::nullopt;
}

}  // namespace

VerbResult ListMapChanges(const PPCContext&, uint8_t* base, uint32_t,
                          const dev_commands::Command&) {
  std::vector<PrepareAction> actions;
  size_t commits = 0;
  auto reader = [&] {
    actions = ReadPrepareActions(base);
    commits = CountCommitActions(base);
  };
  if (!guest_call::Guarded(reader)) return Fail("fault while reading guest objects");
  for (const PrepareAction& action : actions)
    REXLOG_INFO("DEVCMD   prepare {} main={} secondary={} highprio={} activator={}",
                action.path, action.main_level, action.secondary,
                action.high_priority ? 1 : 0, action.activator ? 1 : 0);
  return Ok(std::to_string(actions.size()) + " prepare actions, " +
            std::to_string(commits) + " commit actions");
}

VerbResult MapChange(const PPCContext& ctx, uint8_t* base, uint32_t engine,
                     const dev_commands::Command& command) {
  const uint32_t world_info = GuardedWorldInfo(base);
  if (!IsHost(base, world_info)) return Fail("not the host");
  bool pending = false;
  auto pending_reader = [&] { pending = MapChangePending(base, engine); };
  if (!guest_call::Guarded(pending_reader)) return Fail("fault while reading guest objects");
  if (pending) return Fail("a map change is already pending");
  std::vector<PrepareAction> actions;
  auto actions_reader = [&] { actions = ReadPrepareActions(base); };
  if (!guest_call::Guarded(actions_reader)) return Fail("fault while reading guest objects");
  const std::string level = command.args.empty() ? "" : command.args[0];
  const std::vector<PrepareAction> candidates = SelectCandidates(actions, level);
  if (level.empty()) return ActivateSingleOrAsk(ctx, base, actions, candidates);
  if (!candidates.empty()) return ActivatePrepare(ctx, base, candidates[0]);
  return BroadcastPrepare(ctx, base, world_info, level);
}

VerbResult MapCommit(const PPCContext& ctx, uint8_t* base, uint32_t engine,
                     const dev_commands::Command& command) {
  const uint32_t world_info = GuardedWorldInfo(base);
  if (!IsHost(base, world_info)) return Fail("not the host");
  const bool force = !command.args.empty() && command.args[0] == "force";
  if (!force) {
    if (auto failure = FailUnlessPreparing(base, engine)) return *failure;
  }
  uint32_t commit = 0;
  auto commit_reader = [&] { commit = FindCommitAction(base); };
  if (!guest_call::Guarded(commit_reader)) return Fail("fault while reading guest objects");
  // retail Commit never reads r3, so a 0 object still broadcasts to controllers.
  if (!guest_call::Invoke(ctx, base, kCommitActivated, {commit}))
    return Fail("guest call failed");
  size_t count = 0;
  auto pcs_reader = [&] {
    count =
        dev_commands::PlayerControllers(base, dev_commands::kGameGlobals, world_info).size();
  };
  if (!guest_call::Guarded(pcs_reader)) return Fail("fault while reading guest objects");
  std::string detail =
      "ClientCommitMapChange sent to " + std::to_string(count) + " player controllers";
  if (commit == 0) detail += " (no SeqAct_CommitMapChange object loaded)";
  return Ok(detail);
}

}  // namespace rex::glue::dev_verbs
