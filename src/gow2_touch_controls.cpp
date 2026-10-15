// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors

#include "gow2_touch_controls.h"
#include "gow2_touch_model.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <rex/ui/windowed_app_context.h>

#if defined(__ANDROID__)
#include <SDL3/SDL_events.h>
#define GOW2_TOUCH_DEFAULT "auto"
#else
#define GOW2_TOUCH_DEFAULT "off"
#endif
REXCVAR_DEFINE_STRING(gow2_touch, GOW2_TOUCH_DEFAULT, "input",
                      "On-screen touch controls: auto (on when the device has a touchscreen), on "
                      "or off")
    .allowed({"auto", "on", "off"});
#undef GOW2_TOUCH_DEFAULT
REXCVAR_DEFINE_DOUBLE(gow2_touch_opacity, 0.5, "input", "Opacity of the on-screen touch controls")
    .range(0.0, 1.0);
REXCVAR_DEFINE_DOUBLE(gow2_touch_look_gain, 1.0, "input",
                      "Camera speed for a drag on the right side of the screen")
    .range(0.1, 5.0);
REXCVAR_DEFINE_BOOL(gow2_touch_aim_toggle, false, "input",
                    "The touch aim button (LT) toggles instead of being held");
REXCVAR_DEFINE_DOUBLE(gow2_touch_scale, 1.0, "input", "Size of the on-screen touch controls")
    .range(0.5, 2.0);
REXCVAR_DEFINE_DOUBLE(gow2_touch_hide_seconds, 10.0, "input",
                      "Seconds the touch controls stay hidden after a controller, keyboard or "
                      "mouse is used; the next touch shows them again")
    .range(0.0, 600.0);

namespace rex::glue {

namespace {

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

touch::Settings SettingsFromCvars() {
  touch::Settings settings;
  settings.scale = static_cast<float>(REXCVAR_GET(gow2_touch_scale));
  settings.look_gain = static_cast<float>(REXCVAR_GET(gow2_touch_look_gain));
  settings.aim_toggle = REXCVAR_GET(gow2_touch_aim_toggle);
  return settings;
}

struct TouchViewport {
  float width = 0;
  float height = 0;
};

}  // namespace

class TouchControls {
 public:
  void SetViewport(float width, float height) {
    std::lock_guard<std::mutex> lock(mutex_);
    viewport_ = {width, height};
    model_.SetViewport(width, height);
  }

  TouchViewport Viewport() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return viewport_;
  }

  void HandleTouch(rex::ui::TouchEvent::Action action, uint32_t pointer, float x, float y) {
    const int64_t now = NowMs();
    std::lock_guard<std::mutex> lock(mutex_);
    model_.SetSettings(SettingsFromCvars());
    switch (action) {
      case rex::ui::TouchEvent::Action::kDown:
        model_.PointerDown(pointer, x, y, now);
        break;
      case rex::ui::TouchEvent::Action::kMove:
        model_.PointerMove(pointer, x, y, now);
        break;
      case rex::ui::TouchEvent::Action::kUp:
      case rex::ui::TouchEvent::Action::kCancel:
        model_.PointerUp(pointer, now);
        break;
    }
    last_touch_ms_.store(now, std::memory_order_relaxed);
  }

  touch::PadState Sample(int64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    model_.SetSettings(SettingsFromCvars());
    return model_.Sample(now);
  }

  std::vector<touch::ControlView> Controls(int64_t now) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return model_.Controls(now);
  }

  touch::StickView Stick() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return model_.Stick();
  }

  void ReleaseAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    model_.ReleaseAll();
  }

  void NoteRealInput(int64_t now) { last_real_input_ms_.store(now, std::memory_order_relaxed); }

  bool OverlayVisible(int64_t now) const {
    const int64_t real = last_real_input_ms_.load(std::memory_order_relaxed);
    if (real == 0 || real <= last_touch_ms_.load(std::memory_order_relaxed)) {
      return true;
    }
    const double hide_ms = REXCVAR_GET(gow2_touch_hide_seconds) * 1000.0;
    return static_cast<double>(now - real) >= hide_ms;
  }

 private:
  mutable std::mutex mutex_;
  touch::TouchModel model_;
  TouchViewport viewport_;
  std::atomic<int64_t> last_real_input_ms_{0};
  std::atomic<int64_t> last_touch_ms_{0};
};

