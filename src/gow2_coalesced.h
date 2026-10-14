// Adapted from gears-judgement-recomp (BSD-3-Clause).
// Coalesced tweak layer.
//
// The game keeps its input bindings and UI strings in the Coalesced_*.bin bundles inside
// GearGame\CookedXenon (an archive of ini files). The edits below are applied to a cached copy
// of the player's own files and the runtime is told to read the copy instead
// (GOW2_VFS_OVERRIDES). The game folder is never modified.
//
// Edits are grouped by purpose and each group is switched on by an option:
//   mouse-look   right-stick dead zone off for the mouse-fed look axes (cvar mnk_mouse)
//   key-prompts  menu prompts show keyboard labels ([Space], [LMB]...) (cvar gow2_prompts)
#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace rex::glue {

struct CoalescedOptions {
  bool mouse_look = false;
  // Key = a name in [ControllerFont] of GearGameUI ("Controller_A"), value = the label shown
  // ("[Space]"). Empty means "leave the pad glyphs alone".
  std::vector<std::pair<std::string, std::string>> prompt_labels;
};

struct CoalescedResult {
  std::string overrides;  // value for GOW2_VFS_OVERRIDES, empty when nothing was prepared
  std::string summary;    // one log line
};

CoalescedResult PrepareCoalescedOverrides(const std::filesystem::path& game_root,
                                          const std::filesystem::path& cache_dir,
                                          const CoalescedOptions& options);

// Reads mnk_mouse, gow2_prompts and the keybind_* labels from the live cvars.
CoalescedOptions CoalescedOptionsFromCvars();

}  // namespace rex::glue
