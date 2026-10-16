// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Host-side test of the comment-preserving gow2.toml editor (no game data, no SDK).
#include "gow2_toml_edit.h"

#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
namespace te = rex::glue::toml_edit;

int g_checks = 0;
int g_failures = 0;
void Expect(bool ok, std::string_view what) { ++g_checks; if (ok) return; ++g_failures; std::printf("FAIL: %.*s\n", int(what.size()), what.data()); }

bool Contains(std::string_view haystack, std::string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}

size_t Count(std::string_view haystack, std::string_view needle) {
  size_t found = 0;
  size_t pos = 0;
  while (pos <= haystack.size()) {
    const size_t at = haystack.find(needle, pos);
    if (at == std::string_view::npos) break;
    ++found;
    pos = at + (needle.empty() ? 1 : needle.size());
  }
  return found;
}

bool EndsWith(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

std::vector<std::string> Split(std::string_view text) {
  std::vector<std::string> lines;
  size_t start = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '\n') continue;
    std::string_view piece = text.substr(start, i - start);
    if (!piece.empty() && piece.back() == '\r') piece.remove_suffix(1);
    lines.emplace_back(piece);
    start = i + 1;
  }
  if (start < text.size()) lines.emplace_back(text.substr(start));
  return lines;
}

bool IsKeyChar(char c) {
  if (c >= '0' && c <= '9') return true;
  if (c >= 'A' && c <= 'Z') return true;
  if (c >= 'a' && c <= 'z') return true;
  return c == '_' || c == '-';
}

bool IsActiveFor(std::string_view line, std::string_view key) {
  size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  const size_t start = i;
  while (i < line.size() && IsKeyChar(line[i])) ++i;
  if (line.substr(start, i - start) != key) return false;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  return i < line.size() && line[i] == '=';
}

size_t ActiveCount(std::string_view text, std::string_view key) {
  size_t found = 0;
  for (const std::string& line : Split(text)) {
    if (IsActiveFor(line, key)) ++found;
  }
  return found;
}

bool HasLine(std::string_view text, std::string_view want) {
  for (const std::string& line : Split(text)) {
    if (line == want) return true;
  }
  return false;
}

bool IsCommentLine(std::string_view line) {
  size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  return i < line.size() && line[i] == '#';
}

bool AllCRLF(std::string_view text) {
  if (!Contains(text, "\r\n")) return false;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '\n') continue;
    if (i == 0 || text[i - 1] != '\r') return false;
  }
  return true;
}

void TestFormatValue() {
  using te::ValueKind;
  Expect(te::FormatValue(ValueKind::kBool, "true") == "true", "bool true stays true");
  Expect(te::FormatValue(ValueKind::kBool, "false") == "false", "bool false stays false");
  Expect(te::FormatValue(ValueKind::kBool, "yes") == "false", "bool junk becomes false");
  Expect(te::FormatValue(ValueKind::kBool, "") == "false", "bool empty becomes false");
  Expect(te::FormatValue(ValueKind::kInt, "3") == "3", "int passes through");
  Expect(te::FormatValue(ValueKind::kInt, "-12") == "-12", "negative int passes through");
  Expect(te::FormatValue(ValueKind::kInt, "abc") == "0", "bad int becomes 0");
  Expect(te::FormatValue(ValueKind::kInt, "") == "0", "empty int becomes 0");
  Expect(te::FormatValue(ValueKind::kInt, "12x") == "0", "partial int becomes 0");
  Expect(te::FormatValue(ValueKind::kInt, "-") == "0", "bare minus becomes 0");
  Expect(te::FormatValue(ValueKind::kFloat, "1.5") == "1.5", "float passes through");
  Expect(te::FormatValue(ValueKind::kFloat, "2") == "2.0", "bare 2 becomes 2.0");
  Expect(te::FormatValue(ValueKind::kFloat, "2e3") == "2e3", "exponent float passes through");
  Expect(te::FormatValue(ValueKind::kFloat, "abc") == "0.0", "bad float becomes 0.0");
  Expect(te::FormatValue(ValueKind::kFloat, "") == "0.0", "empty float becomes 0.0");
  Expect(te::FormatValue(ValueKind::kFloat, "nan") == "0.0", "nan becomes 0.0");
  Expect(te::FormatValue(ValueKind::kFloat, "inf") == "0.0", "inf becomes 0.0");
  Expect(te::FormatValue(ValueKind::kString, "C:\\Users\\x") == "'C:\\Users\\x'",
         "windows path uses literal string");
  Expect(te::FormatValue(ValueKind::kString, "C:\\o'b") == "\"C:\\\\o'b\"",
         "quote forces basic string with backslash escapes");
  Expect(te::FormatValue(ValueKind::kString, "say \"hi\"") == "\"say \\\"hi\\\"\"",
         "quotes are escaped");
  Expect(te::FormatValue(ValueKind::kString, "a\nb") == "\"a\\nb\"", "newline is escaped");
  Expect(te::FormatValue(ValueKind::kString, "a\tb") == "\"a\\tb\"", "tab is escaped");
  Expect(te::FormatValue(ValueKind::kString, "bilinear") == "\"bilinear\"",
         "plain text uses basic string");
  Expect(te::FormatValue(ValueKind::kString, std::string("a\x01" "b")) == "\"a\\u0001b\"",
         "control chars use unicode escapes");
}