namespace {

using rex::input::DeviceId;
using rex::input::DeviceInfo;
using rex::input::X_INPUT_CAPABILITIES;
using rex::input::X_INPUT_KEYSTROKE;
using rex::input::X_INPUT_STATE;
using rex::input::X_INPUT_VIBRATION;
using rex::ui::VirtualKey;

// Above the keyboard/mouse driver (0) and native mouse look (1), below the ImGui drawer (64),
// which never consumes touches.
constexpr size_t kTouchListenerZOrder = 32;
constexpr DeviceId kTouchDevice = static_cast<DeviceId>(0x544F4348);
constexpr int kRealStickThreshold = 16000;
constexpr uint8_t kTriggerKeyThreshold = 128;
constexpr uint32_t kLeftTriggerBit = 1u << 16;
constexpr uint32_t kRightTriggerBit = 1u << 17;

struct KeystrokeBit {
  uint32_t bit;
  VirtualKey key;
};

constexpr KeystrokeBit kKeystrokeBits[] = {
    {touch::button::kDpadUp, VirtualKey::kXInputPadDpadUp},
    {touch::button::kDpadDown, VirtualKey::kXInputPadDpadDown},
    {touch::button::kDpadLeft, VirtualKey::kXInputPadDpadLeft},
    {touch::button::kDpadRight, VirtualKey::kXInputPadDpadRight},
    {touch::button::kStart, VirtualKey::kXInputPadStart},
    {touch::button::kBack, VirtualKey::kXInputPadBack},
    {touch::button::kLeftThumb, VirtualKey::kXInputPadLThumbPress},
    {touch::button::kRightThumb, VirtualKey::kXInputPadRThumbPress},
    {touch::button::kLeftShoulder, VirtualKey::kXInputPadLShoulder},
    {touch::button::kRightShoulder, VirtualKey::kXInputPadRShoulder},
    {touch::button::kA, VirtualKey::kXInputPadA},
    {touch::button::kB, VirtualKey::kXInputPadB},
    {touch::button::kX, VirtualKey::kXInputPadX},
    {touch::button::kY, VirtualKey::kXInputPadY},
    {kLeftTriggerBit, VirtualKey::kXInputPadLTrigger},
    {kRightTriggerBit, VirtualKey::kXInputPadRTrigger},
};

uint32_t KeystrokeBits(const touch::PadState& pad) {
  uint32_t bits = pad.buttons;
  if (pad.left_trigger >= kTriggerKeyThreshold) bits |= kLeftTriggerBit;
  if (pad.right_trigger >= kTriggerKeyThreshold) bits |= kRightTriggerBit;
  return bits;
}

void FillKeystroke(VirtualKey key, uint16_t flags, X_INPUT_KEYSTROKE* out) {
  out->virtual_key = static_cast<uint16_t>(key);
  out->unicode = 0;
  out->flags = flags;
  // InputSystem stamps the guest user this device is assigned to.
  out->user_index = 0;
  out->hid_code = 0;
}

// One change per call, releases before presses, like the SDK's SDL driver.
bool NextKeystroke(uint32_t current, uint32_t& reported, X_INPUT_KEYSTROKE* out) {
  for (const KeystrokeBit& entry : kKeystrokeBits) {
    if ((reported & entry.bit) && !(current & entry.bit)) {
      reported &= ~entry.bit;
      FillKeystroke(entry.key, rex::input::X_INPUT_KEYSTROKE_KEYUP, out);
      return true;
    }
  }
  for (const KeystrokeBit& entry : kKeystrokeBits) {
    if (!(reported & entry.bit) && (current & entry.bit)) {
      reported |= entry.bit;
      FillKeystroke(entry.key, rex::input::X_INPUT_KEYSTROKE_KEYDOWN, out);
      return true;
    }
  }
  return false;
}

void FillState(const touch::PadState& pad, uint32_t packet_number, X_INPUT_STATE* out) {
  std::memset(out, 0, sizeof(*out));
  out->packet_number = packet_number;
  out->gamepad.buttons = pad.buttons;
  out->gamepad.left_trigger = pad.left_trigger;
  out->gamepad.right_trigger = pad.right_trigger;
  out->gamepad.thumb_lx = pad.thumb_lx;
  out->gamepad.thumb_ly = pad.thumb_ly;
  out->gamepad.thumb_rx = pad.thumb_rx;
  out->gamepad.thumb_ry = pad.thumb_ry;
}

void FillGamepadCapabilities(X_INPUT_CAPABILITIES* caps) {
  std::memset(caps, 0, sizeof(*caps));
  caps->type = 0x01;      // XINPUT_DEVTYPE_GAMEPAD
  caps->sub_type = 0x01;  // XINPUT_DEVSUBTYPE_GAMEPAD
  caps->gamepad.buttons = 0xFFFF;
  caps->gamepad.left_trigger = 0xFF;
  caps->gamepad.right_trigger = 0xFF;
  caps->gamepad.thumb_lx = static_cast<int16_t>(0x7FFF);
  caps->gamepad.thumb_ly = static_cast<int16_t>(0x7FFF);
  caps->gamepad.thumb_rx = static_cast<int16_t>(0x7FFF);
  caps->gamepad.thumb_ry = static_cast<int16_t>(0x7FFF);
}

#if defined(__ANDROID__)
bool IsRealGamepadInput(const SDL_Event& event) {
  if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) return true;
  return event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
         std::abs(static_cast<int>(event.gaxis.value)) > kRealStickThreshold;
}

