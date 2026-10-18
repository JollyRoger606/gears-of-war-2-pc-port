// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Comment-preserving edits of the flat gow2.toml options file.
#include "gow2_toml_edit.h"

#include <charconv>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace rex::glue::toml_edit {

namespace {

constexpr char kMarker[] = "# Set in game (PC Options)";

std::string PathUtf8(const std::filesystem::path& path) {
  const auto utf8 = path.u8string();
  return std::string(utf8.begin(), utf8.end());
}

bool IsBlank(char c) { return c == ' ' || c == '\t'; }

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

bool IsKeyChar(char c) {
  if (IsDigit(c)) return true;
  if (c >= 'A' && c <= 'Z') return true;
  if (c >= 'a' && c <= 'z') return true;
  return c == '_' || c == '-';
}

bool IsBlankLine(std::string_view line) {
  for (char c : line) {
    if (!IsBlank(c)) return false;
  }
  return true;
}

size_t SkipBlank(std::string_view line, size_t i) {
  while (i < line.size() && IsBlank(line[i])) ++i;
  return i;
}

bool HasTripleAt(std::string_view line, size_t i, char quote) {
  return i + 3 <= line.size() && line[i] == quote && line[i + 1] == quote && line[i + 2] == quote;
}

// Advances the multi-line string state past one line; single-line strings never span lines.
char ScanLine(std::string_view line, char triple) {
  char str = 0;
  size_t i = 0;
  while (i < line.size()) {
    const char c = line[i];
    if (triple != 0) {
      if (triple == '"' && c == '\\') {
        i += 2;
        continue;
      }
      if (HasTripleAt(line, i, triple)) {
        triple = 0;
        i += 3;
        continue;
      }
      ++i;
      continue;
    }
    if (str != 0) {
      if (str == '"' && c == '\\') {
        i += 2;
        continue;
      }
      if (c == str) str = 0;
      ++i;
      continue;
    }
    if (c == '#') break;
    if (c == '"' || c == '\'') {
      if (HasTripleAt(line, i, c)) {
        triple = c;
        i += 3;
        continue;
      }
      str = c;
    }
    ++i;
  }
  return triple;
}

std::vector<char> InsideLines(const std::vector<std::string>& lines) {
  std::vector<char> inside(lines.size(), 0);
  char triple = 0;
  for (size_t n = 0; n < lines.size(); ++n) {
    inside[n] = (triple != 0);
    triple = ScanLine(lines[n], triple);
  }
  return inside;
}

size_t FindHeader(const std::vector<std::string>& lines, const std::vector<char>& inside) {
  for (size_t i = 0; i < lines.size(); ++i) {
    if (inside[i]) continue;
    const size_t j = SkipBlank(lines[i], 0);
    if (j < lines[i].size() && lines[i][j] == '[') return i;
  }
  return lines.size();
}

struct LineKey {
  bool hit = false;
  size_t key_start = 0;
  size_t eq = 0;
};

LineKey MatchActive(std::string_view line, std::string_view key) {
  LineKey out;
  if (key.empty()) return out;
  const size_t start = SkipBlank(line, 0);
  size_t i = start;
  while (i < line.size() && IsKeyChar(line[i])) ++i;
  if (line.substr(start, i - start) != key) return out;
  const size_t eq = SkipBlank(line, i);
  if (eq >= line.size() || line[eq] != '=') return out;
  out.hit = true;
  out.key_start = start;
  out.eq = eq;
  return out;
}

LineKey MatchCommented(std::string_view line, std::string_view key) {
  LineKey out;
  if (key.empty()) return out;
  size_t i = SkipBlank(line, 0);
  if (i >= line.size() || line[i] != '#') return out;
  ++i;
  while (i < line.size() && (line[i] == '#' || IsBlank(line[i]))) ++i;
  const size_t start = i;
  while (i < line.size() && IsKeyChar(line[i])) ++i;
  if (line.substr(start, i - start) != key) return out;
  const size_t eq = SkipBlank(line, i);
  if (eq >= line.size() || line[eq] != '=') return out;
  out.hit = true;
  out.key_start = start;
  out.eq = eq;
  return out;
}

bool IsActiveAny(std::string_view line) {
  const size_t start = SkipBlank(line, 0);
  size_t i = start;
  while (i < line.size() && IsKeyChar(line[i])) ++i;
  if (i == start) return false;
  const size_t eq = SkipBlank(line, i);
  return eq < line.size() && line[eq] == '=';
}

// Finds the '#' starting a trailing comment, skipping over any quoted strings.
std::string TrailingComment(std::string_view line, size_t start, char triple = 0) {
  char str = 0;
  size_t i = start;
  while (i < line.size()) {
    const char c = line[i];
    if (triple != 0) {
      if (triple == '"' && c == '\\') {
        i += 2;
        continue;
      }
      if (HasTripleAt(line, i, triple)) {
        triple = 0;
        i += 3;
        continue;
      }
      ++i;
      continue;
    }
    if (str != 0) {
      if (str == '"' && c == '\\') {
        i += 2;
        continue;
      }
      if (c == str) str = 0;
      ++i;
      continue;
    }
    if (c == '#') break;
    if (c == '"' || c == '\'') {
      if (HasTripleAt(line, i, c)) {
        triple = c;
        i += 3;
        continue;
      }
      str = c;
    }
    ++i;
  }
  if (i >= line.size()) return {};
  size_t end = line.size();
  while (end > i && IsBlank(line[end - 1])) --end;
  return std::string(line.substr(i, end - i));
}

size_t FindMarker(const std::vector<std::string>& lines, const std::vector<char>& inside, size_t top) {
  for (size_t i = 0; i < top; ++i) {
    if (inside[i]) continue;
    const std::string_view line = lines[i];
    const size_t first = SkipBlank(line, 0);
    size_t last = line.size();
    while (last > first && IsBlank(line[last - 1])) --last;
    if (line.substr(first, last - first) == kMarker) return i;
  }
  return lines.size();
}

std::string DetectEol(std::string_view text) {
  return text.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
}

std::vector<std::string> SplitLines(std::string_view text) {
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

std::string JoinLines(const std::vector<std::string>& lines, std::string_view eol) {
  if (lines.empty()) return std::string(eol);
  std::string out;
  for (const std::string& line : lines) {
    out += line;
    out += eol;
  }
  return out;
}

void StripTrailingEmpty(std::vector<std::string>& lines) {
  while (!lines.empty() && lines.back().empty()) lines.pop_back();
}

bool ParseInt(std::string_view value) {
  if (value.empty()) return false;
  size_t i = 0;
  if (value[0] == '-') i = 1;
  if (i >= value.size()) return false;
  for (size_t j = i; j < value.size(); ++j) {
    if (!IsDigit(value[j])) return false;
  }
  long long parsed = 0;
  const char* first = value.data();
  const char* last = value.data() + value.size();
  const std::from_chars_result got = std::from_chars(first, last, parsed);
  return got.ec == std::errc() && got.ptr == last;
}

bool ParseFloat(std::string_view value) {
  size_t i = 0;
  if (i < value.size() && (value[i] == '+' || value[i] == '-')) ++i;
  size_t digits = 0;
  while (i < value.size() && IsDigit(value[i])) {
    ++i;
    ++digits;
  }
  if (i < value.size() && value[i] == '.') {
    ++i;
    while (i < value.size() && IsDigit(value[i])) {
      ++i;
      ++digits;
    }
  }
  if (digits == 0) return false;
  if (i < value.size() && (value[i] == 'e' || value[i] == 'E')) {
    ++i;
    if (i < value.size() && (value[i] == '+' || value[i] == '-')) ++i;
    size_t exp = 0;
    while (i < value.size() && IsDigit(value[i])) {
      ++i;
      ++exp;
    }
    if (exp == 0) return false;
  }
  if (i != value.size()) return false;
  const std::string copy(value);
  char* end = nullptr;
  const double number = std::strtod(copy.c_str(), &end);
  return end == copy.c_str() + copy.size() && std::isfinite(number);
}

std::string FormatString(std::string_view value) {
  const bool backslash = value.find('\\') != std::string_view::npos;
  const bool squote = value.find('\'') != std::string_view::npos;
  const bool newline =
      value.find('\n') != std::string_view::npos || value.find('\r') != std::string_view::npos;
  if (backslash && !squote && !newline) {
    std::string out = "'";
    out += value;
    out += "'";
    return out;
  }
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out = "\"";
  for (const char c : value) {
    const unsigned char u = static_cast<unsigned char>(c);
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (u < 0x20) {
          out += "\\u00";
          out += kHex[(u >> 4) & 0xF];
          out += kHex[u & 0xF];
        } else {
          out += c;
        }
        break;
    }
  }
  out += '"';
  return out;
}

size_t ValueEnd(const std::vector<std::string>& lines, size_t start) {
  char triple = ScanLine(lines[start], 0);
  size_t end = start + 1;
  while (triple != 0 && end < lines.size()) {
    triple = ScanLine(lines[end], triple);
    ++end;
  }
  return end;
}

void CommentSpan(std::vector<std::string>& lines, size_t start, size_t end) {
  for (size_t i = start; i < end; ++i) lines[i] = "# " + lines[i];
}

bool ReplaceActive(std::vector<std::string>& lines, const std::vector<char>& inside, size_t top,
                   std::string_view key, const std::string& made) {
  size_t first = top;
  for (size_t i = 0; i < top; ++i) {
    if (inside[i]) continue;
    if (MatchActive(lines[i], key).hit) {
      first = i;
      break;
    }
  }
  if (first == top) return false;
  for (size_t i = top; i-- > first;) {
    if (inside[i]) continue;
    const LineKey at = MatchActive(lines[i], key);
    if (!at.hit) continue;
    const size_t end = ValueEnd(lines, i);
    if (i != first) {
      CommentSpan(lines, i, end);
      continue;
    }
    const std::string comment = end == i + 1
        ? TrailingComment(lines[i], at.eq + 1)
        : TrailingComment(lines[end - 1], 0, ScanLine(lines[i], 0));
    lines[i] = lines[i].substr(0, at.key_start) + made + (comment.empty() ? "" : "  " + comment);
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i + 1),
                lines.begin() + static_cast<std::ptrdiff_t>(end));
  }
  return true;
}

