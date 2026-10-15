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
constexpr int64_t kMinPressMs = 100;
constexpr int64_t kLookHoldMs = 60;
constexpr float kLookSmoothing = 0.5f;
constexpr float kLookFullSpeed = 2.0f;
constexpr float kLookDeadZone = 0.30f;
constexpr float kLookAxisNoise = 0.02f;
constexpr float kLookMinorAxisRatio = 0.35f;

constexpr int16_t kAxisMax = 32767;
constexpr uint8_t kTriggerMax = 255;
constexpr float kMsPerSecond = 1000.0f;

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
    default:
      return 0;
  }
}

int FindNearestButton(float x, float y, float width, float height, float scale) {
  const auto& layout = DefaultLayout();
  int best_index = -1;
  float best_dist_sq = 0.0f;
  for (size_t i = 0; i < kButtonControlCount; ++i) {
    float cx = layout[i].x * width;
    float cy = layout[i].y * height;
    float r = layout[i].radius * height * scale;
    float max_dist = r * kHitSlop;
    float dx = x - cx;
    float dy = y - cy;
    float dist_sq = dx * dx + dy * dy;
    if (dist_sq <= max_dist * max_dist) {
      if (best_index < 0 || dist_sq < best_dist_sq) {
        best_dist_sq = dist_sq;
        best_index = static_cast<int>(i);
      }
    }
  }
  return best_index;
}

float ShapeLookAxis(float val, float max_raw) {
  float abs_val = std::abs(val);
  if (abs_val < kLookAxisNoise || abs_val < kLookMinorAxisRatio * max_raw) {
    return 0.0f;
  }
  float sign = (val > 0.0f) ? 1.0f : -1.0f;
  float scaled = std::min(abs_val, 1.0f);
  return sign * (kLookDeadZone + (1.0f - kLookDeadZone) * scaled);
}

int16_t QuantizeAxis(float normalized) {
  float rounded = std::round(normalized * static_cast<float>(kAxisMax));
  int32_t clamped = std::clamp<int32_t>(static_cast<int32_t>(rounded), -kAxisMax, kAxisMax);
  return static_cast<int16_t>(clamped);
}

}  // namespace

const std::array<ControlSpec, kButtonControlCount>& DefaultLayout() {
  static constexpr std::array<ControlSpec, kButtonControlCount> kLayout = {{
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
  }};
  return kLayout;
}

void TouchModel::SetViewport(float width, float height) {
  width_ = width;
  height_ = height;
}

void TouchModel::SetSettings(const Settings& settings) {
  settings_ = settings;
}

void TouchModel::PointerDown(uint32_t pointer, float x, float y, int64_t now_ms) {
  if (width_ <= 0.0f || height_ <= 0.0f) {
    return;
  }
  auto it = std::find_if(pointers_.begin(), pointers_.end(),
                         [pointer](const TrackedPointer& p) { return p.id == pointer; });
  if (it != pointers_.end()) {
    PointerUp(pointer, now_ms);
  }

  PointerTarget target = PointerTarget::kNone;
  ControlId button_id = ControlId::kFire;

  int btn_idx = FindNearestButton(x, y, width_, height_, settings_.scale);
  if (btn_idx >= 0) {
    target = PointerTarget::kButton;
    button_id = DefaultLayout()[static_cast<size_t>(btn_idx)].id;
    if (button_id == ControlId::kAim && settings_.aim_toggle) {
      aim_toggle_flag_ = !aim_toggle_flag_;
    }
  } else if (x < kStickZoneMaxX * width_ && y > kStickZoneMinY * height_) {
    if (!stick_active_) {
      target = PointerTarget::kStick;
      stick_active_ = true;
      stick_pointer_id_ = pointer;
      stick_base_x_ = x;
      stick_base_y_ = y;
      stick_curr_x_ = x;
      stick_curr_y_ = y;
    }
  } else if (x >= kStickZoneMaxX * width_) {
    if (!look_active_) {
      target = PointerTarget::kLook;
      look_active_ = true;
      look_pointer_id_ = pointer;
      look_last_x_ = x;
      look_last_y_ = y;
      look_last_ms_ = now_ms;
      look_last_move_ms_ = now_ms;
      look_pending_dx_ = 0.0f;
      look_pending_dy_ = 0.0f;
      look_has_sample_ = false;
      look_vx_ = 0.0f;
      look_vy_ = 0.0f;
    }
  }

  pointers_.push_back(TrackedPointer{
      .id = pointer,
      .target = target,
      .button_id = button_id,
      .down_x = x,
      .down_y = y,
      .down_ms = now_ms,
  });
}

void TouchModel::PointerMove(uint32_t pointer, float x, float y, int64_t now_ms) {
  auto it = std::find_if(pointers_.begin(), pointers_.end(),
                         [pointer](const TrackedPointer& p) { return p.id == pointer; });
  if (it == pointers_.end()) {
    return;
  }

  if (it->target == PointerTarget::kStick) {
    stick_curr_x_ = x;
    stick_curr_y_ = y;
  } else if (it->target == PointerTarget::kLook) {
    look_pending_dx_ += (x - look_last_x_);
    look_pending_dy_ += (y - look_last_y_);
    look_last_x_ = x;
    look_last_y_ = y;

    int64_t dt_ms = now_ms - look_last_ms_;
    if (dt_ms <= 0 || height_ <= 0.0f) {
      return;
    }
    float dt_sec = static_cast<float>(dt_ms) / kMsPerSecond;
    float sample_vx = (look_pending_dx_ / height_) / dt_sec;
    float sample_vy = (look_pending_dy_ / height_) / dt_sec;
    look_pending_dx_ = 0.0f;
    look_pending_dy_ = 0.0f;

    if (!look_has_sample_) {
      look_vx_ = sample_vx;
      look_vy_ = sample_vy;
      look_has_sample_ = true;
    } else {
      look_vx_ = kLookSmoothing * sample_vx + (1.0f - kLookSmoothing) * look_vx_;
      look_vy_ = kLookSmoothing * sample_vy + (1.0f - kLookSmoothing) * look_vy_;
    }
    look_last_ms_ = now_ms;
    look_last_move_ms_ = now_ms;
  }
}

