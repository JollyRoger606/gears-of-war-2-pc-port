// Host-side test of the gow2_save_dir logic on temporary folders (no game data, no SDK).
// Build: cmake --build <dir> --target gow2_save_dir_test, then run the exe.
#include "gow2_save_dir.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

int g_checks = 0;
int g_failures = 0;
void Expect(bool ok, std::string_view what) { ++g_checks; if (ok) return; ++g_failures; std::printf("FAIL: %.*s\n", int(what.size()), what.data()); }

bool Contains(std::string_view haystack, std::string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}

std::string ToUtf8(const fs::path& path) {
  const std::u8string u8 = path.u8string();
  return std::string(u8.begin(), u8.end());
}

void CreateDirs(const fs::path& path) {
  std::error_code ec;
  fs::create_directories(path, ec);
}

void WriteFile(const fs::path& path, std::string_view text) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return;
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string ReadFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::string out;
  char c = '\0';
  while (in.get(c)) out.push_back(c);
  return out;
}

bool Exists(const fs::path& path) {
  std::error_code ec;
  return fs::exists(path, ec);
}

void TestEmptySettingResolvesEmpty(const fs::path& root) {
  const fs::path base = root / "case1";
  Expect(rex::glue::ResolveSaveDirSetting("", base).empty(), "empty setting resolves empty");
  Expect(rex::glue::ResolveSaveDirSetting("  \t ", base).empty(), "blank setting resolves empty");
}

void TestRelativeSetting(const fs::path& root) {
  const fs::path base = root / "case2";
  const fs::path got = rex::glue::ResolveSaveDirSetting("saves", base);
  Expect(got == (base / "saves").lexically_normal(), "relative setting joins base");
}

void TestQuotedSetting(const fs::path& root) {
  const fs::path base = root / "case3";
  const fs::path got = rex::glue::ResolveSaveDirSetting("  \"saves\"  ", base);
  Expect(got == (base / "saves").lexically_normal(), "quoted setting trims quotes");
}

void TestAbsoluteSetting(const fs::path& root) {
  const fs::path abs = root / "abs";
  CreateDirs(abs);
  const fs::path base = root / "case4";
  Expect(rex::glue::ResolveSaveDirSetting(ToUtf8(abs), base) == abs.lexically_normal(),
         "absolute setting resolves to itself");
}

#ifdef _WIN32
void TestEnvExpansion(const fs::path& root) {
  const fs::path env = root / "env";
  CreateDirs(env);
  (void)_putenv_s("GOW2_SAVE_TEST_DIR", ToUtf8(env).c_str());
  const fs::path base = root / "case5";
  CreateDirs(base);
  Expect(rex::glue::ResolveSaveDirSetting("%GOW2_SAVE_TEST_DIR%\\x", base) ==
             (env / "x").lexically_normal(),
         "env var expands");
  Expect(rex::glue::ResolveSaveDirSetting("%GOW2_NO_SUCH_VAR_123%", base) ==
             (base / "%GOW2_NO_SUCH_VAR_123%").lexically_normal(),
         "unknown env var stays literal");
}
#endif

void TestEmptySettingKeepsDefault(const fs::path& root) {
  const fs::path kase = root / "case6";
  const fs::path def = kase / "def";
  const fs::path cache = kase / "cache";
  const fs::path base = kase / "base";
  const rex::glue::SaveDirChoice choice = rex::glue::ChooseUserDataRoot("", def, cache, base);
  Expect(choice.user_data_root == def, "empty setting keeps default root");
  Expect(choice.summary.empty(), "empty setting has empty summary");
  Expect(!choice.is_warning, "empty setting is not a warning");
}

void TestSeedCopyAndroidLayout(const fs::path& root) {
  const fs::path kase = root / "case7";
  const fs::path def = kase / "def";
  const fs::path cache = def / "cache";
  const fs::path cloud = kase / "cloud";
  const fs::path base = kase / "base";
  const fs::path def_sav =
      def / "B13EBABEBABEBABE" / "4D53082D" / "00000001" / "Gears2Checkpoint" / "Gears2Checkpoint0.sav";
  WriteFile(def_sav, "save-a");
  WriteFile(def / "4D53082D" / "profile" / "User" / "63E83FFF", "profile");
  WriteFile(def / "cache" / "shaders" / "x.bin", "cache");
  const rex::glue::SaveDirChoice choice = rex::glue::ChooseUserDataRoot(ToUtf8(cloud), def, cache, base);
  Expect(choice.user_data_root == cloud.lexically_normal(), "seed copy uses cloud folder");
  Expect(!choice.is_warning, "seed copy is not a warning");
  Expect(Contains(choice.summary, "copied the existing saves"), "seed copy summary mentions copy");
  const fs::path cloud_sav =
      cloud / "B13EBABEBABEBABE" / "4D53082D" / "00000001" / "Gears2Checkpoint" / "Gears2Checkpoint0.sav";
  Expect(ReadFile(cloud_sav) == "save-a", "seed copy brings the save");
  Expect(ReadFile(cloud / "4D53082D" / "profile" / "User" / "63E83FFF") == "profile",
         "seed copy brings the profile");
  Expect(!Exists(cloud / "cache"), "seed copy skips the cache");
  Expect(Exists(def_sav), "seed copy keeps the default save");
  Expect(!Exists(cloud / ".gow2-write-test"), "seed copy leaves no probe file");
}