// The SDK's SDL driver owns the pads; this watch only notices that one is in use.
bool SDLCALL RealGamepadEventWatch(void* userdata, SDL_Event* event) {
  if (event && IsRealGamepadInput(*event)) {
    static_cast<TouchControls*>(userdata)->NoteRealInput(NowMs());
  }
  return true;
}
#endif

class TouchInputDriver final : public rex::input::InputDriver,
                               public rex::ui::WindowInputListener,
                               public rex::ui::WindowListener {
 public:
  explicit TouchInputDriver(std::shared_ptr<TouchControls> touch)
      : InputDriver(nullptr, kTouchListenerZOrder), touch_(std::move(touch)) {}

  ~TouchInputDriver() override { Detach(); }

  X_STATUS Setup() override { return X_STATUS_SUCCESS; }

  void EnumerateDevices(std::vector<DeviceInfo>& out) override {
    DeviceInfo info;
    info.id = kTouchDevice;
    info.name = "Touch controls";
    // Synthetic devices feed guest user 0 next to the first real pad and never take its slot.
    info.synthetic = true;
    out.push_back(info);
  }

  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) override {
    if (id != kTouchDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    const touch::PadState pad = CurrentPad();
    std::lock_guard<std::mutex> lock(mutex_);
    if (!(pad == last_pad_)) {
      ++packet_number_;
      last_pad_ = pad;
    }
    if (out_state) FillState(pad, packet_number_, out_state);
    return X_ERROR_SUCCESS;
  }

  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t flags,
                                 X_INPUT_CAPABILITIES* out_caps) override {
    (void)flags;
    if (id != kTouchDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (out_caps) FillGamepadCapabilities(out_caps);
    return X_ERROR_SUCCESS;
  }

  X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) override {
    (void)vibration;
    return id == kTouchDevice ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  }

  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t flags, X_INPUT_KEYSTROKE* out) override {
    (void)flags;
    if (id != kTouchDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (!out) return X_ERROR_BAD_ARGUMENTS;
    const uint32_t current = KeystrokeBits(CurrentPad());
    std::lock_guard<std::mutex> lock(mutex_);
    return NextKeystroke(current, keystroke_bits_, out) ? X_ERROR_SUCCESS : X_ERROR_EMPTY;
  }

  void OnWindowAvailable(rex::ui::Window* window) override {
    if (!window || window_) return;
    window_ = window;
    window->AddInputListener(this, window_z_order());
    window->AddListener(this);
    UpdateViewport(window);
#if defined(__ANDROID__)
    SDL_AddEventWatch(&RealGamepadEventWatch, touch_.get());
#endif
    REXLOG_INFO("TOUCH driver attached to a {}x{} window", window->GetActualPhysicalWidth(),
                window->GetActualPhysicalHeight());
  }

  void OnTouchEvent(rex::ui::TouchEvent& e) override {
    if (e.target()) UpdateViewport(e.target());
    touch_->HandleTouch(e.action(), e.pointer_id(), e.x(), e.y());
    e.set_handled(true);
  }

  void OnKeyDown(rex::ui::KeyEvent&) override { touch_->NoteRealInput(NowMs()); }
  void OnMouseDown(rex::ui::MouseEvent&) override { touch_->NoteRealInput(NowMs()); }
  void OnMouseMove(rex::ui::MouseEvent&) override { touch_->NoteRealInput(NowMs()); }

  void OnClosing(rex::ui::UIEvent&) override { Detach(); }
  void OnLostFocus(rex::ui::UISetupEvent&) override { touch_->ReleaseAll(); }
  void OnMinimized(rex::ui::UIEvent&) override { touch_->ReleaseAll(); }
  void OnResize(rex::ui::UISetupEvent& e) override {
    if (e.target()) UpdateViewport(e.target());
  }

 private:
  touch::PadState CurrentPad() {
    // Neutral while one of the runtime's own overlays has the input.
    return is_active() ? touch_->Sample(NowMs()) : touch::PadState{};
  }

  void UpdateViewport(rex::ui::Window* window) {
    touch_->SetViewport(static_cast<float>(window->GetActualPhysicalWidth()),
                        static_cast<float>(window->GetActualPhysicalHeight()));
  }

  void Detach() {
    rex::ui::Window* window = window_;
    if (!window) return;
    window_ = nullptr;
    window->app_context().CallInUIThreadSynchronous([this, window] {
      window->RemoveInputListener(this);
      window->RemoveListener(this);
#if defined(__ANDROID__)
      SDL_RemoveEventWatch(&RealGamepadEventWatch, touch_.get());
#endif
    });
    touch_->ReleaseAll();
  }

  std::shared_ptr<TouchControls> touch_;
  // Written on the UI thread only.
  rex::ui::Window* window_ = nullptr;
  std::mutex mutex_;
  touch::PadState last_pad_;
  uint32_t packet_number_ = 0;
  uint32_t keystroke_bits_ = 0;
};