void TestUpsertActive() {
  using te::ValueKind;
  std::string out =
      te::Upsert("vsync = true   # sync\n", {{"vsync", ValueKind::kBool, "false"}});
  Expect(out == "vsync = false  # sync\n", "replace keeps trailing comment");
  out = te::Upsert("  vsync = true # sync\n", {{"vsync", ValueKind::kBool, "false"}});
  Expect(out == "  vsync = false  # sync\n", "replace keeps indent and comment");
  out = te::Upsert("vsync = true\n", {{"vsync", ValueKind::kBool, "false"}});
  Expect(out == "vsync = false\n", "replace without comment stays bare");
  out = te::Upsert("name = \"a#b\"\n", {{"name", ValueKind::kString, "x"}});
  Expect(out == "name = \"x\"\n", "hash inside string is not a comment");
  out = te::Upsert("name = \"a#b\" # note\n", {{"name", ValueKind::kString, "x"}});
  Expect(out == "name = \"x\"  # note\n", "comment after string value is kept");
}

void TestUpsertCommented() {
  using te::ValueKind;
  std::string out =
      te::Upsert("# net_link_active = true\n", {{"net_link_active", ValueKind::kBool, "true"}});
  Expect(out == "net_link_active = true\n", "commented key uncomments in place");
  out = te::Upsert("#   gow2_save_dir = '%OneDrive%\\x'\n",
                   {{"gow2_save_dir", ValueKind::kString, "D:\\Saves"}});
  Expect(out == "gow2_save_dir = 'D:\\Saves'\n", "spaced comment uncomments in place");
  out = te::Upsert("   # vsync = true\n", {{"vsync", ValueKind::kBool, "false"}});
  Expect(out == "vsync = false\n", "indented comment uncomments without indent");
}

void TestUpsertAppend() {
  using te::ValueKind;
  const std::string out =
      te::Upsert("vsync = true\n", {{"fullscreen", ValueKind::kBool, "false"}});
  Expect(out == "vsync = true\n\n# Set in game (PC Options)\nfullscreen = false\n",
         "absent key appends under a marker");
  const std::string again =
      te::Upsert(out, {{"present_dither", ValueKind::kBool, "true"}});
  Expect(Count(again, "# Set in game (PC Options)") == 1, "second append adds no marker");
  Expect(Contains(again, "fullscreen = false\npresent_dither = true\n"),
         "second append lands under the marker");
  const std::string empty = te::Upsert("", {{"vsync", ValueKind::kBool, "true"}});
  Expect(empty == "# Set in game (PC Options)\nvsync = true\n",
         "empty input yields just the marker block");
}

