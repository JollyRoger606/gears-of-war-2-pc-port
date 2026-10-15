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
#include <limits>
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

// The layout lists the controls in this order.
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
  kRightThumb,
  kSettings,
};
constexpr size_t kControlCount = 16;

// Who reads the pad: every guest thread that calls XamInputGetState, and the key-event reader, has
// to see every press itself (the game thread reads every slow frame, another thread every 16 ms).
using ConsumerId = uint64_t;

struct ControlSpec {
  ControlId id;
  float x;       // centre, fraction of the viewport width
  float y;       // centre, fraction of the viewport height
  float radius;  // fraction of the viewport height at scale 1
  const char* label;
};

const std::array<ControlSpec, kControlCount>& DefaultLayout();

// Turns a look speed (viewport heights per second, y up) into right-stick deflection.
void ShapeLook(float raw_x, float raw_y, float* out_x, float* out_y);

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
  void IgnorePointer(uint32_t pointer, int64_t now_ms);
  void PointerMove(uint32_t pointer, float x, float y, int64_t now_ms);
  void PointerUp(uint32_t pointer, int64_t now_ms);
  void ReleaseAll();
  void DropPendingInput();

  PadState Sample(ConsumerId consumer, int64_t now_ms) const;
  void Acknowledge(ConsumerId consumer, const PadState& reported, int64_t now_ms);
  bool TakeSettingsRequest();

  std::vector<ControlView> Controls(int64_t now_ms) const;
  StickView Stick() const;
  bool HasPointers() const;

 private:
  static constexpr int64_t kNeverMs = std::numeric_limits<int64_t>::min() / 4;

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
    float last_x = 0;
    float last_y = 0;
    bool dragging = false;
  };

  struct LookTravel {
    uint64_t seq = 0;
    int64_t ms = 0;
    float dx = 0;
    float dy = 0;
  };

  struct ConsumerState {
    ConsumerId id = 0;
    uint32_t pending = 0;
    uint32_t rearm = 0;
    uint32_t last_reported = 0;
    int64_t last_poll_ms = kNeverMs;
    uint64_t look_seq = 0;
    int64_t last_look_poll_ms = kNeverMs;
    int16_t last_look_rx = 0;
    int16_t last_look_ry = 0;
  };

  TrackedPointer* FindPointer(uint32_t pointer);
  void TrackPointer(uint32_t pointer, PointerTarget target, ControlId button_id, float x, float y,
                    int64_t now_ms);
  void PressControl(uint32_t pointer, ControlId id, float x, float y, int64_t now_ms);
  void RegisterPress(ControlId id, int64_t now_ms);
  PointerTarget ClaimStickOrLook(float x, float y);
  void MoveDraggableButton(TrackedPointer& ptr, float x, float y, int64_t now_ms);
  void AddLookTravel(float dx, float dy, int64_t now_ms);

  const ConsumerState* FindConsumer(ConsumerId id) const;
  ConsumerState& ConsumerFor(ConsumerId id);
  void ForgetIdleConsumers(int64_t now_ms);
  bool IsHeld(ControlId id) const;
  bool IsPending(const ConsumerState& consumer, ControlId id, int64_t now_ms) const;
  bool IsPendingForAnyone(ControlId id, int64_t now_ms) const;
  bool IsPressedFor(const ConsumerState& consumer, ControlId id, int64_t now_ms) const;
  void AddControl(PadState& state, ControlId id, bool pressed) const;
  void LeftStick(PadState& state) const;
  void LookStick(const ConsumerState& consumer, int64_t now_ms, int16_t* rx, int16_t* ry) const;
  uint32_t ReportedBits(const PadState& reported) const;
  void ClearAcknowledgedPresses(ConsumerState& consumer, uint32_t reported_bits, int64_t now_ms);
  void MarkLookRead(ConsumerState& consumer, const PadState& reported, int64_t now_ms);

  float width_ = 0;
  float height_ = 0;
  Settings settings_;

  std::vector<TrackedPointer> pointers_;
  std::array<int64_t, kControlCount> press_ms_{};
  std::vector<ConsumerState> consumers_;
  bool aim_toggle_flag_ = false;
  bool settings_requested_ = false;

  bool stick_active_ = false;
  float stick_base_x_ = 0;
  float stick_base_y_ = 0;
  float stick_curr_x_ = 0;
  float stick_curr_y_ = 0;

  bool look_active_ = false;
  std::vector<LookTravel> look_travel_;
  uint64_t last_look_seq_ = 0;
};

}  // namespace rex::glue::touch
