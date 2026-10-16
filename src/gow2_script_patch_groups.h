// gow2 - ReXGlue Recompiled Project
//
// Self-registration of the extra co-op patch groups; gow2_script_patch_hook.cpp applies them by name after Stage 1.
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "gow2_script_patches.h"

namespace rex::glue::script_patch {

using CoopGroupFactory = std::optional<PatchGroup> (*)();

struct CoopGroupEntry {
  std::string_view name;
  CoopGroupFactory make;
};

// Both are final before the first engine tick: the startup hook sets them after FStartupPackages::LoadAll.
bool CoopStage1Applied();
uint64_t CoopStackHash();

inline std::vector<CoopGroupEntry>& CoopGroupEntries() {
  static std::vector<CoopGroupEntry> entries;
  return entries;
}

inline bool RegisterCoopGroup(std::string_view name, CoopGroupFactory make) {
  CoopGroupEntries().push_back({name, make});
  return true;
}

// Static initializers run in an unspecified order across translation units, so apply by name.
inline std::vector<CoopGroupEntry> CoopGroupsByName() {
  std::vector<CoopGroupEntry> sorted = CoopGroupEntries();
  std::sort(sorted.begin(), sorted.end(),
            [](const CoopGroupEntry& a, const CoopGroupEntry& b) { return a.name < b.name; });
  return sorted;
}

}  // namespace rex::glue::script_patch
