// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Pure text conversions for PC Options values.
#include "gow2_options_values_text.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace rex::glue::options::values_text {
namespace {

bool IsDigits(std::string_view text) {
  return !text.empty() && text.find_first_not_of("0123456789") == text.npos;
}

bool IsUnsignedWithin(std::string_view text, std::uint64_t limit) {
  if (!IsDigits(text)) return false;
  std::uint64_t value = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc{} && value <= limit;
}

std::optional<Pair> ParsePair(std::string_view text, char separator, std::uint64_t limit) {
  const auto split = text.find(separator);
  if (split == text.npos) return std::nullopt;
  const auto first = text.substr(0, split);
  const auto second = text.substr(split + 1);
  if (!IsUnsignedWithin(first, limit)) return std::nullopt;
  if (!IsUnsignedWithin(second, limit)) return std::nullopt;
  return Pair{std::string(first), std::string(second)};
}

bool IsPositiveInteger(std::string_view text) {
  if (!IsUnsignedWithin(text, std::numeric_limits<std::int32_t>::max())) return false;
  return text.find_first_not_of('0') != text.npos;
}

std::optional<double> PositiveDouble(std::string_view text) {
  if (text.empty()) return std::nullopt;
  double value = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return std::nullopt;
  if (!std::isfinite(value) || value <= 0) return std::nullopt;
  return value;
}

double ResolutionScale(std::string_view text) {
  const auto value = PositiveDouble(text);
  if (!value || *value > std::numeric_limits<double>::max() / 1280) return 1;
  return *value;
}

std::string InternalSize(double scale) {
  std::ostringstream text;
  text.imbue(std::locale::classic());
  text << std::fixed << std::setprecision(0) << 1280 * scale << 'x' << 720 * scale;
  return text.str();
}

bool IsIsoDateTime(std::string_view text) {
  if (text.size() < 19) return false;
  if (text[4] != '-' || text[7] != '-' || text[10] != 'T') return false;
  if (text[13] != ':' || text[16] != ':') return false;
  return IsDigits(text.substr(0, 4)) && IsDigits(text.substr(5, 2)) &&
         IsDigits(text.substr(8, 2)) && IsDigits(text.substr(11, 2)) &&
         IsDigits(text.substr(14, 2)) && IsDigits(text.substr(17, 2));
}

}

std::optional<Pair> ParseWindowSize(std::string_view value) {
  if (value == "auto") return Pair{"0", "0"};
  return ParsePair(value, 'x', std::numeric_limits<std::int32_t>::max());
}

std::optional<Pair> ParseTextureMemory(std::string_view value) {
  return ParsePair(value, '/', std::numeric_limits<std::uint64_t>::max());
}

std::string WindowMode(std::string_view fullscreen, std::string_view exclusive) {
  if (fullscreen != "true" && fullscreen != "1") return "windowed";
  if (exclusive == "true" || exclusive == "1") return "exclusive";
  return "borderless";
}

std::string WindowSize(std::string_view width, std::string_view height) {
  if (!IsPositiveInteger(width) || !IsPositiveInteger(height)) return "auto";
  return std::string(width) + "x" + std::string(height);
}

std::string UpscaleRatio(std::string_view scale, std::string_view target_w,
                         std::string_view target_h) {
  const double factor = ResolutionScale(scale);
  const auto internal = InternalSize(factor);
  if (target_w.empty() || target_h.empty()) return internal;
  const auto height = PositiveDouble(target_h);
  if (!height || !PositiveDouble(target_w)) return internal;
  std::ostringstream text;
  text.imbue(std::locale::classic());
  text << internal << " \xe2\x86\x92 " << target_w << 'x' << target_h << " ("
       << std::fixed << std::setprecision(1) << *height / (720 * factor) << "x)";
  return text.str();
}

std::string LeaseTime(std::string_view lease_end) {
  if (!IsIsoDateTime(lease_end)) return {};
  return std::string(lease_end.substr(11, 5));
}

}
