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
constexpr float kDegPerRad = 57.29577951308232f;

// Readers: the key-event reader, the game thread, and a second thread that polls every 16 ms.
constexpr ConsumerId kKeystroke = 1;
constexpr ConsumerId kState = 2;
constexpr ConsumerId kFastThread = 4;

constexpr float kRtX = 0.90f * kW;
constexpr float kRtY = 0.60f * kH;
constexpr float kLtX = 0.78f * kW;
constexpr float kLtY = 0.74f * kH;
constexpr float kAX = 0.93f * kW;
constexpr float kAY = 0.84f * kH;
constexpr float kLookX = 0.60f * kW;
constexpr float kLookY = 0.30f * kH;

TouchModel MakeModel() {
  TouchModel model;
  model.SetViewport(kW, kH);
  return model;
}

PadState Poll(TouchModel& model, ConsumerId consumer, int64_t t) {
  const PadState pad = model.Sample(consumer, t);
  model.Acknowledge(consumer, pad, t);
  return pad;
}

PadState Poll(TouchModel& model, int64_t t) {
  return Poll(model, kState, t);
}

void Tap(TouchModel& model, float x, float y, int64_t down, int64_t up) {
  model.PointerDown(1, x, y, down);
  model.PointerUp(1, up);
}

bool HasA(const PadState& pad) {
  return (pad.buttons & button::kA) != 0;
}

int16_t ExpectedLook(float speed) {
  return static_cast<int16_t>(std::round((0.30f + 0.70f * std::sqrt(speed)) * 32767.0f));
}

float OutputAngleDeg(float in_deg) {
  float x = 0.0f;
  float y = 0.0f;
  ShapeLook(0.5f * std::cos(in_deg / kDegPerRad), 0.5f * std::sin(in_deg / kDegPerRad), &x, &y);
  return std::atan2(y, x) * kDegPerRad;
}

// Drags pointer `pointer` from (x, y) by (step_x, step_y) heights every 10 ms in (from, to], and
// polls the state consumer at every multiple of poll_every_ms in that range, in time order.
// Returns how many polls read thumb_rx outside expected +- tolerance.
int DragAndPoll(TouchModel& model, float step_x, int64_t from, int64_t to, int64_t poll_every_ms,
                int16_t expected, int tolerance) {
  float x = kLookX;
  int bad = 0;
  model.PointerDown(1, x, kLookY, from);
  for (int64_t t = from + 10; t <= to; t += 10) {
    x += step_x * kH;
    model.PointerMove(1, x, kLookY, t);
    if (t % poll_every_ms != 0) continue;
    const PadState pad = Poll(model, t);
    const bool off = std::abs(pad.thumb_rx - expected) > tolerance;
    if (pad.thumb_rx <= 0 || off || pad.thumb_ry != 0) ++bad;
  }
  return bad;
}

void TestLayoutOrder() {
  const auto& layout = DefaultLayout();
  CHECK("layout_size", layout.size() == kControlCount, "kControlCount entries");
  for (size_t i = 0; i < layout.size(); ++i) {
    CHECK("layout_order", layout[i].id == static_cast<ControlId>(i), "entry " + std::to_string(i));
  }
}

void TestTapUntilSampled() {
  TouchModel model = MakeModel();
  Poll(model, 0);
  Tap(model, kRtX, kRtY, 1000, 1020);
  CHECK("tap_until_sampled_seen", Poll(model, 1300).right_trigger == 255,
        "a slow poll still sees the tap");
  CHECK("tap_until_sampled_once", Poll(model, 1600).right_trigger == 0,
        "and the next one does not");
}

void TestTapMinPressFastPolls() {
  TouchModel model = MakeModel();
  Poll(model, 0);
  Tap(model, kAX, kAY, 1000, 1010);
  CHECK("tap_fast_1033", HasA(Poll(model, 1033)), "pressed at 1033");
  CHECK("tap_fast_1066", HasA(Poll(model, 1066)), "pressed at 1066");
  CHECK("tap_fast_1100", HasA(Poll(model, 1100)), "pressed at 1100");
  CHECK("tap_fast_1133", !HasA(Poll(model, 1133)), "released at 1133");
}

void TestHold() {
  TouchModel model = MakeModel();
  Poll(model, 0);
  model.PointerDown(1, kRtX, kRtY, 0);
  CHECK("hold_held", Poll(model, 500).right_trigger == 255, "RT held at 500");
  model.PointerUp(1, 600);
  CHECK("hold_released", Poll(model, 601).right_trigger == 0, "RT released at 601");
}