void TestTableHeader() {
  using te::ValueKind;
  const std::string out =
      te::Upsert("vsync = true\n\n[mods]\nvsync = true\n", {{"fullscreen", ValueKind::kBool, "true"}});
  Expect(out == "vsync = true\n\n# Set in game (PC Options)\nfullscreen = true\n[mods]\nvsync = true\n",
         "append goes before the table header");
  const std::string section =
      te::Upsert("[mods]\nvsync = true\n", {{"vsync", ValueKind::kBool, "false"}});
  Expect(Contains(section, "[mods]\nvsync = true\n"), "post-header line is never changed");
  Expect(HasLine(section, "vsync = false"), "top-level copy is appended instead");
  const std::string indented =
      te::Upsert("  [mods]\nvsync = true\n", {{"vsync", ValueKind::kBool, "false"}});
  Expect(Contains(indented, "vsync = true\n"), "indented header still starts a section");
}

void TestMultilineString() {
  using te::ValueKind;
  const std::string out = te::Upsert("desc = \"\"\"\nvsync = true\n\"\"\"\nvsync = true\n",
                                     {{"vsync", ValueKind::kBool, "false"}});
  const std::vector<std::string> lines = Split(out);
  Expect(lines.size() == 4 && lines[1] == "vsync = true" && lines[3] == "vsync = false",
         "key inside triple-double block is ignored");
  const std::string single =
      te::Upsert("desc = '''\nvsync = true\n'''\n", {{"vsync", ValueKind::kBool, "false"}});
  Expect(Contains(single, "\nvsync = true\n"), "key inside triple-single block is ignored");
  Expect(Contains(single, "# Set in game (PC Options)\nvsync = false\n"),
         "blocked key appends instead");
}

void TestPrefixKeys() {
  using te::ValueKind;
  const std::string out =
      te::Upsert("net_peers2 = \"x\"\n", {{"net_peers", ValueKind::kString, "y"}});
  Expect(Contains(out, "net_peers2 = \"x\"\n"), "longer key is not a prefix match");
  Expect(ActiveCount(out, "net_peers") == 1, "short key appends instead");
  const std::string both = te::Upsert("net_peers = \"a\"\nnet_peers2 = \"x\"\n",
                                      {{"net_peers", ValueKind::kString, "y"}});
  Expect(both == "net_peers = \"y\"\nnet_peers2 = \"x\"\n", "exact key matches among prefixes");
}

void TestDuplicates() {
  using te::ValueKind;
  const std::string out =
      te::Upsert("vsync = true\nvsync = true\n", {{"vsync", ValueKind::kBool, "false"}});
  Expect(out == "vsync = false\n# vsync = true\n", "first duplicate replaced, rest commented");
}

void TestLineEndings() {
  using te::ValueKind;
  const std::string crlf =
      te::Upsert("vsync = true\r\nfullscreen = true\r\n", {{"vsync", ValueKind::kBool, "false"}});
  Expect(crlf == "vsync = false\r\nfullscreen = true\r\n", "crlf input stays crlf");
  const std::string added =
      te::Upsert("vsync = true\r\n", {{"new_key", ValueKind::kBool, "true"}});
  Expect(AllCRLF(added), "appended crlf lines use crlf");
  Expect(EndsWith(added, "\r\n") && !EndsWith(added, "\r\n\r\n"), "crlf output ends once");
  const std::string lf = te::Upsert("vsync = true\n", {{"vsync", ValueKind::kBool, "false"}});
  Expect(!Contains(lf, "\r"), "lf input stays lf");
  Expect(EndsWith(lf, "\n") && !EndsWith(lf, "\n\n"), "lf output ends once");
  const std::string missing = te::Upsert("vsync = true", {{"vsync", ValueKind::kBool, "false"}});
  Expect(missing == "vsync = false\n", "missing trailing newline is added");
}

