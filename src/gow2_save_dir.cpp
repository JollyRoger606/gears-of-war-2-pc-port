// gow2 - ReXGlue Recompiled Project
#include "gow2_save_dir.h"

#include "gow2_machine_xuid.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace rex::glue {

namespace {

std::string Trim(const std::string& text) {
  const char* blank = " \t\r\n";
  const size_t first = text.find_first_not_of(blank);
  if (first == std::string::npos) return {};
  const size_t last = text.find_last_not_of(blank);
  std::string out = text.substr(first, last - first + 1);
  if (out.size() >= 2 && out.front() == '"' && out.back() == '"') out = out.substr(1, out.size() - 2);
  return out;
}

#ifdef _WIN32
bool Utf8ToWide(const std::string& text, std::wstring& out) {
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  if (size <= 0) return false;
  std::wstring wide(static_cast<size_t>(size - 1), L'\0');
  if (!MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), size)) return false;
  out = wide;
  return true;
}

bool WideToUtf8(const std::wstring& wide, std::string& out) {
  const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (size <= 0) return false;
  std::string narrow(static_cast<size_t>(size - 1), '\0');
  if (!WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, narrow.data(), size, nullptr, nullptr)) {
    return false;
  }
  out = narrow;
  return true;
}

bool ExpandWide(const std::wstring& in, std::wstring& out) {
  const DWORD size = ExpandEnvironmentStringsW(in.c_str(), nullptr, 0);
  if (size == 0) return false;
  std::wstring expanded(static_cast<size_t>(size - 1), L'\0');
  const DWORD done = ExpandEnvironmentStringsW(in.c_str(), expanded.data(), size);
  if (done == 0 || done > size) return false;
  out = expanded;
  return true;
}
#endif

std::string ExpandVariables(const std::string& text) {
#ifdef _WIN32
  if (text.empty()) return text;
  std::wstring wide;
  if (!Utf8ToWide(text, wide)) return text;
  std::wstring expanded;
  if (!ExpandWide(wide, expanded)) return text;
  std::string out;
  if (!WideToUtf8(expanded, out)) return text;
  return out;
#else
  return text;
#endif
}

std::filesystem::path Utf8ToPath(const std::string& text) {
  return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::string PathText(const std::filesystem::path& path) {
  const std::u8string u8 = path.u8string();
  return std::string(u8.begin(), u8.end());
}

#ifdef _WIN32
std::wstring LowerWide(std::wstring text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](wchar_t c) { return std::towlower(c); });
  return text;
}
#endif

bool IsSameFolder(const std::filesystem::path& a, const std::filesystem::path& b) {
  std::error_code ec_a;
  std::error_code ec_b;
  const bool a_exists = std::filesystem::exists(a, ec_a);
  const bool b_exists = std::filesystem::exists(b, ec_b);
  if (a_exists && b_exists) {
    std::error_code ec;
    const bool same = std::filesystem::equivalent(a, b, ec);
    if (ec) return false;
    return same;
  }
#ifdef _WIN32
  return LowerWide(a.lexically_normal().wstring()) == LowerWide(b.lexically_normal().wstring());
#else
  return a.lexically_normal() == b.lexically_normal();
#endif
}

bool ProbeWritable(const std::filesystem::path& dir) {
  const std::filesystem::path probe = dir / ".gow2-write-test";
  std::ofstream out(probe, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.put('x');
  out.close();
  const bool ok = static_cast<bool>(out);
  std::error_code ec;
  std::filesystem::remove(probe, ec);
  return ok;
}

bool EnsureWritableFolder(const std::filesystem::path& dir, std::string& reason) {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    reason = ec.message();
    return false;
  }
  if (!std::filesystem::is_directory(dir, ec)) {
    reason = "not a folder";
    return false;
  }
  if (!ProbeWritable(dir)) {
    reason = "cannot write there";
    return false;
  }
  return true;
}

bool HasAnyEntry(const std::filesystem::path& dir) {
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) return false;
  std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec);
  if (ec) return false;
  return it != std::filesystem::directory_iterator();
}

bool CopyOneEntry(const std::filesystem::path& src, const std::filesystem::path& to,
                  const std::filesystem::path& skip, std::string& reason) {
  if (IsSameFolder(src, skip)) return true;
  // The System Link XUID names this PC; a synced save folder would hand it to every PC.
  if (IsMachineXuidFile(src)) return true;
  std::error_code ec;
  std::filesystem::copy(src, to / src.filename(),
                        std::filesystem::copy_options::recursive |
                            std::filesystem::copy_options::skip_existing,
                        ec);
  if (ec) {
    reason = ec.message();
    return false;
  }
  return true;
}

bool CopyEntriesExcept(const std::filesystem::path& from, const std::filesystem::path& to,
                       const std::filesystem::path& skip, std::string& reason) {
  std::error_code ec;
  std::filesystem::directory_iterator it(from, std::filesystem::directory_options::skip_permission_denied, ec);
  if (ec) {
    reason = ec.message();
    return false;
  }
  const std::filesystem::directory_iterator end;
  for (; it != end; it.increment(ec)) {
    if (ec) break;
    if (!CopyOneEntry(it->path(), to, skip, reason)) return false;
  }
  if (ec) {
    reason = ec.message();
    return false;
  }
  return true;
}

}  // namespace

std::filesystem::path ResolveSaveDirSetting(const std::string& setting,
                                            const std::filesystem::path& base_dir) {
  const std::string trimmed = Trim(setting);
  if (trimmed.empty()) return {};
  const std::string expanded = ExpandVariables(trimmed);
  std::filesystem::path path = Utf8ToPath(expanded);
  if (path.is_relative()) path = base_dir / path;
  return path.lexically_normal();
}

SaveDirChoice ChooseUserDataRoot(const std::string& setting, const std::filesystem::path& default_root,
                                 const std::filesystem::path& cache_root, const std::filesystem::path& base_dir) {
  const std::filesystem::path chosen = ResolveSaveDirSetting(setting, base_dir);
  if (chosen.empty()) return {default_root, "", false};
  if (IsSameFolder(chosen, default_root)) {
    return {default_root, "gow2_save_dir is the default save folder: " + PathText(default_root), false};
  }
  std::string reason;
  if (!EnsureWritableFolder(chosen, reason)) {
    return {default_root, "gow2_save_dir \"" + setting + "\" cannot be used (" + reason + "); saves stay in " + PathText(default_root), true};
  }
  std::string prefix;
  if (!HasAnyEntry(chosen) && HasAnyEntry(default_root)) {
    if (!CopyEntriesExcept(default_root, chosen, cache_root, reason)) {
      return {default_root, "could not copy the existing saves from " + PathText(default_root) + " to " + PathText(chosen) + " (" + reason + "); saves stay in " + PathText(default_root), true};
    }
    prefix = "copied the existing saves from " + PathText(default_root) + "; ";
  }
  return {chosen, prefix + "saves, profile settings and achievements in " + PathText(chosen) + " (gow2_save_dir)", false};
}

}  // namespace rex::glue