void TestTwoTapsBetweenPollsGetARelease() {
  TouchModel model = MakeModel();
  Poll(model, 0);
  Tap(model, kAX, kAY, 100, 110);
  CHECK("two_taps_first", HasA(Poll(model, 300)), "first tap seen");
  Tap(model, kAX, kAY, 350, 360);
  CHECK("two_taps_release", !HasA(Poll(model, 600)), "a release between the taps");
  CHECK("two_taps_second", HasA(Poll(model, 900)), "second tap seen");
  CHECK("two_taps_done", !HasA(Poll(model, 1200)), "then released");
}

void TestConsumersIndependent() {
  TouchModel model = MakeModel();
  Poll(model, kState, 0);
  Poll(model, kKeystroke, 0);
  Tap(model, kAX, kAY, 100, 110);
  CHECK("consumers_state_sees", HasA(Poll(model, kState, 300)), "state sees the tap");
  CHECK("consumers_state_done", !HasA(Poll(model, kState, 400)), "state saw it once");
  CHECK("consumers_keystroke_keeps", HasA(model.Sample(kKeystroke, 410)),
        "the keystroke reader still gets it");
}

void TestIdleConsumerGetsNoLatch() {
  TouchModel model = MakeModel();
  Poll(model, kState, 0);
  Tap(model, kAX, kAY, 3000, 3010);
  CHECK("idle_consumer", !HasA(model.Sample(kKeystroke, 3100)),
        "a reader that never polls gets no stale press");
}

void TestPendingExpires() {
  TouchModel model = MakeModel();
  Poll(model, 0);
  Tap(model, kAX, kAY, 100, 110);
  CHECK("pending_expires", !HasA(Poll(model, 2200)), "a tap older than 2 s is dropped");
}

void TestDropPending() {
  TouchModel model = MakeModel();
  Poll(model, 0);
  Tap(model, kAX, kAY, 100, 110);
  model.DropPendingInput();
  CHECK("drop_pending", !HasA(Poll(model, 300)), "dropped");
}

void TestRightThumbTap() {
  TouchModel model = MakeModel();
  Poll(model, 0);
  Tap(model, 0.70f * kW, 0.60f * kH, 100, 110);
  CHECK("r3_tap", (Poll(model, 500).buttons & button::kRightThumb) != 0, "R3 pressed");
}

bool SettingsPressed(const TouchModel& model, int64_t t) {
  for (const ControlView& view : model.Controls(t)) {
    if (view.id == ControlId::kSettings) return view.pressed;
  }
  return false;
}

void TestSettingsControl() {
  TouchModel model = MakeModel();
  Poll(model, 0);
  model.PointerDown(1, 0.965f * kW, 0.07f * kH, 100);
  CHECK("settings_request", model.TakeSettingsRequest(), "requested");
  CHECK("settings_request_once", !model.TakeSettingsRequest(), "only once");
  CHECK("settings_not_on_pad", Poll(model, 200) == PadState{}, "never reaches the pad");
  CHECK("settings_drawn_pressed", SettingsPressed(model, 200), "pressed while held");
  model.PointerUp(1, 300);
  CHECK("settings_drawn_released", !SettingsPressed(model, 300), "released after lift");
}

void TestIgnorePointer() {
  TouchModel model = MakeModel();
  Poll(model, 0);
  model.IgnorePointer(1, 10);
  model.PointerMove(1, kLookX, kLookY, 20);
  model.PointerMove(1, kLookX + 0.2f * kH, kLookY, 30);
  model.PointerMove(1, kRtX, kRtY, 40);
  CHECK("ignore_neutral", Poll(model, 300) == PadState{}, "drives nothing");
  CHECK("ignore_tracked", model.HasPointers(), "still tracked");
  model.PointerUp(1, 310);
  CHECK("ignore_lifted", !model.HasPointers(), "gone after lift");
}

void TestLookPolls300Ms() {
  TouchModel model = MakeModel();
  Poll(model, 0);
  const int bad = DragAndPoll(model, 0.005f, 0, 1800, 300, ExpectedLook(0.5f), 400);
  CHECK("look_polls_300ms", bad == 0, std::to_string(bad) + " polls off");
}

