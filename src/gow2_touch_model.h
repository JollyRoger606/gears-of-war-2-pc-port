// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
//
// On-screen touch controls as a pure model: the layout, which finger owns which control, and the
// virtual Xbox 360 pad the fingers produce. No SDK, SDL or ImGui types, so it is tested on any host
// (android/gamelibs/tests/touch_model_test.cpp). Coordinates are pixels of a landscape viewport,
// times are milliseconds of any monotonic clock.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rex::glue::touch {

namespace button {
constexpr uint16_t kDpadUp = 0x0001;
constexpr uint16_t kDpadDown = 0x0002;
constexpr uint16_t kDpadLeft = 0x0004;
constexpr uint16_t kDpadRight = 0x0008;
constexpr uint16_t kStart = 0x0010;
constexpr uint16_t kBack = 0x0020;
constexpr uint16_t kLeftThumb = 0x0040;
constexpr uint16_t kRightThumb = 0x0080;
constexpr uint16_t kLeftShoulder = 0x0100;
constexpr uint16_t kRightShoulder = 0x0200;
constexpr uint16_t kA = 0x1000;
constexpr uint16_t kB = 0x2000;
constexpr uint16_t kX = 0x4000;
constexpr uint16_t kY = 0x8000;
}  // namespace button

struct PadState {
  uint16_t buttons = 0;
  uint8_t left_trigger = 0;
  uint8_t right_trigger = 0;
  int16_t thumb_lx = 0;
  int16_t thumb_ly = 0;
  int16_t thumb_rx = 0;
  int16_t thumb_ry = 0;

  bool operator==(const PadState&) const = default;
};

enum class ControlId : uint8_t {
  kFire,
  kAim,
  kA,
  kB,
  kX,
  kY,
  kRightShoulder,
  kLeftShoulder,
  kDpadUp,
  kDpadDown,
  kDpadLeft,
  kDpadRight,
  kStart,
  kBack,
};
constexpr size_t kButtonControlCount = 14;

struct ControlSpec {
  ControlId id;
  float x;       // centre, fraction of the viewport width
  float y;       // centre, fraction of the viewport height
  float radius;  // fraction of the viewport height at scale 1
  const char* label;
};

const std::array<ControlSpec, kButtonControlCount>& DefaultLayout();

struct Settings {
  float scale = 1.0f;
  float look_gain = 1.0f;
  bool aim_toggle = false;
};

struct ControlView {
  ControlId id;
  float x;
  float y;
  float radius;
  const char* label;
  bool pressed;
};

struct StickView {
  bool active = false;
  float base_x = 0;
  float base_y = 0;
  float knob_x = 0;
  float knob_y = 0;
  float radius = 0;
};

class TouchModel {
 public:
  void SetViewport(float width, float height);
  void SetSettings(const Settings& settings);

  void PointerDown(uint32_t pointer, float x, float y, int64_t now_ms);
  void PointerMove(uint32_t pointer, float x, float y, int64_t now_ms);
  void PointerUp(uint32_t pointer, int64_t now_ms);
  void ReleaseAll();

  PadState Sample(int64_t now_ms) const;
  std::vector<ControlView> Controls(int64_t now_ms) const;
  StickView Stick() const;
  bool HasPointers() const;

 private:
  enum class PointerTarget : uint8_t {
    kNone,
    kButton,
    kStick,
    kLook,
  };

  struct TrackedPointer {
    uint32_t id = 0;
    PointerTarget target = PointerTarget::kNone;
    ControlId button_id = ControlId::kFire;
    float down_x = 0;
    float down_y = 0;
    int64_t down_ms = 0;
  };

  bool IsButtonPressed(size_t index, ControlId id, int64_t now_ms) const;

  float width_ = 0;
  float height_ = 0;
  Settings settings_;

  std::vector<TrackedPointer> pointers_;
  std::array<int64_t, kButtonControlCount> button_latch_until_{};
  bool aim_toggle_flag_ = false;

  bool stick_active_ = false;
  uint32_t stick_pointer_id_ = 0;
  float stick_base_x_ = 0;
  float stick_base_y_ = 0;
  float stick_curr_x_ = 0;
  float stick_curr_y_ = 0;

  bool look_active_ = false;
  uint32_t look_pointer_id_ = 0;
  float look_last_x_ = 0;
  float look_last_y_ = 0;
  int64_t look_last_ms_ = 0;
  int64_t look_last_move_ms_ = 0;
  float look_pending_dx_ = 0;
  float look_pending_dy_ = 0;
  bool look_has_sample_ = false;
  float look_vx_ = 0;
  float look_vy_ = 0;
};

}  // namespace rex::glue::touch