bool ReplaceCommented(std::vector<std::string>& lines, const std::vector<char>& inside, size_t top,
                      std::string_view key, const std::string& made) {
  for (size_t i = 0; i < top; ++i) {
    if (inside[i]) continue;
    if (!MatchCommented(lines[i], key).hit) continue;
    lines[i] = made;
    return true;
  }
  return false;
}

void AppendKey(std::vector<std::string>& lines, const std::vector<char>& inside, size_t top,
               const std::string& made) {
  const size_t marker = FindMarker(lines, inside, top);
  if (marker != lines.size()) {
    size_t pos = marker + 1;
    while (pos < top && !inside[pos] && IsActiveAny(lines[pos])) ++pos;
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(pos), made);
    return;
  }
  std::vector<std::string> block;
  if (top > 0 && !IsBlankLine(lines[top - 1])) block.emplace_back();
  block.emplace_back(kMarker);
  block.push_back(made);
  lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(top), block.begin(), block.end());
}

}  // namespace

std::string FormatValue(ValueKind kind, std::string_view value) {
  switch (kind) {
    case ValueKind::kBool:
      return value == "true" ? "true" : "false";
    case ValueKind::kInt:
      return ParseInt(value) ? std::string(value) : "0";
    case ValueKind::kFloat: {
      if (!ParseFloat(value)) return "0.0";
      std::string out(value);
      if (out.find_first_of(".eE") == std::string::npos) out += ".0";
      return out;
    }
    case ValueKind::kString:
      return FormatString(value);
  }
  return "\"\"";
}