void TouchModel::PointerUp(uint32_t pointer, int64_t now_ms) {
  auto it = std::find_if(pointers_.begin(), pointers_.end(),
                         [pointer](const TrackedPointer& p) { return p.id == pointer; });
  if (it == pointers_.end()) {
    return;
  }

  if (it->target == PointerTarget::kButton) {
    int64_t min_until = it->down_ms + kMinPressMs;
    if (min_until > now_ms) {
      size_t idx = static_cast<size_t>(it->button_id);
      button_latch_until_[idx] = std::max(button_latch_until_[idx], min_until);
    }
  } else if (it->target == PointerTarget::kStick) {
    stick_active_ = false;
    stick_pointer_id_ = 0;
  } else if (it->target == PointerTarget::kLook) {
    look_active_ = false;
    look_pointer_id_ = 0;
    look_has_sample_ = false;
    look_vx_ = 0.0f;
    look_vy_ = 0.0f;
  }

  pointers_.erase(it);
}

void TouchModel::ReleaseAll() {
  pointers_.clear();
  button_latch_until_.fill(0);
  aim_toggle_flag_ = false;

  stick_active_ = false;
  stick_pointer_id_ = 0;
  stick_base_x_ = 0.0f;
  stick_base_y_ = 0.0f;
  stick_curr_x_ = 0.0f;
  stick_curr_y_ = 0.0f;

  look_active_ = false;
  look_pointer_id_ = 0;
  look_last_x_ = 0.0f;
  look_last_y_ = 0.0f;
  look_last_ms_ = 0;
  look_last_move_ms_ = 0;
  look_pending_dx_ = 0.0f;
  look_pending_dy_ = 0.0f;
  look_has_sample_ = false;
  look_vx_ = 0.0f;
  look_vy_ = 0.0f;
}

bool TouchModel::IsButtonPressed(size_t index, ControlId id, int64_t now_ms) const {
  if (now_ms < button_latch_until_[index]) {
    return true;
  }
  for (const auto& ptr : pointers_) {
    if (ptr.target == PointerTarget::kButton && ptr.button_id == id) {
      return true;
    }
  }
  return false;
}

PadState TouchModel::Sample(int64_t now_ms) const {
  PadState state{};
  if (width_ <= 0.0f || height_ <= 0.0f) {
    return state;
  }
  const auto& layout = DefaultLayout();

  for (size_t i = 0; i < kButtonControlCount; ++i) {
    ControlId id = layout[i].id;
    if (id == ControlId::kFire) {
      if (IsButtonPressed(i, id, now_ms)) {
        state.right_trigger = kTriggerMax;
      }
    } else if (id == ControlId::kAim) {
      if (settings_.aim_toggle) {
        if (aim_toggle_flag_) {
          state.left_trigger = kTriggerMax;
        }
      } else if (IsButtonPressed(i, id, now_ms)) {
        state.left_trigger = kTriggerMax;
      }
    } else if (IsButtonPressed(i, id, now_ms)) {
      state.buttons |= ButtonMask(id);
    }
  }

  if (stick_active_) {
    float r = kStickRadius * height_ * settings_.scale;
    if (r > 0.0f) {
      float dx = (stick_curr_x_ - stick_base_x_) / r;
      float dy = (stick_curr_y_ - stick_base_y_) / r;
      float len = std::hypot(dx, dy);
      if (len > 1.0f) {
        dx /= len;
        dy /= len;
      }
      state.thumb_lx = QuantizeAxis(dx);
      state.thumb_ly = QuantizeAxis(-dy);
    }
  }

  if (look_active_ && look_has_sample_ && (now_ms - look_last_move_ms_ <= kLookHoldMs)) {
    float raw_x = (look_vx_ / kLookFullSpeed) * settings_.look_gain;
    float raw_y = (-look_vy_ / kLookFullSpeed) * settings_.look_gain;
    float max_raw = std::max(std::abs(raw_x), std::abs(raw_y));
    float axis_x = ShapeLookAxis(raw_x, max_raw);
    float axis_y = ShapeLookAxis(raw_y, max_raw);
    state.thumb_rx = QuantizeAxis(axis_x);
    state.thumb_ry = QuantizeAxis(axis_y);
  }

  return state;
}

std::vector<ControlView> TouchModel::Controls(int64_t now_ms) const {
  const auto& layout = DefaultLayout();
  std::vector<ControlView> views;
  views.reserve(kButtonControlCount);
  for (size_t i = 0; i < kButtonControlCount; ++i) {
    const auto& spec = layout[i];
    bool pressed = false;
    if (spec.id == ControlId::kAim && settings_.aim_toggle) {
      pressed = aim_toggle_flag_;
    } else {
      pressed = IsButtonPressed(i, spec.id, now_ms);
    }
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
  float r = kStickRadius * height_ * settings_.scale;
  if (!stick_active_) {
    float idle_x = kStickIdleX * width_;
    float idle_y = kStickIdleY * height_;
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
    float len = std::hypot(dx, dy);
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