constexpr float kIdleFillAlpha = 0.18f;
constexpr float kPressedFillAlpha = 0.45f;
constexpr float kOutlineAlpha = 0.70f;
constexpr float kLabelAlpha = 0.90f;
constexpr float kIdleStickAlpha = 0.5f;
constexpr float kKnobRadius = 0.45f;
constexpr float kShortLabelSize = 0.70f;
constexpr float kLongLabelSize = 0.42f;
constexpr float kMinOutline = 1.5f;
constexpr float kOutlineRatio = 0.06f;

ImU32 White(float alpha) {
  const int a = std::clamp(static_cast<int>(alpha * 255.0f + 0.5f), 0, 255);
  return IM_COL32(255, 255, 255, a);
}

void DrawLabel(ImDrawList* list, ImVec2 centre, float radius, const char* text, float alpha) {
  ImFont* font = ImGui::GetFont();
  const float size = radius * (std::strlen(text) <= 2 ? kShortLabelSize : kLongLabelSize);
  const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
  const ImVec2 pos(centre.x - extent.x * 0.5f, centre.y - extent.y * 0.5f);
  list->AddText(font, size, pos, White(alpha * kLabelAlpha), text);
}

void DrawRing(ImDrawList* list, ImVec2 centre, float radius, float fill_alpha, float alpha) {
  list->AddCircleFilled(centre, radius, White(alpha * fill_alpha));
  list->AddCircle(centre, radius, White(alpha * kOutlineAlpha), 0,
                  std::max(kMinOutline, radius * kOutlineRatio));
}