void TestLookPolls900Ms() {
  TouchModel model = MakeModel();
  Poll(model, 0);
  const int bad = DragAndPoll(model, 0.005f, 0, 1800, 900, ExpectedLook(0.5f), 400);
  CHECK("look_polls_900ms", bad == 0, std::to_string(bad) + " polls off");
}

void TestFlickBetweenPolls() {
  TouchModel model = MakeModel();
  Poll(model, 0);
  Poll(model, 300);
  model.PointerDown(1, kLookX, kLookY, 305);
  for (int i = 1; i <= 3; ++i) {
    model.PointerMove(1, kLookX + static_cast<float>(i) * 0.02f * kH, kLookY, 300 + i * 10);
  }
  model.PointerUp(1, 335);
  CHECK("flick_read", Poll(model, 600).thumb_rx > 0, "a flick between polls turns");
  CHECK("flick_spent", Poll(model, 900).thumb_rx == 0, "once");
}

void TestDoublePollRepeats() {
  TouchModel model = MakeModel();
  Poll(model, 0);
  DragAndPoll(model, 0.005f, 0, 290, 1000000, 0, 0);
  model.PointerMove(1, kLookX + 30 * 0.005f * kH, kLookY, 300);
  const PadState first = Poll(model, 300);
  CHECK("double_poll_first", first.thumb_rx > 0, "turns");
  CHECK("double_poll_repeat", Poll(model, 305).thumb_rx == first.thumb_rx,
        "same frame, same value");
  CHECK("double_poll_spent", Poll(model, 600).thumb_rx == 0, "no new travel");
}

void TestLookUpIsPositive() {
  TouchModel model = MakeModel();
  Poll(model, 0);
  model.PointerDown(1, kLookX, kLookY, 0);
  for (int t = 10; t <= 200; t += 10) {
    model.PointerMove(1, kLookX, kLookY - static_cast<float>(t) * 0.001f * kH, t);
  }
  CHECK("look_up_is_positive", Poll(model, 200).thumb_ry > 0, "thumb_ry > 0");
}

void TestShapeKeepsDirection() {
  for (float angle : {20.0f, 30.0f, 45.0f}) {
    const float out = OutputAngleDeg(angle);
    CHECK("shape_keeps_direction", std::abs(out - angle) <= 1.0f,
          std::to_string(angle) + " -> " + std::to_string(out));
  }
}

void TestShapeSnapsNearAxis() {
  float x = 0.0f;
  float y = 0.0f;
  ShapeLook(0.5f * std::cos(5.0f / kDegPerRad), 0.5f * std::sin(5.0f / kDegPerRad), &x, &y);
  CHECK("shape_snaps_near_axis", y == 0.0f && x > 0.0f, "5 degrees is horizontal");
}

void TestShapeIsSmooth() {
  float previous = OutputAngleDeg(0.0f);
  float worst = 0.0f;
  for (int angle = 1; angle <= 90; ++angle) {
    const float out = OutputAngleDeg(static_cast<float>(angle));
    worst = std::max(worst, std::abs(out - previous));
    previous = out;
  }
  CHECK("shape_is_smooth", worst <= 2.5f, "largest step " + std::to_string(worst));
}

void TestShapeMinimum() {
  float x = 1.0f;
  float y = 1.0f;
  ShapeLook(0.019f, 0.0f, &x, &y);
  CHECK("shape_below_noise", x == 0.0f && y == 0.0f, "noise is 0");
  ShapeLook(0.02f, 0.0f, &x, &y);
  CHECK("shape_minimum", std::abs(x - (0.30f + 0.70f * std::sqrt(0.02f))) <= 0.005f,
        "smallest deflection " + std::to_string(x));
}

void TestFastReaderLeavesTheTapForTheGame() {
  TouchModel model = MakeModel();
  Poll(model, kState, 0);
  Poll(model, kFastThread, 0);
  Tap(model, kAX, kAY, 1000, 1020);
  int fast_presses = 0;
  for (int64_t t = 1008; t <= 1296; t += 16) {
    if (HasA(Poll(model, kFastThread, t))) ++fast_presses;
  }
  CHECK("fast_reader_saw_tap", fast_presses > 0, "the fast reader sees the tap");
  CHECK("fast_reader_done", !HasA(Poll(model, kFastThread, 1312)), "and lets it go");
  CHECK("game_reader_sees_tap", HasA(Poll(model, kState, 1300)), "the game thread still sees it");
  CHECK("game_reader_done", !HasA(Poll(model, kState, 1670)), "once");
}

