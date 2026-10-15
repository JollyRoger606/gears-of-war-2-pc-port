// gow2 - ReXGlue Recompiled Project
//
// Save folder override: gow2_save_dir moves saves, profile settings and achievement
// unlocks to any folder; empty keeps the default.
#pragma once

#include <filesystem>
#include <string>

namespace rex::glue {

struct SaveDirChoice {
  std::filesystem::path user_data_root;  // what the runtime will use
  std::string summary;                   // one log line; empty when the setting is unset
  bool is_warning = false;               // the setting could not be honoured; the default is kept
};
std::filesystem::path ResolveSaveDirSetting(const std::string& setting, const std::filesystem::path& base_dir);
SaveDirChoice ChooseUserDataRoot(const std::string& setting, const std::filesystem::path& default_root,
                                 const std::filesystem::path& cache_root, const std::filesystem::path& base_dir);
std::string SaveDirSetting();  // current value of the gow2_save_dir cvar

}  // namespace rex::glue
