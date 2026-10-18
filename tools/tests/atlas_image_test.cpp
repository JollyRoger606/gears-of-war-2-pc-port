// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Host-side test of the RGBA image helpers of the keycap atlas (no SDK).
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "gow2_atlas_image.h"

using namespace rex::glue::atlas;

int g_checks = 0;
int g_failures = 0;
void Expect(bool ok, std::string_view what) { ++g_checks; if (ok) return; ++g_failures; std::printf("FAIL: %.*s\n", int(what.size()), what.data()); }

Image Solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  Image im;
  im.width = w;
  im.height = h;
  im.rgba.resize(size_t(w) * h * 4);
  for (size_t i = 0; i < im.rgba.size(); i += 4) {
    im.rgba[i] = r; im.rgba[i + 1] = g; im.rgba[i + 2] = b; im.rgba[i + 3] = a;
  }
  return im;
}

// A DDS header like the pack tools write: DX10, R8G8B8A8, `mips` levels.
std::vector<uint8_t> Header(int w, int h, int mips) {
  std::vector<uint8_t> hd(148, 0);
  std::memcpy(hd.data(), "DDS ", 4);
  auto put = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) hd[at + i] = uint8_t(v >> (8 * i)); };
  put(4, 124); put(12, uint32_t(h)); put(16, uint32_t(w)); put(28, uint32_t(mips));
  put(84, 0x30315844);  // DX10
  put(128, 28);         // R8G8B8A8_UNORM
  return hd;
}

int main() {
  Expect(MipCount(512, 512) == 10, "mip count of 512x512");
  Expect(MipCount(1, 1) == 1 && MipCount(4, 2) == 3, "mip count small");

  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "gow2_atlas_image_test";
  fs::create_directories(dir);
  const fs::path file = dir / "a.dds";
  Image a = Solid(8, 4, 10, 20, 30, 255);
  Expect(WriteDds(file, Header(8, 4, MipCount(8, 4)), a), "write");
  Expect(fs::file_size(file) == 148 + (8 * 4 + 4 * 2 + 2 * 1 + 1) * 4, "file holds the whole mip chain");
  Image b;
  std::vector<uint8_t> head;
  Expect(ReadDds(file, &b, &head), "read");
  Expect(b.width == 8 && b.height == 4 && b.rgba == a.rgba && head.size() == 148, "round trip");
  Expect(!ReadDds(dir / "missing.dds", &b, nullptr), "missing file");
  {
    std::ofstream(dir / "bad.dds", std::ios::binary) << "not a dds file at all, just text";
    Expect(!ReadDds(dir / "bad.dds", &b, nullptr), "garbage file");
  }
  Expect(!WriteDds(file, std::vector<uint8_t>(10), a), "short header refused");

  // Blit: a 2x2 opaque red square scaled to 4x4 into the middle of a blue 8x8.
  Image dst = Solid(8, 8, 0, 0, 255, 255);
  Image red = Solid(2, 2, 255, 0, 0, 255);
  Blit(dst, red, 0, 0, 2, 2, 2, 2, 4, 4);
  auto px = [&](int x, int y) { return &dst.rgba[(size_t(y) * 8 + x) * 4]; };
  Expect(px(3, 3)[0] == 255 && px(3, 3)[2] == 0, "inside is red");
  Expect(px(1, 1)[0] == 0 && px(1, 1)[2] == 255, "outside stays blue");
  // Half-transparent white over blue is a light blue; the destination alpha stays opaque.
  Image white = Solid(1, 1, 255, 255, 255, 128);
  Image dst2 = Solid(2, 2, 0, 0, 255, 255);
  Blit(dst2, white, 0, 0, 1, 1, 0, 0, 1, 1);
  Expect(dst2.rgba[0] > 100 && dst2.rgba[0] < 156 && dst2.rgba[2] == 255 && dst2.rgba[3] == 255, "alpha blend");
  // A transparent source changes nothing; a source outside its image is ignored.
  Image dst3 = Solid(2, 2, 1, 2, 3, 255);
  Blit(dst3, Solid(2, 2, 255, 255, 255, 0), 0, 0, 2, 2, 0, 0, 2, 2);
  Blit(dst3, red, 5, 5, 2, 2, 0, 0, 2, 2);
  Expect(dst3.rgba[0] == 1 && dst3.rgba[1] == 2 && dst3.rgba[2] == 3, "no change from nothing");
  // Downscale averages: half the source red, half green, onto one pixel.
  Image rg = Solid(2, 1, 255, 0, 0, 255);
  rg.rgba[4] = 0; rg.rgba[5] = 255; rg.rgba[6] = 0;
  Image dst4 = Solid(1, 1, 0, 0, 0, 0);
  Blit(dst4, rg, 0, 0, 2, 1, 0, 0, 1, 1);
  Expect(dst4.rgba[0] > 120 && dst4.rgba[0] < 135 && dst4.rgba[1] > 120 && dst4.rgba[1] < 135, "area average");

  fs::remove_all(dir);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