void TestRemove() {
  const std::string out = te::Remove("vsync = true\nfullscreen = true\n", {"vsync"});
  Expect(out == "# vsync = true\nfullscreen = true\n", "remove comments the line");
  Expect(te::Remove("vsync = true\n", {"nope"}) == "vsync = true\n",
         "remove of unknown key changes nothing");
  Expect(te::Remove("[m]\nvsync = true\n", {"vsync"}) == "[m]\nvsync = true\n",
         "remove skips post-header lines");
  Expect(te::Remove("a = 1\r\nvsync = true\r\n", {"vsync"}) == "a = 1\r\n# vsync = true\r\n",
         "remove keeps crlf");
}

void TestRealConfig(int argc, char** argv) {
  if (argc < 2) {
    std::printf("(skipping real config: no path argument)\n");
    return;
  }
  std::string text;
  Expect(te::ReadFile(argv[1], &text) && !text.empty(), "real config reads from disk");
  if (text.empty()) return;
  const std::vector<te::Assignment> edits = {
      {"resolution_scale", te::ValueKind::kInt, "3"},
      {"net_link_active", te::ValueKind::kBool, "true"},
      {"gow2_save_dir", te::ValueKind::kString, "C:\\Users\\me\\OneDrive\\GoW2"},
  };
  const std::string out = te::Upsert(text, edits);
  int kept = 0;
  for (const std::string& line : Split(text)) {
    if (!IsCommentLine(line)) continue;
    if (Contains(line, "resolution_scale") || Contains(line, "net_link_active") ||
        Contains(line, "gow2_save_dir")) {
      continue;
    }
    ++kept;
    Expect(HasLine(out, line), "original comment line is still present");
  }
  Expect(kept > 10, "real config has comments to keep");
  Expect(ActiveCount(out, "resolution_scale") == 1, "resolution_scale appears once");
  Expect(ActiveCount(out, "net_link_active") == 1, "net_link_active appears once");
  Expect(ActiveCount(out, "gow2_save_dir") == 1, "gow2_save_dir appears once");
  Expect(Contains(out, "resolution_scale = 3"), "resolution_scale set to 3");
  Expect(Contains(out, "net_link_active = true"), "net_link_active set to true");
  Expect(Contains(out, "gow2_save_dir = 'C:\\Users\\me\\OneDrive\\GoW2'"),
         "save dir set as literal string");
}

void TestFileRoundTrip() {
  std::error_code ec;
  fs::path tmp = fs::temp_directory_path(ec);
  if (ec) {
    ec.clear();
    tmp = fs::current_path(ec);
    ec.clear();
  }
  std::random_device rd;
  const fs::path root = tmp / ("gow2_toml_edit_test_" + std::to_string(rd()));
  fs::remove_all(root, ec);
  ec.clear();
  const fs::path file = root / "sub" / "dir" / "gow2.toml";
  fs::path sibling = file;
  sibling += ".tmp";
  std::string text;
  Expect(!te::ReadFile(file, &text), "missing file reads false");
  std::string error;
  Expect(te::WriteFileAtomic(file, "vsync = true\n", &error), "atomic write creates folders");
  Expect(te::ReadFile(file, &text) && text == "vsync = true\n", "read back matches write");
  Expect(te::WriteFileAtomic(file, "vsync = false\n", &error), "atomic write overwrites");
  Expect(te::ReadFile(file, &text) && text == "vsync = false\n", "read back matches overwrite");
  Expect(!fs::exists(sibling, ec), "no tmp file left behind");
  fs::remove_all(root, ec);
}

int main(int argc, char** argv) {
  try {
    TestFormatValue();
    TestUpsertActive();
    TestUpsertCommented();
    TestUpsertAppend();
    TestTableHeader();
    TestMultilineString();
    TestPrefixKeys();
    TestDuplicates();
    TestLineEndings();
    TestRemove();
    TestRealConfig(argc, argv);
    TestFileRoundTrip();
  } catch (const std::exception& e) {
    ++g_checks;
    ++g_failures;
    std::printf("FAIL: exception: %s\n", e.what());
  } catch (...) {
    ++g_checks;
    ++g_failures;
    std::printf("FAIL: unknown exception\n");
  }
  std::printf("toml_edit_test: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
