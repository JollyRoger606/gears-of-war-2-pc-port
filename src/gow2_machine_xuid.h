// gow2 - ReXGlue Recompiled Project
//
// The System Link XUID belongs to this PC. Patch 0010 keeps it in the user data folder, and
// gow2_save_dir can move that folder somewhere several PCs sync: the file then stays in this PC's
// default user folder and reaches the runtime as user_xuid.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace rex::glue {

// The name patch 0010 gives the per-install XUID file (system_link::kInstallXuidFileName).
inline constexpr const char* kMachineXuidFileName = "system_link_xuid.txt";

struct MachineXuid {
  std::string xuid;     // 16 upper-case hex digits
  bool created = false; // no usable file was there; this run made the XUID
  bool saved = true;    // false: the new XUID could not be written and lasts this run only
};

bool IsMachineXuidFile(const std::filesystem::path& entry);
bool ShouldPinMachineXuid(bool user_root_moved, bool link_active, bool xuid_set);
std::optional<std::string> NormalizeXuid(const std::string& text);
std::string MakeMachineXuid(uint64_t random_bits);
std::optional<std::string> ReadMachineXuid(const std::filesystem::path& dir);
bool WriteMachineXuid(const std::filesystem::path& dir, const std::string& xuid);
MachineXuid LoadOrCreateMachineXuid(const std::filesystem::path& dir,
                                    const std::function<uint64_t()>& random_bits);

}  // namespace rex::glue
