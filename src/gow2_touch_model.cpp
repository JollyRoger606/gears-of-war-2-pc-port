// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors

#include "gow2_touch_model.h"

#include <algorithm>
#include <cmath>

namespace rex::glue::touch {

namespace {

constexpr float kStickZoneMaxX = 0.40f;
constexpr float kStickZoneMinY = 0.15f;
constexpr float kStickRadius = 0.10f;
constexpr float kStickIdleX = 0.17f;
constexpr float kStickIdleY = 0.72f;
constexpr float kHitSlop = 1.2f;
// A tap stays pressed until the game has read it, and at least this long.
constexpr int64_t kMinPressMs = 100;
// Longer than the slowest game frame measured on the phone (1.3 s); an older tap is dropped.
constexpr int64_t kMaxPendingMs = 2000;
// A reader that has not polled for this long (a finished thread) is forgotten.
constexpr int64_t kForgetConsumerMs = 10000;
// Polls closer than this are one frame reading the pad twice: they get the same look value.
constexpr int64_t kMinLookIntervalMs = 16;
constexpr int64_t kMaxLookIntervalMs = 1500;
constexpr float kLookFullSpeed = 1.0f;
constexpr float kLookDeadZone = 0.30f;
constexpr float kLookNoise = 0.02f;
constexpr float kLookSnapStartDeg = 8.0f;
constexpr float kLookSnapEndDeg = 16.0f;
constexpr float kDragSlop = 0.02f;
constexpr uint8_t kTriggerPressed = 128;

constexpr int16_t kAxisMax = 32767;
constexpr uint8_t kTriggerMax = 255;
constexpr float kMsPerSecond = 1000.0f;
constexpr float kRightAngleDeg = 90.0f;
constexpr float kHalfRightAngleDeg = 45.0f;
constexpr float kDegPerRad = 57.29577951308232f;

uint32_t ControlBit(ControlId id) {
  return 1u << static_cast<uint32_t>(id);
}

ControlId ControlAt(size_t index) {
  return static_cast<ControlId>(index);
}

uint16_t ButtonMask(ControlId id) {
  switch (id) {
    case ControlId::kA:
      return button::kA;
    case ControlId::kB:
      return button::kB;
    case ControlId::kX:
      return button::kX;
    case ControlId::kY:
      return button::kY;
    case ControlId::kRightShoulder:
      return button::kRightShoulder;
    case ControlId::kLeftShoulder:
      return button::kLeftShoulder;
    case ControlId::kDpadUp:
      return button::kDpadUp;
    case ControlId::kDpadDown:
      return button::kDpadDown;
    case ControlId::kDpadLeft:
      return button::kDpadLeft;
    case ControlId::kDpadRight:
      return button::kDpadRight;
    case ControlId::kStart:
      return button::kStart;
    case ControlId::kBack:
      return button::kBack;
    case ControlId::kRightThumb:
      return button::kRightThumb;
    default:
      return 0;
  }
}

bool IsReportedPressed(const PadState& reported, ControlId id) {
  switch (id) {
    case ControlId::kFire:
      return reported.right_trigger >= kTriggerPressed;
    case ControlId::kAim:
      return reported.left_trigger >= kTriggerPressed;
    default:
      return (reported.buttons & ButtonMask(id)) != 0;
  }
}

bool DragsTheCamera(ControlId id) {
  return id == ControlId::kFire || id == ControlId::kAim;
}

int FindNearestButton(float x, float y, float width, float height, float scale) {
  const auto& layout = DefaultLayout();
  int best_index = -1;
  float best_dist_sq = 0.0f;
  for (size_t i = 0; i < kControlCount; ++i) {
    const float max_dist = layout[i].radius * height * scale * kHitSlop;
    const float dx = x - layout[i].x * width;
    const float dy = y - layout[i].y * height;
    const float dist_sq = dx * dx + dy * dy;
    if (dist_sq > max_dist * max_dist) continue;
    if (best_index < 0 || dist_sq < best_dist_sq) {
      best_dist_sq = dist_sq;
      best_index = static_cast<int>(i);
    }
  }
  return best_index;
}

// 0 near an axis, the drag's own angle further out, and a ramp between them so the direction never
// jumps; d is the angle to the nearest axis.
float SnapAngleToAxis(float d) {
  if (d < kLookSnapStartDeg) return 0.0f;
  if (d < kLookSnapEndDeg) return 2.0f * (d - kLookSnapStartDeg);
  return d;
}

float ShapeLookMagnitude(float magnitude) {
  // The game's look response past its dead zone is convex; the square root keeps slow drags from
  // vanishing in it.
  return kLookDeadZone + (1.0f - kLookDeadZone) * std::sqrt(std::min(magnitude, 1.0f));
}

float SignOf(float value) {
  return value < 0.0f ? -1.0f : 1.0f;
}

int16_t QuantizeAxis(float normalized) {
  const float rounded = std::round(normalized * static_cast<float>(kAxisMax));
  const int32_t clamped = std::clamp<int32_t>(static_cast<int32_t>(rounded), -kAxisMax, kAxisMax);
  return static_cast<int16_t>(clamped);
}

}  // namespace

const std::array<ControlSpec, kControlCount>& DefaultLayout() {
  static constexpr std::array<ControlSpec, kControlCount> kLayout = {{
      {ControlId::kFire, 0.90f, 0.60f, 0.085f, "RT"},
      {ControlId::kAim, 0.78f, 0.74f, 0.065f, "LT"},
      {ControlId::kA, 0.93f, 0.84f, 0.060f, "A"},
      {ControlId::kB, 0.83f, 0.88f, 0.055f, "B"},
      {ControlId::kX, 0.72f, 0.88f, 0.055f, "X"},
      {ControlId::kY, 0.93f, 0.42f, 0.055f, "Y"},
      {ControlId::kRightShoulder, 0.80f, 0.46f, 0.055f, "RB"},
      {ControlId::kLeftShoulder, 0.07f, 0.12f, 0.055f, "LB"},
      {ControlId::kDpadUp, 0.15f, 0.28f, 0.045f, "^"},
      {ControlId::kDpadDown, 0.15f, 0.48f, 0.045f, "v"},
      {ControlId::kDpadLeft, 0.10f, 0.38f, 0.045f, "<"},
      {ControlId::kDpadRight, 0.20f, 0.38f, 0.045f, ">"},
      {ControlId::kStart, 0.53f, 0.06f, 0.040f, "Start"},
      {ControlId::kBack, 0.45f, 0.06f, 0.040f, "Back"},
      {ControlId::kRightThumb, 0.70f, 0.60f, 0.040f, "R3"},
      {ControlId::kSettings, 0.965f, 0.07f, 0.035f, "Settings"},
  }};
  return kLayout;
}

void ShapeLook(float raw_x, float raw_y, float* out_x, float* out_y) {
  const float magnitude = std::hypot(raw_x, raw_y);
  if (magnitude < kLookNoise) {
    *out_x = 0.0f;
    *out_y = 0.0f;
    return;
  }
  const float shaped = ShapeLookMagnitude(magnitude);
  const float angle = std::atan2(std::abs(raw_y), std::abs(raw_x)) * kDegPerRad;
  const float to_axis = std::min(angle, kRightAngleDeg - angle);
  const float snapped = SnapAngleToAxis(to_axis);
  const float out_angle = angle <= kHalfRightAngleDeg ? snapped : kRightAngleDeg - snapped;
  *out_x = SignOf(raw_x) * shaped * std::cos(out_angle / kDegPerRad);
  *out_y = SignOf(raw_y) * shaped * std::sin(out_angle / kDegPerRad);
}

void TouchModel::SetViewport(float width, float height) {
  width_ = width;
  height_ = height;
}

void TouchModel::SetSettings(const Settings& settings) {
  settings_ = settings;
}

TouchModel::TrackedPointer* TouchModel::FindPointer(uint32_t pointer) {
  auto it = std::find_if(pointers_.begin(), pointers_.end(),
                         [pointer](const TrackedPointer& p) { return p.id == pointer; });
  return it == pointers_.end() ? nullptr : &*it;
}

void TouchModel::TrackPointer(uint32_t pointer, PointerTarget target, ControlId button_id, float x,
                              float y, int64_t now_ms) {
  pointers_.push_back(TrackedPointer{
      .id = pointer,
      .target = target,
      .button_id = button_id,
      .down_x = x,
      .down_y = y,
      .down_ms = now_ms,
      .last_x = x,
      .last_y = y,
  });
}

void TouchModel::PointerDown(uint32_t pointer, float x, float y, int64_t now_ms) {
  if (width_ <= 0.0f || height_ <= 0.0f) {
    return;
  }
  if (FindPointer(pointer)) {
    PointerUp(pointer, now_ms);
  }
  const int index = FindNearestButton(x, y, width_, height_, settings_.scale);
  if (index >= 0) {
    PressControl(pointer, ControlAt(static_cast<size_t>(index)), x, y, now_ms);
    return;
  }
  TrackPointer(pointer, ClaimStickOrLook(x, y), ControlId::kFire, x, y, now_ms);
}

void TouchModel::PressControl(uint32_t pointer, ControlId id, float x, float y, int64_t now_ms) {
  if (id == ControlId::kSettings) {
    settings_requested_ = true;
  } else if (id == ControlId::kAim && settings_.aim_toggle) {
    aim_toggle_flag_ = !aim_toggle_flag_;
  } else {
    RegisterPress(id, now_ms);
  }
  TrackPointer(pointer, PointerTarget::kButton, id, x, y, now_ms);
}

// A press every reader has to see once even when the finger lifts before its next poll. A reader
// that last saw this control pressed first has to see it released.
void TouchModel::RegisterPress(ControlId id, int64_t now_ms) {
  const bool was_held = IsHeld(id);
  const uint32_t bit = ControlBit(id);
  press_ms_[static_cast<size_t>(id)] = now_ms;
  for (ConsumerState& consumer : consumers_) {
    if (now_ms - consumer.last_poll_ms > kMaxPendingMs) continue;
    if (!was_held && (consumer.last_reported & bit)) consumer.rearm |= bit;
    consumer.pending |= bit;
  }
}

TouchModel::PointerTarget TouchModel::ClaimStickOrLook(float x, float y) {
  if (x < kStickZoneMaxX * width_) {
    if (y <= kStickZoneMinY * height_ || stick_active_) return PointerTarget::kNone;
    stick_active_ = true;
    stick_base_x_ = x;
    stick_base_y_ = y;
    stick_curr_x_ = x;
    stick_curr_y_ = y;
    return PointerTarget::kStick;
  }
  if (look_active_) return PointerTarget::kNone;
  look_active_ = true;
  return PointerTarget::kLook;
}

void TouchModel::IgnorePointer(uint32_t pointer, int64_t now_ms) {
  if (FindPointer(pointer)) {
    PointerUp(pointer, now_ms);
  }
  TrackPointer(pointer, PointerTarget::kNone, ControlId::kFire, 0.0f, 0.0f, now_ms);
}

void TouchModel::PointerMove(uint32_t pointer, float x, float y, int64_t now_ms) {
  TrackedPointer* ptr = FindPointer(pointer);
  if (!ptr) {
    return;
  }
  if (ptr->target == PointerTarget::kStick) {
    stick_curr_x_ = x;
    stick_curr_y_ = y;
  } else if (ptr->target == PointerTarget::kLook) {
    AddLookTravel(x - ptr->last_x, y - ptr->last_y, now_ms);
    ptr->last_x = x;
    ptr->last_y = y;
  } else if (ptr->target == PointerTarget::kButton && DragsTheCamera(ptr->button_id)) {
    MoveDraggableButton(*ptr, x, y, now_ms);
  }
}

// A finger on RT or LT that slides past a small slop also turns the camera, as in mobile shooters,
// so aiming, firing and looking need two fingers on the right side instead of three.
void TouchModel::MoveDraggableButton(TrackedPointer& ptr, float x, float y, int64_t now_ms) {
  if (ptr.dragging) {
    AddLookTravel(x - ptr.last_x, y - ptr.last_y, now_ms);
  } else if (std::hypot(x - ptr.down_x, y - ptr.down_y) > kDragSlop * height_) {
    ptr.dragging = true;
  } else {
    return;
  }
  ptr.last_x = x;
  ptr.last_y = y;
}

void TouchModel::AddLookTravel(float dx, float dy, int64_t now_ms) {
  look_travel_.push_back(LookTravel{++last_look_seq_, now_ms, dx, dy});
  const int64_t oldest = now_ms - kMaxLookIntervalMs;
  std::erase_if(look_travel_, [oldest](const LookTravel& step) { return step.ms < oldest; });
}

void TouchModel::PointerUp(uint32_t pointer, int64_t now_ms) {
  (void)now_ms;
  TrackedPointer* ptr = FindPointer(pointer);
  if (!ptr) {
    return;
  }
  if (ptr->target == PointerTarget::kStick) {
    stick_active_ = false;
  } else if (ptr->target == PointerTarget::kLook) {
    // Its travel stays until each reader's next poll, so a flick between two polls still turns.
    look_active_ = false;
  }
  std::erase_if(pointers_, [pointer](const TrackedPointer& p) { return p.id == pointer; });
}

void TouchModel::ReleaseAll() {
  pointers_.clear();
  for (ConsumerState& consumer : consumers_) {
    consumer.pending = 0;
    consumer.rearm = 0;
    consumer.last_look_rx = 0;
    consumer.last_look_ry = 0;
  }
  aim_toggle_flag_ = false;
  settings_requested_ = false;
  stick_active_ = false;
  stick_base_x_ = 0.0f;
  stick_base_y_ = 0.0f;
  stick_curr_x_ = 0.0f;
  stick_curr_y_ = 0.0f;
  look_active_ = false;
  look_travel_.clear();
}

void TouchModel::DropPendingInput() {
  for (ConsumerState& consumer : consumers_) {
    consumer.pending = 0;
    consumer.rearm = 0;
  }
  look_travel_.clear();
}

const TouchModel::ConsumerState* TouchModel::FindConsumer(ConsumerId id) const {
  auto it = std::find_if(consumers_.begin(), consumers_.end(),
                         [id](const ConsumerState& c) { return c.id == id; });
  return it == consumers_.end() ? nullptr : &*it;
}

TouchModel::ConsumerState& TouchModel::ConsumerFor(ConsumerId id) {
  auto it = std::find_if(consumers_.begin(), consumers_.end(),
                         [id](const ConsumerState& c) { return c.id == id; });
  if (it != consumers_.end()) return *it;
  consumers_.push_back(ConsumerState{.id = id, .look_seq = last_look_seq_});
  return consumers_.back();
}

void TouchModel::ForgetIdleConsumers(int64_t now_ms) {
  std::erase_if(consumers_, [now_ms](const ConsumerState& c) {
    return now_ms - c.last_poll_ms > kForgetConsumerMs;
  });
}

bool TouchModel::IsHeld(ControlId id) const {
  return std::any_of(pointers_.begin(), pointers_.end(), [id](const TrackedPointer& p) {
    return p.target == PointerTarget::kButton && p.button_id == id;
  });
}

bool TouchModel::IsPending(const ConsumerState& consumer, ControlId id, int64_t now_ms) const {
  if (!(consumer.pending & ControlBit(id))) return false;
  return now_ms - press_ms_[static_cast<size_t>(id)] <= kMaxPendingMs;
}

bool TouchModel::IsPendingForAnyone(ControlId id, int64_t now_ms) const {
  return std::any_of(consumers_.begin(), consumers_.end(), [&](const ConsumerState& consumer) {
    return IsPending(consumer, id, now_ms);
  });
}

bool TouchModel::IsPressedFor(const ConsumerState& consumer, ControlId id, int64_t now_ms) const {
  if (consumer.rearm & ControlBit(id)) return false;
  return IsHeld(id) || IsPending(consumer, id, now_ms);
}

void TouchModel::AddControl(PadState& state, ControlId id, bool pressed) const {
  if (id == ControlId::kFire) {
    if (pressed) state.right_trigger = kTriggerMax;
  } else if (id == ControlId::kAim) {
    const bool aiming = settings_.aim_toggle ? aim_toggle_flag_ : pressed;
    if (aiming) state.left_trigger = kTriggerMax;
  } else if (pressed) {
    state.buttons |= ButtonMask(id);
  }
}

void TouchModel::LeftStick(PadState& state) const {
  const float r = kStickRadius * height_ * settings_.scale;
  if (!stick_active_ || r <= 0.0f) {
    return;
  }
  float dx = (stick_curr_x_ - stick_base_x_) / r;
  float dy = (stick_curr_y_ - stick_base_y_) / r;
  const float len = std::hypot(dx, dy);
  if (len > 1.0f) {
    dx /= len;
    dy /= len;
  }
  state.thumb_lx = QuantizeAxis(dx);
  state.thumb_ly = QuantizeAxis(-dy);
}

// The look speed is the travel since the previous poll over the time since that poll, so every
// poll of a steady drag turns the camera however long the game's frames take.
void TouchModel::LookStick(const ConsumerState& consumer, int64_t now_ms, int16_t* rx,
                           int16_t* ry) const {
  if (now_ms - consumer.last_look_poll_ms < kMinLookIntervalMs) {
    *rx = consumer.last_look_rx;
    *ry = consumer.last_look_ry;
    return;
  }
  float travel_x = 0.0f;
  float travel_y = 0.0f;
  for (const LookTravel& step : look_travel_) {
    if (step.seq <= consumer.look_seq) continue;
    travel_x += step.dx;
    travel_y += step.dy;
  }
  const int64_t interval =
      std::clamp(now_ms - consumer.last_look_poll_ms, kMinLookIntervalMs, kMaxLookIntervalMs);
  const float seconds = static_cast<float>(interval) / kMsPerSecond;
  const float scale = settings_.look_gain / (height_ * seconds * kLookFullSpeed);
  float shaped_x = 0.0f;
  float shaped_y = 0.0f;
  ShapeLook(travel_x * scale, -travel_y * scale, &shaped_x, &shaped_y);
  *rx = QuantizeAxis(shaped_x);
  *ry = QuantizeAxis(shaped_y);
}

PadState TouchModel::Sample(ConsumerId consumer_id, int64_t now_ms) const {
  PadState state{};
  if (width_ <= 0.0f || height_ <= 0.0f) {
    return state;
  }
  const ConsumerState* known = FindConsumer(consumer_id);
  // A reader's first poll: it has seen no press and no travel yet.
  const ConsumerState fresh{.id = consumer_id, .look_seq = last_look_seq_};
  const ConsumerState& consumer = known ? *known : fresh;
  for (size_t i = 0; i < kControlCount; ++i) {
    const ControlId id = ControlAt(i);
    if (id == ControlId::kSettings) continue;
    AddControl(state, id, IsPressedFor(consumer, id, now_ms));
  }
  LeftStick(state);
  LookStick(consumer, now_ms, &state.thumb_rx, &state.thumb_ry);
  return state;
}

uint32_t TouchModel::ReportedBits(const PadState& reported) const {
  uint32_t bits = 0;
  for (size_t i = 0; i < kControlCount; ++i) {
    const ControlId id = ControlAt(i);
    if (id != ControlId::kSettings && IsReportedPressed(reported, id)) bits |= ControlBit(id);
  }
  return bits;
}

void TouchModel::ClearAcknowledgedPresses(ConsumerState& consumer, uint32_t reported_bits,
                                          int64_t now_ms) {
  for (size_t i = 0; i < kControlCount; ++i) {
    const uint32_t bit = ControlBit(ControlAt(i));
    if (!(consumer.pending & bit)) continue;
    const int64_t age = now_ms - press_ms_[i];
    const bool seen_long_enough = (reported_bits & bit) && age >= kMinPressMs;
    if (seen_long_enough || age > kMaxPendingMs) consumer.pending &= ~bit;
  }
}

void TouchModel::MarkLookRead(ConsumerState& consumer, const PadState& reported, int64_t now_ms) {
  if (now_ms - consumer.last_look_poll_ms < kMinLookIntervalMs) {
    return;
  }
  consumer.look_seq = last_look_seq_;
  consumer.last_look_poll_ms = now_ms;
  consumer.last_look_rx = reported.thumb_rx;
  consumer.last_look_ry = reported.thumb_ry;
}

void TouchModel::Acknowledge(ConsumerId consumer_id, const PadState& reported, int64_t now_ms) {
  ForgetIdleConsumers(now_ms);
  ConsumerState& consumer = ConsumerFor(consumer_id);
  const uint32_t reported_bits = ReportedBits(reported);
  consumer.rearm &= reported_bits;
  ClearAcknowledgedPresses(consumer, reported_bits, now_ms);
  consumer.last_reported = reported_bits;
  consumer.last_poll_ms = now_ms;
  MarkLookRead(consumer, reported, now_ms);
}

bool TouchModel::TakeSettingsRequest() {
  const bool requested = settings_requested_;
  settings_requested_ = false;
  return requested;
}

std::vector<ControlView> TouchModel::Controls(int64_t now_ms) const {
  const auto& layout = DefaultLayout();
  std::vector<ControlView> views;
  views.reserve(kControlCount);
  for (const ControlSpec& spec : layout) {
    const bool toggled_aim = spec.id == ControlId::kAim && settings_.aim_toggle;
    const bool pressed = toggled_aim ? aim_toggle_flag_
                                     : IsHeld(spec.id) || IsPendingForAnyone(spec.id, now_ms);
    views.push_back(ControlView{
        .id = spec.id,
        .x = spec.x * width_,
        .y = spec.y * height_,
        .radius = spec.radius * height_ * settings_.scale,
        .label = spec.label,
        .pressed = pressed,
    });
  }
  return views;
}

StickView TouchModel::Stick() const {
  const float r = kStickRadius * height_ * settings_.scale;
  if (!stick_active_) {
    const float idle_x = kStickIdleX * width_;
    const float idle_y = kStickIdleY * height_;
    return StickView{
        .active = false,
        .base_x = idle_x,
        .base_y = idle_y,
        .knob_x = idle_x,
        .knob_y = idle_y,
        .radius = r,
    };
  }

  float dx = 0.0f;
  float dy = 0.0f;
  if (r > 0.0f) {
    dx = (stick_curr_x_ - stick_base_x_) / r;
    dy = (stick_curr_y_ - stick_base_y_) / r;
    const float len = std::hypot(dx, dy);
    if (len > 1.0f) {
      dx /= len;
      dy /= len;
    }
  }

  return StickView{
      .active = true,
      .base_x = stick_base_x_,
      .base_y = stick_base_y_,
      .knob_x = stick_base_x_ + dx * r,
      .knob_y = stick_base_y_ + dy * r,
      .radius = r,
  };
}

bool TouchModel::HasPointers() const {
  return !pointers_.empty();
}

}  // namespace rex::glue::touch