void DrawButton(ImDrawList* list, const touch::ControlView& view, float k, float alpha) {
  const ImVec2 centre(view.x * k, view.y * k);
  const float radius = view.radius * k;
  DrawRing(list, centre, radius, view.pressed ? kPressedFillAlpha : kIdleFillAlpha, alpha);
  DrawLabel(list, centre, radius, view.label, alpha);
}

void DrawStick(ImDrawList* list, const touch::StickView& stick, float k, float alpha) {
  const ImVec2 base(stick.base_x * k, stick.base_y * k);
  const float radius = stick.radius * k;
  if (!stick.active) {
    DrawRing(list, base, radius, kIdleFillAlpha, alpha * kIdleStickAlpha);
    DrawLabel(list, base, radius, "Move", alpha * kIdleStickAlpha);
    return;
  }
  DrawRing(list, base, radius, kIdleFillAlpha, alpha);
  const ImVec2 knob(stick.knob_x * k, stick.knob_y * k);
  list->AddCircleFilled(knob, radius * kKnobRadius, White(alpha * kPressedFillAlpha));
}

class TouchOverlay final : public rex::ui::ImGuiDialog {
 public:
  TouchOverlay(rex::ui::ImGuiDrawer* drawer, std::shared_ptr<TouchControls> touch)
      : ImGuiDialog(drawer), touch_(std::move(touch)) {}

#if REX_PLATFORM_ANDROID
  // Redrawn with each guest frame; a repaint at the phone's 120 Hz cost the game GPU time.
  bool WantsContinuousPaint() const override { return false; }
#endif

 protected:
  void OnDraw(ImGuiIO& io) override {
    const int64_t now = NowMs();
    if (!touch_->OverlayVisible(now)) return;
    const TouchViewport viewport = touch_->Viewport();
    if (viewport.width <= 0) return;
    // ImGui draws in logical units, the model works in physical pixels.
    const float k = io.DisplaySize.x / viewport.width;
    const float alpha = std::clamp(static_cast<float>(REXCVAR_GET(gow2_touch_opacity)), 0.f, 1.f);
    // Behind every ImGui window, so the runtime's settings and console stay on top.
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    for (const touch::ControlView& view : touch_->Controls(now)) {
      DrawButton(list, view, k, alpha);
    }
    DrawStick(list, touch_->Stick(), k, alpha);
  }

 private:
  std::shared_ptr<TouchControls> touch_;
};

}  // namespace

std::shared_ptr<TouchControls> CreateTouchControls(bool has_touchscreen) {
  const std::string mode = REXCVAR_GET(gow2_touch);
  const bool on = mode == "on" || (mode == "auto" && has_touchscreen);
  REXLOG_INFO("TOUCH controls {} (gow2_touch={}, touchscreen {})", on ? "on" : "off", mode,
              has_touchscreen ? "present" : "absent");
  return on ? std::make_shared<TouchControls>() : nullptr;
}

void AddTouchDriver(rex::RuntimeConfig& config, std::shared_ptr<TouchControls> touch) {
  if (!touch) return;
  auto previous = config.input_factory;
  if (!previous) {
    previous = [](bool tool_mode) -> std::unique_ptr<rex::system::IInputSystem> {
      return rex::input::CreateDefaultInputSystem(tool_mode);
    };
  }
  config.input_factory = [previous = std::move(previous), touch = std::move(touch)](
                             bool tool_mode) -> std::unique_ptr<rex::system::IInputSystem> {
    std::unique_ptr<rex::system::IInputSystem> system = previous(tool_mode);
    if (system && !tool_mode) {
      // ReXApp's factory and CreateDefaultInputSystem both build a rex::input::InputSystem.
      static_cast<rex::input::InputSystem*>(system.get())
          ->AddDriver(std::make_unique<TouchInputDriver>(touch));
    }
    return system;
  };
}

std::unique_ptr<rex::ui::ImGuiDialog> CreateTouchOverlay(rex::ui::ImGuiDrawer* drawer,
                                                         std::shared_ptr<TouchControls> touch) {
  if (!drawer || !touch) return nullptr;
  return std::make_unique<TouchOverlay>(drawer, std::move(touch));
}

}  // namespace rex::glue