std::string Upsert(std::string_view text, const std::vector<Assignment>& assignments) {
  const std::string eol = DetectEol(text);
  std::vector<std::string> lines = SplitLines(text);
  for (const Assignment& a : assignments) {
    if (a.key.empty()) continue;
    const std::string made = a.key + " = " + FormatValue(a.kind, a.value);
    const std::vector<char> inside = InsideLines(lines);
    const size_t top = FindHeader(lines, inside);
    if (ReplaceActive(lines, inside, top, a.key, made)) continue;
    if (ReplaceCommented(lines, inside, top, a.key, made)) continue;
    AppendKey(lines, inside, top, made);
  }
  StripTrailingEmpty(lines);
  return JoinLines(lines, eol);
}

std::string Remove(std::string_view text, const std::vector<std::string>& keys) {
  const std::string eol = DetectEol(text);
  std::vector<std::string> lines = SplitLines(text);
  for (const std::string& key : keys) {
    if (key.empty()) continue;
    const std::vector<char> inside = InsideLines(lines);
    const size_t top = FindHeader(lines, inside);
    for (size_t i = 0; i < top; ++i) {
      if (inside[i]) continue;
      if (!MatchActive(lines[i], key).hit) continue;
      const size_t end = ValueEnd(lines, i);
      CommentSpan(lines, i, end);
      i = end - 1;
    }
  }
  StripTrailingEmpty(lines);
  return JoinLines(lines, eol);
}