void TestNeverOverwrites(const fs::path& root) {
  const fs::path kase = root / "case7";  // continues the seed-copy case
  const fs::path def = kase / "def";
  const fs::path cache = def / "cache";
  const fs::path cloud = kase / "cloud";
  const fs::path base = kase / "base";
  const fs::path def_sav =
      def / "B13EBABEBABEBABE" / "4D53082D" / "00000001" / "Gears2Checkpoint" / "Gears2Checkpoint0.sav";
  const fs::path cloud_sav =
      cloud / "B13EBABEBABEBABE" / "4D53082D" / "00000001" / "Gears2Checkpoint" / "Gears2Checkpoint0.sav";
  WriteFile(def_sav, "save-b");
  const rex::glue::SaveDirChoice choice = rex::glue::ChooseUserDataRoot(ToUtf8(cloud), def, cache, base);
  Expect(ReadFile(cloud_sav) == "save-a", "second run keeps the cloud save");
  Expect(!Contains(choice.summary, "copied"), "second run does not copy again");
  Expect(choice.user_data_root == cloud.lexically_normal(), "second run keeps the cloud folder");
}

void TestWindowsLayout(const fs::path& root) {
  const fs::path kase = root / "case9";
  const fs::path def = kase / "user";
  const fs::path cache = kase / "cache";
  const fs::path cloud = kase / "cloud";
  const fs::path base = kase / "base";
  WriteFile(def / "Gears2Checkpoint0.sav", "win-save");
  WriteFile(cache / "shaders" / "x.bin", "cache");
  const rex::glue::SaveDirChoice choice = rex::glue::ChooseUserDataRoot(ToUtf8(cloud), def, cache, base);
  Expect(choice.user_data_root == cloud.lexically_normal(), "sibling cache layout uses cloud folder");
  Expect(!choice.is_warning, "sibling cache layout is not a warning");
  Expect(ReadFile(cloud / "Gears2Checkpoint0.sav") == "win-save", "sibling cache layout copies the save");
  Expect(!Exists(cloud / "cache"), "sibling cache layout skips the cache");
}

void TestSettingEqualToDefault(const fs::path& root) {
  const fs::path kase = root / "case10";
  const fs::path def = kase / "def";
  const fs::path cache = kase / "cache";
  const fs::path base = kase / "base";
  CreateDirs(def);
  const rex::glue::SaveDirChoice choice = rex::glue::ChooseUserDataRoot(ToUtf8(def), def, cache, base);
  Expect(choice.user_data_root == def, "default setting keeps default root");
  Expect(!choice.is_warning, "default setting is not a warning");
  Expect(Contains(choice.summary, "default"), "default setting summary mentions default");
}

void TestUnusableFolder(const fs::path& root) {
  const fs::path kase = root / "case11";
  const fs::path def = kase / "def";
  const fs::path cache = kase / "cache";
  const fs::path base = kase / "base";
  CreateDirs(def);
  const fs::path afile = kase / "afile.txt";
  WriteFile(afile, "x");
  const rex::glue::SaveDirChoice choice = rex::glue::ChooseUserDataRoot(ToUtf8(afile), def, cache, base);
  Expect(choice.user_data_root == def, "file setting falls back to default");
  Expect(choice.is_warning, "file setting is a warning");
  Expect(Contains(choice.summary, "cannot be used"), "file setting summary says cannot be used");
}

void TestRelativeSettingAgainstBase(const fs::path& root) {
  const fs::path kase = root / "case12";
  const fs::path base = kase / "exe";
  const fs::path def = kase / "def";
  const fs::path cache = kase / "cache";
  CreateDirs(base);
  CreateDirs(def);
  const rex::glue::SaveDirChoice choice = rex::glue::ChooseUserDataRoot("saves", def, cache, base);
  Expect(choice.user_data_root == (base / "saves").lexically_normal(),
         "relative setting resolves against base");
  Expect(Exists(base / "saves"), "relative setting creates the folder");
}

int main() {
  fs::path root;
  std::error_code ec;
  try {
    fs::path tmp = fs::temp_directory_path(ec);
    if (ec) {
      ec.clear();
      tmp = fs::current_path(ec);
      ec.clear();
    }
    std::random_device rd;
    root = tmp / ("gow2_save_dir_test_" + std::to_string(rd()));
    fs::remove_all(root, ec);
    ec.clear();
    fs::create_directories(root, ec);
    ec.clear();
    TestEmptySettingResolvesEmpty(root);
    TestRelativeSetting(root);
    TestQuotedSetting(root);
    TestAbsoluteSetting(root);
#ifdef _WIN32
    TestEnvExpansion(root);
#endif
    TestEmptySettingKeepsDefault(root);
    TestSeedCopyAndroidLayout(root);
    TestNeverOverwrites(root);
    TestWindowsLayout(root);
    TestSettingEqualToDefault(root);
    TestUnusableFolder(root);
    TestRelativeSettingAgainstBase(root);
  } catch (const std::exception& e) {
    ++g_checks;
    ++g_failures;
    std::printf("FAIL: exception: %s\n", e.what());
  } catch (...) {
    ++g_checks;
    ++g_failures;
    std::printf("FAIL: unknown exception\n");
  }
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  if (!root.empty()) {
    fs::remove_all(root, ec);
  }
  return g_failures == 0 ? 0 : 1;
}
