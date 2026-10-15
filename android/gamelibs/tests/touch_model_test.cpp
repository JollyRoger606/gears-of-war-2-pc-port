// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors

#include "gow2_touch_model.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace rex::glue::touch;

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(const char* name, bool condition, const std::string& detail) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::cout << "FAIL " << name << ": " << detail << "\n";
  }
}

#define CHECK(name, cond, detail) Check(name, (cond), detail)

constexpr float kW = 3120.0f;
constexpr float kH = 1440.0f;

void TestTapFireLatch() {
  TouchModel model;
  model.SetViewport(kW, kH);
  model.PointerDown(1, 0.90f * kW, 0.60f * kH, 1000);
  CHECK("tap_fire_down", model.Sample(1000).right_trigger == 255, "RT should be 255 at down");
  model.PointerUp(1, 1020);
  CHECK("tap_fire_latched", model.Sample(1050).right_trigger == 255, "RT should stay 255");
  CHECK("tap_fire_expired", model.Sample(1101).right_trigger == 0, "RT should be 0 at t=1101");
}

void TestHoldFire() {
  TouchModel model;
  model.SetViewport(kW, kH);
  model.PointerDown(1, 0.90f * kW, 0.60f * kH, 0);
  CHECK("hold_fire_held", model.Sample(500).right_trigger == 255, "RT should be 255 at t=500");
  model.PointerUp(1, 600);
  CHECK("hold_fire_released", model.Sample(601).right_trigger == 0, "RT should be 0 at t=601");
}

void TestStickFullRight() {
  TouchModel model;
  model.SetViewport(kW, kH);
  float r = 0.10f * kH;
  model.PointerDown(1, 0.20f * kW, 0.70f * kH, 0);
  model.PointerMove(1, 0.20f * kW + r, 0.70f * kH, 10);
  PadState pad = model.Sample(10);
  CHECK("stick_full_right_lx", pad.thumb_lx == 32767, "thumb_lx == 32767");
  CHECK("stick_full_right_ly", pad.thumb_ly == 0, "thumb_ly == 0");
}

void TestStickHalfUp() {
  TouchModel model;
  model.SetViewport(kW, kH);
  float r = 0.10f * kH;
  model.PointerDown(1, 0.20f * kW, 0.70f * kH, 0);
  model.PointerMove(1, 0.20f * kW, 0.70f * kH - 0.5f * r, 10);
  PadState pad = model.Sample(10);
  CHECK("stick_half_up_lx", pad.thumb_lx == 0, "thumb_lx == 0");
  CHECK("stick_half_up_ly", pad.thumb_ly == 16384, "thumb_ly == 16384");
}

void TestStickClamped() {
  TouchModel model;
  model.SetViewport(kW, kH);
  float r = 0.10f * kH;
  model.PointerDown(1, 0.20f * kW, 0.70f * kH, 0);
  model.PointerMove(1, 0.20f * kW + 3.0f * r, 0.70f * kH + 4.0f * r, 10);
  PadState pad = model.Sample(10);
  int16_t expected_lx = static_cast<int16_t>(std::round(0.6f * 32767.0f));
  int16_t expected_ly = static_cast<int16_t>(std::round(-0.8f * 32767.0f));
  CHECK("stick_clamped_lx", pad.thumb_lx == expected_lx, "thumb_lx clamped");
  CHECK("stick_clamped_ly", pad.thumb_ly == expected_ly, "thumb_ly clamped");
}

void TestStickNotInTopStrip() {
  TouchModel model;
  model.SetViewport(kW, kH);
  float r = 0.10f * kH;
  model.PointerDown(1, 0.20f * kW, 0.05f * kH, 0);
  model.PointerMove(1, 0.20f * kW + r, 0.05f * kH, 10);
  CHECK("stick_not_in_top_strip", model.Sample(10) == PadState{}, "all zero PadState");
}

void TestLookFastSwipe() {
  TouchModel model;
  model.SetViewport(kW, kH);
  model.PointerDown(1, 0.60f * kW, 0.30f * kH, 0);
  for (int i = 1; i <= 5; ++i) {
    float x = 0.60f * kW + static_cast<float>(i) * 0.02f * kH;
    model.PointerMove(1, x, 0.30f * kH, i * 10);
  }
  PadState pad50 = model.Sample(50);
  CHECK("look_fast_swipe_rx", pad50.thumb_rx == 32767, "thumb_rx == 32767");
  CHECK("look_fast_swipe_ry", pad50.thumb_ry == 0, "thumb_ry == 0");
  PadState pad_held = model.Sample(50 + 60 + 1);
  CHECK("look_fast_swipe_held_rx", pad_held.thumb_rx == 0, "thumb_rx == 0 when held");
  CHECK("look_fast_swipe_held_ry", pad_held.thumb_ry == 0, "thumb_ry == 0 when held");
}