void TestGameReadsWholeDragWhileFastReaderPolls() {
  TouchModel model = MakeModel();
  Poll(model, kState, 0);
  Poll(model, kFastThread, 0);
  model.PointerDown(1, kLookX, kLookY, 0);
  int bad = 0;
  float x = kLookX;
  for (int64_t t = 1; t <= 1110; ++t) {
    if (t % 10 == 0) {
      x += 0.005f * kH;
      model.PointerMove(1, x, kLookY, t);
    }
    if (t % 16 == 0) Poll(model, kFastThread, t);
    if (t % 370 != 0) continue;
    const PadState pad = Poll(model, kState, t);
    if (std::abs(pad.thumb_rx - ExpectedLook(0.5f)) > 400) ++bad;
  }
  CHECK("game_reads_whole_drag", bad == 0, std::to_string(bad) + " game reads off");
}

void TestGameSeesFlickTheFastReaderRead() {
  TouchModel model = MakeModel();
  Poll(model, kState, 0);
  Poll(model, kState, 300);
  Poll(model, kFastThread, 300);
  model.PointerDown(1, kLookX, kLookY, 305);
  for (int i = 1; i <= 3; ++i) {
    model.PointerMove(1, kLookX + static_cast<float>(i) * 0.02f * kH, kLookY, 300 + i * 10);
    Poll(model, kFastThread, 302 + i * 10);
  }
  model.PointerUp(1, 335);
  Poll(model, kFastThread, 340);
  CHECK("game_sees_flick", Poll(model, kState, 600).thumb_rx > 0, "the game thread turns too");
  CHECK("fast_reader_flick_spent", Poll(model, kFastThread, 610).thumb_rx == 0,
        "the fast reader read it");
}

void TestButtonDragTurns(const char* name, float x, float y, bool fire) {
  TouchModel model = MakeModel();
  Poll(model, 0);
  model.PointerDown(1, x, y, 100);
  model.PointerMove(1, x + 0.01f * kH, y, 110);
  const PadState still = Poll(model, 300);
  const uint8_t trigger_still = fire ? still.right_trigger : still.left_trigger;
  CHECK(name, trigger_still == 255 && still.thumb_rx == 0, "inside the slop: held, no turn");
  for (int t = 310, i = 2; t <= 600; t += 10, ++i) {
    model.PointerMove(1, x + static_cast<float>(i) * 0.01f * kH, y, t);
  }
  const PadState dragged = Poll(model, 600);
  const uint8_t trigger_dragged = fire ? dragged.right_trigger : dragged.left_trigger;
  CHECK(name, trigger_dragged == 255 && dragged.thumb_rx > 0, "past the slop: held and turning");
}

void TestStickFullRight() {
  TouchModel model = MakeModel();
  const float r = 0.10f * kH;
  model.PointerDown(1, 0.20f * kW, 0.70f * kH, 0);
  model.PointerMove(1, 0.20f * kW + r, 0.70f * kH, 10);
  const PadState pad = Poll(model, 10);
  CHECK("stick_full_right_lx", pad.thumb_lx == 32767, "thumb_lx == 32767");
  CHECK("stick_full_right_ly", pad.thumb_ly == 0, "thumb_ly == 0");
}

void TestStickHalfUp() {
  TouchModel model = MakeModel();
  const float r = 0.10f * kH;
  model.PointerDown(1, 0.20f * kW, 0.70f * kH, 0);
  model.PointerMove(1, 0.20f * kW, 0.70f * kH - 0.5f * r, 10);
  const PadState pad = Poll(model, 10);
  CHECK("stick_half_up_lx", pad.thumb_lx == 0, "thumb_lx == 0");
  CHECK("stick_half_up_ly", pad.thumb_ly == 16384, "thumb_ly == 16384");
}

void TestStickClamped() {
  TouchModel model = MakeModel();
  const float r = 0.10f * kH;
  model.PointerDown(1, 0.20f * kW, 0.70f * kH, 0);
  model.PointerMove(1, 0.20f * kW + 3.0f * r, 0.70f * kH + 4.0f * r, 10);
  const PadState pad = Poll(model, 10);
  const int16_t expected_lx = static_cast<int16_t>(std::round(0.6f * 32767.0f));
  const int16_t expected_ly = static_cast<int16_t>(std::round(-0.8f * 32767.0f));
  CHECK("stick_clamped_lx", pad.thumb_lx == expected_lx, "thumb_lx clamped");
  CHECK("stick_clamped_ly", pad.thumb_ly == expected_ly, "thumb_ly clamped");
}

