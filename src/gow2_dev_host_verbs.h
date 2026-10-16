// gow2 - ReXGlue Recompiled Project
//
// The '!' verbs of gow2_dev_command_file (format in gow2_dev_commands.h): host-side actions that
// read guest objects or call script functions on the game thread. Dev only.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rex/ppc.h>

#include "gow2_dev_commands.h"

namespace rex::glue::dev_verbs {

struct VerbResult {
  bool ok = false;
  std::string detail;
};

VerbResult Ok(std::string detail);
VerbResult Fail(std::string detail);

// The server decides damage, checkpoints and map changes: Standalone, Dedicated or Listen.
bool IsHost(const uint8_t* base, uint32_t world_info);

// 0 when there is no world yet or reading it faulted.
uint32_t GuardedWorldInfo(const uint8_t* base);

// nullopt when reading the controllers faulted.
std::optional<std::vector<uint32_t>> GuardedTargets(const uint8_t* base, uint32_t engine,
                                                    uint32_t world_info,
                                                    const dev_commands::Target& target);

VerbResult RunHostVerb(const PPCContext& ctx, uint8_t* base, uint32_t engine,
                       const dev_commands::Command& command);

}  // namespace rex::glue::dev_verbs