void TestLookSlowDrag() {
  TouchModel model;
  model.SetViewport(kW, kH);
  model.PointerDown(1, 0.60f * kW, 0.30f * kH, 0);
  for (int i = 1; i <= 5; ++i) {
    float x = 0.60f * kW + static_cast<float>(i) * 0.002f * kH;
    model.PointerMove(1, x, 0.30f * kH, i * 10);
  }
  PadState pad = model.Sample(50);
  int16_t expected = static_cast<int16_t>(std::round((0.30f + 0.70f * 0.1f) * 32767.0f));
  CHECK("look_slow_drag_rx", std::abs(pad.thumb_rx - expected) <= 2, "thumb_rx near expected");
  CHECK("look_slow_drag_ry", pad.thumb_ry == 0, "thumb_ry == 0");
}

void TestLookUpIsPositive() {
  TouchModel model;
  model.SetViewport(kW, kH);
  model.PointerDown(1, 0.60f * kW, 0.30f * kH, 0);
  for (int i = 1; i <= 5; ++i) {
    float y = 0.30f * kH - static_cast<float>(i) * 0.02f * kH;
    model.PointerMove(1, 0.60f * kW, y, i * 10);
  }
  PadState pad = model.Sample(50);
  CHECK("look_up_is_positive", pad.thumb_ry > 0, "thumb_ry > 0");
}

void TestMultiTouchCombines() {
  TouchModel model;
  model.SetViewport(kW, kH);
  float r = 0.10f * kH;
  model.PointerDown(1, 0.20f * kW, 0.70f * kH, 0);
  model.PointerMove(1, 0.20f * kW + r, 0.70f * kH, 50);

  model.PointerDown(2, 0.60f * kW, 0.30f * kH, 0);
  for (int i = 1; i <= 5; ++i) {
    float x = 0.60f * kW + static_cast<float>(i) * 0.02f * kH;
    model.PointerMove(2, x, 0.30f * kH, i * 10);
  }

  model.PointerDown(3, 0.90f * kW, 0.60f * kH, 0);
  model.PointerDown(4, 0.93f * kW, 0.84f * kH, 0);

  PadState pad = model.Sample(50);
  CHECK("multi_touch_lx", pad.thumb_lx > 0, "thumb_lx > 0");
  CHECK("multi_touch_rx", pad.thumb_rx > 0, "thumb_rx > 0");
  CHECK("multi_touch_rt", pad.right_trigger == 255, "right_trigger == 255");
  CHECK("multi_touch_a", (pad.buttons & button::kA) != 0, "buttons & kA");
}

void TestPointerKeepsItsButton() {
  TouchModel model;
  model.SetViewport(kW, kH);
  model.PointerDown(1, 0.93f * kW, 0.84f * kH, 0);
  model.PointerMove(1, 0.83f * kW, 0.88f * kH, 10);
  PadState pad = model.Sample(10);
  CHECK("pointer_keeps_its_button", pad.buttons == button::kA, "buttons == kA only");
}

void TestAimToggle() {
  TouchModel model;
  model.SetViewport(kW, kH);
  Settings settings;
  settings.aim_toggle = true;
  model.SetSettings(settings);

  model.PointerDown(1, 0.78f * kW, 0.74f * kH, 0);
  model.PointerUp(1, 10);
  CHECK("aim_toggle_on", model.Sample(500).left_trigger == 255, "left_trigger == 255");

  model.PointerDown(1, 0.78f * kW, 0.74f * kH, 1000);
  model.PointerUp(1, 1010);
  CHECK("aim_toggle_off", model.Sample(1500).left_trigger == 0, "left_trigger == 0");
}

void TestAimHoldDefault() {
  TouchModel model;
  model.SetViewport(kW, kH);
  model.PointerDown(1, 0.78f * kW, 0.74f * kH, 0);
  model.PointerUp(1, 200);
  CHECK("aim_hold_default", model.Sample(201).left_trigger == 0, "left_trigger == 0 at t=201");
}

void TestReleaseAll() {
  TouchModel model;
  model.SetViewport(kW, kH);
  Settings settings;
  settings.aim_toggle = true;
  model.SetSettings(settings);

  model.PointerDown(1, 0.20f * kW, 0.70f * kH, 0);
  model.PointerDown(2, 0.90f * kW, 0.60f * kH, 0);
  model.PointerDown(3, 0.78f * kW, 0.74f * kH, 0);
  model.PointerUp(3, 10);

  model.ReleaseAll();
  CHECK("release_all_state", model.Sample(100) == PadState{}, "PadState should be default");
  CHECK("release_all_has_pointers", !model.HasPointers(), "HasPointers should be false");
}

