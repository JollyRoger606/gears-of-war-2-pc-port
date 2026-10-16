// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Host-side checks for the SDK-free PC Options text conversions.
#include "gow2_options_values_text.h"

#include <cstdio>
#include <string_view>

namespace {
namespace text = rex::glue::options::values_text;
int checks = 0;
int failures = 0;

void Expect(bool ok, std::string_view name) {
  ++checks;
  if (ok) return;
  ++failures;
  std::printf("FAIL: %.*s\n", static_cast<int>(name.size()), name.data());
}

void ExpectPair(const std::optional<text::Pair>& pair, std::string_view first,
                std::string_view second, std::string_view name) {
  Expect(pair && pair->first == first && pair->second == second, name);
}

void TestWindowParsing() {
  ExpectPair(text::ParseWindowSize("auto"), "0", "0", "auto dimensions");
  ExpectPair(text::ParseWindowSize("1920x1080"), "1920", "1080", "window dimensions");
  ExpectPair(text::ParseWindowSize("0x0"), "0", "0", "zero dimensions");
  ExpectPair(text::ParseWindowSize("001x002"), "001", "002", "leading zero dimensions");
  ExpectPair(text::ParseWindowSize("2147483647x1"), "2147483647", "1", "maximum width");
  for (const auto value : {"", "AUTO", "1920X1080", "1920x", "x1080", "1x2x3",
                            "-1x2", "+1x2", " 1x2", "1x2 ", "1.5x2", "1x2px",
                            "2147483648x1", "1x2147483648", "18446744073709551616x1"}) {
    Expect(!text::ParseWindowSize(value), value);
  }
}

void TestMemoryParsing() {
  ExpectPair(text::ParseTextureMemory("512/1024"), "512", "1024", "memory limits");
  ExpectPair(text::ParseTextureMemory("0/0"), "0", "0", "zero memory limits");
  ExpectPair(text::ParseTextureMemory("18446744073709551615/1"),
             "18446744073709551615", "1", "maximum memory limit");
  for (const auto value : {"", "1", "/2", "1/", "1/2/3", "-1/2", "1/+2",
                            "1 /2", "1/2MB", "18446744073709551616/1",
                            "1/18446744073709551616"}) {
    Expect(!text::ParseTextureMemory(value), value);
  }
}

void TestWindowMapping() {
  Expect(text::WindowMode("false", "false") == "windowed", "windowed mode");
  Expect(text::WindowMode("false", "true") == "windowed", "windowed ignores exclusive");
  Expect(text::WindowMode("true", "false") == "borderless", "borderless mode");
  Expect(text::WindowMode("true", "true") == "exclusive", "exclusive mode");
  Expect(text::WindowMode("1", "1") == "exclusive", "numeric bool mode");
  Expect(text::WindowMode("", "") == "windowed", "missing bool mode");
  Expect(text::WindowSize("1920", "1080") == "1920x1080", "window size text");
  Expect(text::WindowSize("0", "1080") == "auto", "zero width is auto");
  Expect(text::WindowSize("1920", "0") == "auto", "zero height is auto");
  Expect(text::WindowSize("-1", "720") == "auto", "negative dimension is auto");
  Expect(text::WindowSize("oops", "720") == "auto", "invalid dimension is auto");
  Expect(text::WindowSize("2147483648", "720") == "auto", "overflow dimension is auto");
}

void TestUpscaleRatio() {
  const std::string_view arrow = " \xe2\x86\x92 ";
  Expect(text::UpscaleRatio("1", "", "") == "1280x720", "internal size alone");
  Expect(text::UpscaleRatio("1.5", "", "") == "1920x1080", "scaled internal size");
  Expect(text::UpscaleRatio("0.5", "", "") == "640x360", "fractional internal size");
  Expect(text::UpscaleRatio("1", "1920", "1080") ==
             std::string("1280x720") + std::string(arrow) + "1920x1080 (1.5x)", "ratio text");
  Expect(text::UpscaleRatio("1.5", "2560", "1440") ==
             std::string("1920x1080") + std::string(arrow) + "2560x1440 (1.3x)", "ratio rounds");
  Expect(text::UpscaleRatio("2", "1920", "") == "2560x1440", "missing target height");
  Expect(text::UpscaleRatio("2", "", "1080") == "2560x1440", "missing target width");
  Expect(text::UpscaleRatio("1", "1920", "bad") == "1280x720", "invalid target is safe");
  for (const auto value : {"", "bad", "1junk", "0", "-2", "nan", "inf", "1e999", "1e308"}) {
    Expect(text::UpscaleRatio(value, "", "") == "1280x720", value);
  }
}

void TestLeaseTime() {
  Expect(text::LeaseTime("2026-10-16T09:07:00") == "09:07", "lease time");
  Expect(text::LeaseTime("2026-10-16T23:59:59Z") == "23:59", "UTC lease time");
  for (const auto value : {"", "short", "2026-10-16T09:0", "2026-10-16 09:07:00",
                            "xxxx-10-16T09:07:00", "2026-10-16Txx:07:00"}) {
    Expect(text::LeaseTime(value).empty(), value);
  }
}
}

int main() {
  TestWindowParsing();
  TestMemoryParsing();
  TestWindowMapping();
  TestUpscaleRatio();
  TestLeaseTime();
  std::printf("options_values_test: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