void TestStickNotInTopStrip() {
  TouchModel model = MakeModel();
  const float r = 0.10f * kH;
  model.PointerDown(1, 0.20f * kW, 0.05f * kH, 0);
  model.PointerMove(1, 0.20f * kW + r, 0.05f * kH, 10);
  CHECK("stick_not_in_top_strip", Poll(model, 10) == PadState{}, "all zero PadState");
}

void TestMultiTouchCombines() {
  TouchModel model = MakeModel();
  Poll(model, 0);
  const float r = 0.10f * kH;
  model.PointerDown(1, 0.20f * kW, 0.70f * kH, 0);
  model.PointerMove(1, 0.20f * kW + r, 0.70f * kH, 50);
  model.PointerDown(2, kLookX, kLookY, 0);
  for (int i = 1; i <= 5; ++i) {
    model.PointerMove(2, kLookX + static_cast<float>(i) * 0.02f * kH, kLookY, i * 10);
  }
  model.PointerDown(3, kRtX, kRtY, 0);
  model.PointerDown(4, kAX, kAY, 0);
  const PadState pad = Poll(model, 50);
  CHECK("multi_touch_lx", pad.thumb_lx > 0, "thumb_lx > 0");
  CHECK("multi_touch_rx", pad.thumb_rx > 0, "thumb_rx > 0");
  CHECK("multi_touch_rt", pad.right_trigger == 255, "right_trigger == 255");
  CHECK("multi_touch_a", HasA(pad), "buttons & kA");
}

void TestPointerKeepsItsButton() {
  TouchModel model = MakeModel();
  model.PointerDown(1, kAX, kAY, 0);
  model.PointerMove(1, 0.83f * kW, 0.88f * kH, 10);
  CHECK("pointer_keeps_its_button", Poll(model, 10).buttons == button::kA, "buttons == kA only");
}

void TestAimToggle() {
  TouchModel model = MakeModel();
  Settings settings;
  settings.aim_toggle = true;
  model.SetSettings(settings);
  Poll(model, 0);
  Tap(model, kLtX, kLtY, 0, 10);
  CHECK("aim_toggle_on", Poll(model, 500).left_trigger == 255, "left_trigger == 255");
  Tap(model, kLtX, kLtY, 1000, 1010);
  CHECK("aim_toggle_off", Poll(model, 1500).left_trigger == 0, "left_trigger == 0");
}

void TestAimHoldDefault() {
  TouchModel model = MakeModel();
  Poll(model, 0);
  model.PointerDown(1, kLtX, kLtY, 0);
  CHECK("aim_hold_held", Poll(model, 100).left_trigger == 255, "held");
  model.PointerUp(1, 200);
  CHECK("aim_hold_default", Poll(model, 201).left_trigger == 0, "left_trigger == 0 at t=201");
}

void TestReleaseAll() {
  TouchModel model = MakeModel();
  Settings settings;
  settings.aim_toggle = true;
  model.SetSettings(settings);
  Poll(model, 0);
  model.PointerDown(1, 0.20f * kW, 0.70f * kH, 0);
  model.PointerDown(2, kRtX, kRtY, 0);
  Tap(model, kLtX, kLtY, 0, 10);
  model.ReleaseAll();
  CHECK("release_all_state", Poll(model, 100) == PadState{}, "PadState should be default");
  CHECK("release_all_has_pointers", !model.HasPointers(), "HasPointers should be false");
}

void TestDpadUp() {
  TouchModel model = MakeModel();
  model.PointerDown(1, 0.15f * kW, 0.28f * kH, 0);
  CHECK("dpad_up", (Poll(model, 0).buttons & button::kDpadUp) != 0, "buttons & kDpadUp");
}

void TestStartBack() {
  TouchModel model = MakeModel();
  model.PointerDown(1, 0.53f * kW, 0.06f * kH, 0);
  CHECK("start_button", (Poll(model, 0).buttons & button::kStart) != 0, "buttons & kStart");
  model.PointerUp(1, 0);
  model.PointerDown(2, 0.45f * kW, 0.06f * kH, 1000);
  CHECK("back_button", (Poll(model, 1000).buttons & button::kBack) != 0, "buttons & kBack");
}

