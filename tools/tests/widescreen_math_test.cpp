// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Host-side test of the widescreen arithmetic (no SDK).
#include "gow2_widescreen_math.h"

#include <cmath>
#include <cstdio>
#include <string_view>

int g_checks = 0;
int g_failures = 0;
void Expect(bool ok, std::string_view what) { ++g_checks; if (ok) return; ++g_failures; std::printf("FAIL: %.*s\n", int(what.size()), what.data()); }
bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

int main() {
  using namespace rex::glue::widescreen;

  // Settings
  Expect(ParseAspect("off", 2.4) == 0.0, "off");
  Expect(ParseAspect("", 2.4) == 0.0, "empty is off");
  Expect(ParseAspect("0", 2.4) == 0.0, "0 is off");
  Expect(Near(ParseAspect("21:9", 0), 21.0 / 9.0, 1e-9), "21:9");
  Expect(Near(ParseAspect("16:10", 0), 1.6, 1e-9), "16:10");
  Expect(Near(ParseAspect("2.39", 0), 2.39, 1e-9), "plain number");
  Expect(Near(ParseAspect("auto", 3440.0 / 1440.0), 3440.0 / 1440.0, 1e-9), "auto takes the display");
  Expect(ParseAspect("auto", 0) == 0.0, "auto with no display is off");
  Expect(ParseAspect("9", 0) == 0.0, "9 is not an aspect");
  Expect(ParseAspect("0.5", 0) == 0.0, "narrower than 1.2 is refused");
  Expect(ParseAspect("5:1", 0) == 0.0, "wider than 4 is refused");
  Expect(ParseAspect("x:y", 0) == 0.0, "garbage is off");
  Expect(ParseAspect("5:0", 0) == 0.0, "zero height is off");

  // Back buffer width
  Expect(BackBufferWidth(0.0, 704) == 0, "off has no width");
  Expect(BackBufferWidth(21.0 / 9.0, 704) == 1680, "21:9 at 704 lines is 1680 wide");
  Expect(BackBufferWidth(3440.0 / 1440.0, 704) == 1680, "3440x1440 at 704 lines is 1680 wide");
  Expect(BackBufferWidth(21.0 / 9.0, 720) == 1680, "21:9 at 720 lines is 1680 wide");
  Expect(BackBufferWidth(16.0 / 9.0, 720) == 0, "16:9 at 720 lines is the game's own 1280");
  Expect(BackBufferWidth(16.0 / 9.0, 704) == 1280, "16:9 at 704 lines is 1280");
  Expect(BackBufferWidth(32.0 / 9.0, 704) == 1680, "32:9 is held to 1680 (EDRAM)");
  Expect(BackBufferWidth(1.3333, 704) == 960 || BackBufferWidth(1.3333, 704) == 960, "4:3 is held to 960");
  Expect(BackBufferWidth(2.0, 704) % 80 == 0, "widths are whole EDRAM tiles");
  for (double a = 1.3; a <= 4.0; a += 0.07) {
    const int w = BackBufferWidth(a, 704);
    Expect(w == 0 || (w >= 960 && w <= 1680 && w % 80 == 0), "every width is a tile multiple in range");
  }

  // Shadow maps next to the wider scene buffers
  Expect(MaxShadowResolution(0, 704) == 0, "no widescreen leaves the shadow size alone");
  Expect(MaxShadowResolution(1280, 704) == 0, "the game's 800 fits at 1280");
  Expect(MaxShadowResolution(1680, 704) == 480, "1680x704 leaves room for 480");
  Expect(MaxShadowResolution(1680, 704) % 32 == 0, "a multiple of 32");
  Expect(MaxShadowResolution(1600, 704) > MaxShadowResolution(1680, 704), "less width leaves more");
  Expect(MaxShadowResolution(1760, 704) >= 128, "never below 128");

  // Field of view
  Expect(Near(WidenHalfFov(0.7854, 0), 0.7854, 1e-12), "off keeps the angle");
  Expect(Near(WidenHalfFov(0.7854, 1280), 0.7854, 1e-3), "1280 wide keeps the angle");
  const double wide = WidenHalfFov(std::atan(1.0), 1680);
  Expect(Near(std::tan(wide), 1.3125, 1e-9), "1680 wide scales the tangent by 1680/1280");
  Expect(wide > 0.7854 && wide < 1.5707, "a wider half angle, below 90 degrees");
  // The vertical view is unchanged: tan(v/2) = tan(h/2) / aspect with aspect = width / height.
  const double h = std::atan(0.8), height = 704.0;
  Expect(Near(std::tan(WidenHalfFov(h, 1680)) / (1680.0 / height), std::tan(h) / (1280.0 / height), 1e-9),
         "the vertical field of view is the one of the 16:9 game");

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