bool ReadFile(const std::filesystem::path& path, std::string* text) {
  if (text == nullptr) return false;
  text->clear();
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::string out;
  char chunk[8192];
  while (in) {
    in.read(chunk, sizeof(chunk));
    out.append(chunk, static_cast<size_t>(in.gcount()));
  }
  if (in.bad()) return false;
  *text = std::move(out);
  return true;
}

namespace {

std::filesystem::path TemporaryPath(const std::filesystem::path& path) {
  static std::atomic<unsigned long long> counter{0};
#ifdef _WIN32
  const auto pid = GetCurrentProcessId();
#else
  const auto pid = getpid();
#endif
  std::filesystem::path tmp = path;
  tmp += ".tmp." + std::to_string(pid) + "." +
         std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
  return tmp;
}

std::error_code ReplaceFile(const std::filesystem::path& tmp, const std::filesystem::path& path) {
#ifdef _WIN32
  // Another writer, a virus scanner or the search indexer can hold the destination for a moment;
  // the move then fails with an access or sharing error that goes away on its own.
  DWORD last = 0;
  for (int attempt = 0; attempt < 50; ++attempt) {
    if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      return {};
    }
    last = GetLastError();
    if (last != ERROR_ACCESS_DENIED && last != ERROR_SHARING_VIOLATION) break;
    Sleep(10);
  }
  return std::error_code(last, std::system_category());
#else
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  return ec;
#endif
}

}  // namespace

bool WriteFileAtomic(const std::filesystem::path& path, std::string_view text, std::string* error) {
  const std::filesystem::path tmp = TemporaryPath(path);
  const auto fail = [&](const std::string& what) {
    if (error != nullptr) *error = what;
    std::error_code gone;
    std::filesystem::remove(tmp, gone);
    return false;
  };
  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) return fail("cannot create directory " + PathUtf8(parent) + ": " + ec.message());
  }
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return fail("cannot write " + PathUtf8(tmp) + ": open failed");
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    out.close();
    if (!out) return fail("cannot write " + PathUtf8(tmp) + ": write failed");
  }
  const std::error_code ec = ReplaceFile(tmp, path);
  if (ec) return fail("cannot replace " + PathUtf8(path) + ": " + ec.message());
  if (error != nullptr) error->clear();
  return true;
}

}  // namespace rex::glue::toml_edit