void TestDpadUp() {
  TouchModel model;
  model.SetViewport(kW, kH);
  model.PointerDown(1, 0.15f * kW, 0.28f * kH, 0);
  CHECK("dpad_up", (model.Sample(0).buttons & button::kDpadUp) != 0, "buttons & kDpadUp");
}

void TestStartBack() {
  TouchModel model;
  model.SetViewport(kW, kH);
  model.PointerDown(1, 0.53f * kW, 0.06f * kH, 0);
  CHECK("start_button", (model.Sample(0).buttons & button::kStart) != 0, "buttons & kStart");
  model.PointerUp(1, 0);

  model.PointerDown(2, 0.45f * kW, 0.06f * kH, 1000);
  CHECK("back_button", (model.Sample(1000).buttons & button::kBack) != 0, "buttons & kBack");
}

void TestScale() {
  TouchModel model;
  model.SetViewport(kW, kH);
  Settings settings;
  settings.scale = 1.5f;
  model.SetSettings(settings);

  auto controls = model.Controls(0);
  float expected_r = 0.085f * 1440.0f * 1.5f;
  CHECK("scale_radius", std::abs(controls[0].radius - expected_r) < 0.01f, "radius matches");

  float rt_x = 0.90f * kW;
  float rt_y = 0.60f * kH;
  model.PointerDown(1, rt_x + 1.1f * expected_r, rt_y, 0);
  CHECK("scale_hit_slop", model.Sample(0).right_trigger == 255, "RT pressed inside slop");
}

void TestZeroViewport() {
  TouchModel model;
  model.SetViewport(0.0f, 0.0f);
  model.PointerDown(1, 100.0f, 100.0f, 0);
  CHECK("zero_viewport_has_pointers", !model.HasPointers(), "HasPointers is false");
  CHECK("zero_viewport_sample", model.Sample(0) == PadState{}, "Sample is PadState{}");
}

void TestFreshModelIsNeutral() {
  TouchModel model;
  model.SetViewport(kW, kH);
  CHECK("fresh_model_neutral_at_zero", model.Sample(0) == PadState{}, "nothing pressed at t=0");
  CHECK("fresh_model_neutral_later", model.Sample(5000) == PadState{}, "nothing pressed later");
}

void TestSecondStickFingerInert() {
  TouchModel model;
  model.SetViewport(kW, kH);
  float r = 0.10f * kH;
  model.PointerDown(1, 0.20f * kW, 0.70f * kH, 0);
  model.PointerMove(1, 0.20f * kW + r, 0.70f * kH, 10);

  model.PointerDown(2, 0.25f * kW, 0.70f * kH, 20);
  model.PointerMove(2, 0.25f * kW - r, 0.70f * kH, 30);

  PadState pad = model.Sample(30);
  CHECK("second_stick_inert", pad.thumb_lx == 32767, "thumb_lx follows pointer 1 only");
}

void TestStickView() {
  TouchModel model;
  model.SetViewport(kW, kH);
  StickView idle = model.Stick();
  CHECK("stick_view_idle_active", !idle.active, "idle stick inactive");
  CHECK("stick_view_idle_base_x", std::abs(idle.base_x - 0.17f * kW) < 0.01f, "idle base_x");
  CHECK("stick_view_idle_base_y", std::abs(idle.base_y - 0.72f * kH) < 0.01f, "idle base_y");

  float r = 0.10f * kH;
  model.PointerDown(1, 0.20f * kW, 0.70f * kH, 0);
  model.PointerMove(1, 0.20f * kW + 0.5f * r, 0.70f * kH - 0.5f * r, 10);
  StickView active = model.Stick();
  CHECK("stick_view_active", active.active, "stick active after move");
  CHECK("stick_view_knob_x",
        active.knob_x >= active.base_x - active.radius - 0.01f &&
            active.knob_x <= active.base_x + active.radius + 0.01f,
        "knob_x in base +- R");
  CHECK("stick_view_knob_y",
        active.knob_y >= active.base_y - active.radius - 0.01f &&
            active.knob_y <= active.base_y + active.radius + 0.01f,
        "knob_y in base +- R");
}

}  // namespace

int main() {
  TestTapFireLatch();
  TestHoldFire();
  TestStickFullRight();
  TestStickHalfUp();
  TestStickClamped();
  TestStickNotInTopStrip();
  TestLookFastSwipe();
  TestLookSlowDrag();
  TestLookUpIsPositive();
  TestMultiTouchCombines();
  TestPointerKeepsItsButton();
  TestAimToggle();
  TestAimHoldDefault();
  TestReleaseAll();
  TestDpadUp();
  TestStartBack();
  TestScale();
  TestZeroViewport();
  TestFreshModelIsNeutral();
  TestSecondStickFingerInert();
  TestStickView();

  std::cout << "touch_model_test: " << g_checks << " checks, " << g_failures << " failed\n";
  return (g_failures == 0) ? 0 : 1;
}