void TestScale() {
  TouchModel model = MakeModel();
  Settings settings;
  settings.scale = 1.5f;
  model.SetSettings(settings);
  const auto controls = model.Controls(0);
  const float expected_r = 0.085f * kH * 1.5f;
  CHECK("scale_count", controls.size() == kControlCount, "one view per control");
  CHECK("scale_radius", std::abs(controls[0].radius - expected_r) < 0.01f, "radius matches");
  model.PointerDown(1, kRtX + 1.1f * expected_r, kRtY, 0);
  CHECK("scale_hit_slop", Poll(model, 0).right_trigger == 255, "RT pressed inside slop");
}

void TestZeroViewport() {
  TouchModel model;
  model.SetViewport(0.0f, 0.0f);
  model.PointerDown(1, 100.0f, 100.0f, 0);
  CHECK("zero_viewport_has_pointers", !model.HasPointers(), "HasPointers is false");
  CHECK("zero_viewport_sample", Poll(model, 0) == PadState{}, "Sample is PadState{}");
}

void TestFreshModelIsNeutral() {
  TouchModel model = MakeModel();
  CHECK("fresh_model_neutral_at_zero", Poll(model, 0) == PadState{}, "nothing pressed at t=0");
  CHECK("fresh_model_neutral_later", Poll(model, 5000) == PadState{}, "nothing pressed later");
}

void TestSecondStickFingerInert() {
  TouchModel model = MakeModel();
  const float r = 0.10f * kH;
  model.PointerDown(1, 0.20f * kW, 0.70f * kH, 0);
  model.PointerMove(1, 0.20f * kW + r, 0.70f * kH, 10);
  model.PointerDown(2, 0.25f * kW, 0.70f * kH, 20);
  model.PointerMove(2, 0.25f * kW - r, 0.70f * kH, 30);
  CHECK("second_stick_inert", Poll(model, 30).thumb_lx == 32767, "thumb_lx follows pointer 1 only");
}

void TestStickView() {
  TouchModel model = MakeModel();
  const StickView idle = model.Stick();
  CHECK("stick_view_idle_active", !idle.active, "idle stick inactive");
  CHECK("stick_view_idle_base_x", std::abs(idle.base_x - 0.17f * kW) < 0.01f, "idle base_x");
  CHECK("stick_view_idle_base_y", std::abs(idle.base_y - 0.72f * kH) < 0.01f, "idle base_y");
  const float r = 0.10f * kH;
  model.PointerDown(1, 0.20f * kW, 0.70f * kH, 0);
  model.PointerMove(1, 0.20f * kW + 0.5f * r, 0.70f * kH - 0.5f * r, 10);
  const StickView active = model.Stick();
  CHECK("stick_view_active", active.active, "stick active after move");
  CHECK("stick_view_knob_x", std::abs(active.knob_x - active.base_x) <= active.radius + 0.01f,
        "knob_x in base +- R");
  CHECK("stick_view_knob_y", std::abs(active.knob_y - active.base_y) <= active.radius + 0.01f,
        "knob_y in base +- R");
}

}  // namespace

int main() {
  TestLayoutOrder();
  TestTapUntilSampled();
  TestTapMinPressFastPolls();
  TestHold();
  TestTwoTapsBetweenPollsGetARelease();
  TestConsumersIndependent();
  TestIdleConsumerGetsNoLatch();
  TestPendingExpires();
  TestDropPending();
  TestRightThumbTap();
  TestSettingsControl();
  TestIgnorePointer();
  TestLookPolls300Ms();
  TestLookPolls900Ms();
  TestFlickBetweenPolls();
  TestDoublePollRepeats();
  TestLookUpIsPositive();
  TestShapeKeepsDirection();
  TestShapeSnapsNearAxis();
  TestShapeIsSmooth();
  TestShapeMinimum();
  TestFastReaderLeavesTheTapForTheGame();
  TestGameReadsWholeDragWhileFastReaderPolls();
  TestGameSeesFlickTheFastReaderRead();
  TestButtonDragTurns("fire_drag_turns", kRtX, kRtY, true);
  TestButtonDragTurns("aim_drag_turns", kLtX, kLtY, false);
  TestStickFullRight();
  TestStickHalfUp();
  TestStickClamped();
  TestStickNotInTopStrip();
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
