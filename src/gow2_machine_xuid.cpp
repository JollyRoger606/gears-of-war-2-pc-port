// gow2 - ReXGlue Recompiled Project
#include "gow2_machine_xuid.h"

#include <cctype>
#include <fstream>
#include <system_error>

namespace rex::glue {

namespace {

// The same rules as patch 0010's system_link::ParseXuid, MakeInstallXuid and FormatXuid.
constexpr uint64_t kForbiddenXuidBits = 0x00C0000000000000ull;
constexpr uint64_t kMachineXuidPrefix = 0xB13E000000000000ull;
constexpr uint64_t kMachineXuidRandomMask = 0x0000FFFFFFFFFFFFull;
constexpr uint64_t kMachineXuidFallbackLow = 1;
constexpr size_t kXuidHexDigits = 16;
constexpr const char* kUpperHex = "0123456789ABCDEF";

std::string TrimBlank(const std::string& text) {
  const char* blank = " \t\r\n\v\f";
  const size_t first = text.find_first_not_of(blank);
  if (first == std::string::npos) return {};
  const size_t last = text.find_last_not_of(blank);
  return text.substr(first, last - first + 1);
}

std::string WithoutHexPrefix(const std::string& text) {
  const bool has_prefix = text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
  return has_prefix ? text.substr(2) : text;
}

bool IsHexText(const std::string& text) {
  if (text.empty() || text.size() > kXuidHexDigits) return false;
  for (const char c : text) {
    if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

uint64_t HexValue(const std::string& text) {
  return std::stoull(text, nullptr, 16);
}

bool IsUsableXuid(uint64_t xuid) {
  return xuid != 0 && (xuid & kForbiddenXuidBits) == 0;
}

std::string FormatXuid(uint64_t xuid) {
  std::string out(kXuidHexDigits, '0');
  for (size_t i = 0; i < kXuidHexDigits; ++i) {
    out[kXuidHexDigits - 1 - i] = kUpperHex[(xuid >> (4 * i)) & 0xF];
  }
  return out;
}

std::filesystem::path MachineXuidPath(const std::filesystem::path& dir) {
  return dir / kMachineXuidFileName;
}

}  // namespace

bool IsMachineXuidFile(const std::filesystem::path& entry) {
  return entry.filename() == kMachineXuidFileName;
}

bool ShouldPinMachineXuid(bool user_root_moved, bool link_active, bool xuid_set) {
  return user_root_moved && link_active && !xuid_set;
}

std::optional<std::string> NormalizeXuid(const std::string& text) {
  const std::string digits = WithoutHexPrefix(TrimBlank(text));
  if (!IsHexText(digits)) return std::nullopt;
  const uint64_t xuid = HexValue(digits);
  if (!IsUsableXuid(xuid)) return std::nullopt;
  return FormatXuid(xuid);
}

std::string MakeMachineXuid(uint64_t random_bits) {
  uint64_t low = random_bits & kMachineXuidRandomMask;
  if (low == 0) low = kMachineXuidFallbackLow;
  return FormatXuid(kMachineXuidPrefix | low);
}

std::optional<std::string> ReadMachineXuid(const std::filesystem::path& dir) {
  std::ifstream in(MachineXuidPath(dir), std::ios::binary);
  if (!in) return std::nullopt;
  std::string line;
  std::getline(in, line);
  return NormalizeXuid(line);
}

bool WriteMachineXuid(const std::filesystem::path& dir, const std::string& xuid) {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::ofstream out(MachineXuidPath(dir), std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << xuid << '\n';
  out.close();
  return static_cast<bool>(out);
}

MachineXuid LoadOrCreateMachineXuid(const std::filesystem::path& dir,
                                    const std::function<uint64_t()>& random_bits) {
  if (std::optional<std::string> stored = ReadMachineXuid(dir)) return {*stored, false, true};
  const std::string created = MakeMachineXuid(random_bits());
  return {created, true, WriteMachineXuid(dir, created)};
}

}  // namespace rex::glue
