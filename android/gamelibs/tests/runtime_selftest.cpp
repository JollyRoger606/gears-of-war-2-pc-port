// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// On-device checks of the Android runtime's UTF-8 decoder and input lock (patch 0007); run by
// scripts/test-android-device.ps1.

#include <rex/input/device_assignment.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/string/utf8.h>
#include <rex/system/xtypes.h>
#include <rex/thread/fiber.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace rex::string;
// The X_STATUS_* and X_ERROR_* macros name these types unqualified.
using rex::X_RESULT;
using rex::X_STATUS;

int g_checks = 0;
int g_failures = 0;

void Report(const char* name, bool ok, const std::string& detail) {
  ++g_checks;
  if (ok) {
    std::printf("PASS %s\n", name);
  } else {
    ++g_failures;
    std::printf("FAIL %s: %s\n", name, detail.c_str());
  }
  std::fflush(stdout);
}

void Check(const char* name, const std::function<bool()>& body) {
  try {
    Report(name, body(), "wrong result");
  } catch (const std::exception& e) {
    Report(name, false, std::string("threw: ") + e.what());
  } catch (...) {
    Report(name, false, "threw a non-std exception");
  }
}

size_t ExpectedFnv1a(uint32_t code_point) {
  size_t hash = size_t(0xCBF29CE484222325ULL);
  const size_t prime = size_t(0x00000100000001B3ULL);
  for (int i = 0; i < 4; ++i) {
    hash ^= size_t((code_point >> (i * 8)) & 0xFF);
    hash *= prime;
  }
  return hash;
}

// A \x escape eats every hex digit after it, so a literal ending in one is split
// before a following hex digit ("a\xFF" "b", never "a\xFFb").
void CheckUtf8Counts() {
  Check("utf8 valid ascii",
        [] { return utf8_count(std::string_view("abc", 3)) == 3; });
  Check("utf8 valid two-byte",
        [] { return utf8_count(std::string_view("\xC3\xA9", 2)) == 1; });
  Check("utf8 invalid lead byte",
        [] { return utf8_count(std::string_view("a\xFF" "b", 3)) == 3; });
  Check("utf8 truncated sequence",
        [] { return utf8_count(std::string_view("\xE2\x82", 2)) == 2; });
  Check("utf8 overlong", [] { return utf8_count(std::string_view("\xC0\xAF", 2)) == 2; });
  Check("utf8 surrogate",
        [] { return utf8_count(std::string_view("\xED\xA0\x80", 3)) == 3; });
  Check("utf8 above U+10FFFF",
        [] { return utf8_count(std::string_view("\xF4\x90\x80\x80", 4)) == 4; });
  Check("utf8 lone continuation byte",
        [] { return utf8_count(std::string_view("\x80", 1)) == 1; });
}

void CheckUtf8Utf16() {
  Check("utf8 to_utf16 keeps the invalid byte", [] {
    return to_utf16(std::string_view("a\xFF" "b", 3)) ==
           std::u16string{u'a', char16_t(0x00FF), u'b'};
  });
  Check("utf8 to_utf16 valid three-byte", [] {
    return to_utf16(std::string_view("\xE2\x82\xAC", 3)) ==
           std::u16string{char16_t(0x20AC)};
  });
}

void CheckUtf8Hashes() {
  Check("utf8 hash of 0xFF equals hash of U+00FF", [] {
    return utf8_hash_fnv1a(std::string_view("\xFF", 1)) ==
           utf8_hash_fnv1a(std::string_view("\xC3\xBF", 2));
  });
  Check("utf8 hash of 0xFF is FNV-1a of code point 0xFF", [] {
    return utf8_hash_fnv1a(std::string_view("\xFF", 1)) == ExpectedFnv1a(0xFF);
  });
}

void CheckUtf8CaseOps() {
  Check("utf8 lower_ascii re-encodes the invalid byte", [] {
    return utf8_lower_ascii(std::string_view("A\xFF" "B", 3)) ==
           std::string("a\xC3\xBF" "b");
  });
  Check("utf8 equal_case with an invalid byte", [] {
    return utf8_equal_case(std::string_view("ABC\xFF", 4), std::string_view("abc\xFF", 4));
  });
  Check("utf8 starts_with_case with an invalid byte", [] {
    return utf8_starts_with_case(std::string_view("GAME:\\" "\xFF" "save", 11),
                                 std::string_view("game:\\", 6));
  });
}

void BusyWait(std::chrono::microseconds span) {
  const auto end = std::chrono::steady_clock::now() + span;
  while (std::chrono::steady_clock::now() < end) {
  }
}

class CountingDriver final : public rex::input::InputDriver {
 public:
  CountingDriver() : InputDriver(nullptr, 0) {}

  rex::X_STATUS Setup() override {
    Enter();
    Leave();
    return X_STATUS_SUCCESS;
  }

  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    Enter();
    rex::input::DeviceInfo first;
    first.id = static_cast<rex::input::DeviceId>(1);
    first.name = "selftest pad 1";
    first.synthetic = false;
    out.push_back(first);
    if ((enumerations_.fetch_add(1) & 1) != 0) {
      rex::input::DeviceInfo second;
      second.id = static_cast<rex::input::DeviceId>(2);
      second.name = "selftest pad 2";
      second.synthetic = false;
      out.push_back(second);
    }
    Leave();
  }

  rex::X_RESULT GetDeviceState(rex::input::DeviceId,
                               rex::input::X_INPUT_STATE* out_state) override {
    Enter();
    if (out_state != nullptr) {
      std::memset(out_state, 0, sizeof(*out_state));
    }
    Leave();
    return X_ERROR_SUCCESS;
  }

  rex::X_RESULT GetDeviceCapabilities(rex::input::DeviceId, uint32_t,
                                      rex::input::X_INPUT_CAPABILITIES* out_caps) override {
    Enter();
    if (out_caps != nullptr) {
      std::memset(out_caps, 0, sizeof(*out_caps));
    }
    Leave();
    return X_ERROR_SUCCESS;
  }

  rex::X_RESULT SetDeviceVibration(rex::input::DeviceId,
                                   rex::input::X_INPUT_VIBRATION*) override {
    Enter();
    Leave();
    return X_ERROR_SUCCESS;
  }

  rex::X_RESULT GetDeviceKeystroke(rex::input::DeviceId, uint32_t,
                                   rex::input::X_INPUT_KEYSTROKE*) override {
    Enter();
    Leave();
    return X_ERROR_EMPTY;
  }

  int max_inside() const { return max_inside_.load(); }
  uint64_t calls() const { return calls_.load(); }

 private:
  void Enter() {
    const int now = inside_.fetch_add(1) + 1;
    int observed = max_inside_.load();
    while (now > observed && !max_inside_.compare_exchange_weak(observed, now)) {
    }
    calls_.fetch_add(1);
    // Widen the race window: without the input lock two workers would meet here.
    BusyWait(std::chrono::microseconds(2));
  }

  void Leave() { inside_.fetch_sub(1); }

  std::atomic<int> inside_{0};
  std::atomic<int> max_inside_{0};
  std::atomic<uint64_t> calls_{0};
  std::atomic<uint64_t> enumerations_{0};
};

struct HammerResult {
  int max_inside;
  uint64_t calls;
};

void HammerWorker(rex::input::InputSystem* input, int worker) {
  for (int i = 0; i < 4000; ++i) {
    switch ((i + worker) % 4) {
      case 0: {
        rex::input::X_INPUT_STATE state{};
        input->GetState(0, &state);
        break;
      }
      case 1: {
        rex::input::X_INPUT_CAPABILITIES caps{};
        input->GetCapabilities(0, 0, &caps);
        break;
      }
      case 2: {
        rex::input::X_INPUT_VIBRATION vibration{};
        input->SetState(0, &vibration);
        break;
      }
      default: {
        rex::input::X_INPUT_KEYSTROKE keystroke{};
        input->GetKeystroke(0, 0, &keystroke);
        break;
      }
    }
  }
}

HammerResult RunInputHammer() {
  rex::input::InputSystem input(nullptr);
  auto driver = std::make_unique<CountingDriver>();
  CountingDriver* counting = driver.get();
  input.AddDriver(std::move(driver));
  input.SetDeviceAssignment(std::make_unique<rex::input::SharedAssignment>());

  std::vector<std::thread> workers;
  for (int w = 0; w < 8; ++w) {
    workers.emplace_back(HammerWorker, &input, w);
  }
  for (auto& worker : workers) {
    worker.join();
  }
  return HammerResult{counting->max_inside(), counting->calls()};
}

void CallDriverDirectly(CountingDriver* driver) {
  for (int i = 0; i < 2000; ++i) {
    rex::input::X_INPUT_STATE state{};
    driver->GetDeviceState(static_cast<rex::input::DeviceId>(1), &state);
  }
}

// The same workload without InputSystem: proves the instrument does see two threads at once.
int MaxInsideWithoutLock() {
  CountingDriver driver;
  std::vector<std::thread> workers;
  for (int w = 0; w < 8; ++w) {
    workers.emplace_back(CallDriverDirectly, &driver);
  }
  for (auto& worker : workers) {
    worker.join();
  }
  return driver.max_inside();
}

void CheckInstrumentSeesOverlap() {
  const int max_inside = MaxInsideWithoutLock();
  Report("input lock: the instrument sees overlapping callers without the lock", max_inside > 1,
         "max inside was " + std::to_string(max_inside));
}

void CheckInputLock() {
  HammerResult result{0, 0};
  try {
    result = RunInputHammer();
  } catch (const std::exception& e) {
    Report("input lock: one thread inside the driver at a time", false,
           std::string("threw: ") + e.what());
    Report("input lock: the driver was exercised", false, std::string("threw: ") + e.what());
    return;
  } catch (...) {
    Report("input lock: one thread inside the driver at a time", false,
           "threw a non-std exception");
    Report("input lock: the driver was exercised", false, "threw a non-std exception");
    return;
  }
  Report("input lock: one thread inside the driver at a time", result.max_inside == 1,
         "max inside was " + std::to_string(result.max_inside));
  Report("input lock: the driver was exercised", result.calls >= 32000,
         "only " + std::to_string(result.calls) + " driver calls");
}

constexpr size_t kFiberStackBytes = 256 * 1024;
using FpCalleeSaved = std::array<double, 8>;

// Leaves -1.0 in d8-d15 when it switches back, as fiber code that uses them would.
void ClobberFpCalleeSavedAndSwitchBack(void* main_fiber) {
  asm volatile(
      "fmov d8, #-1.0\n"
      "fmov d9, #-1.0\n"
      "fmov d10, #-1.0\n"
      "fmov d11, #-1.0\n"
      "fmov d12, #-1.0\n"
      "fmov d13, #-1.0\n"
      "fmov d14, #-1.0\n"
      "fmov d15, #-1.0\n" ::
          : "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15");
  rex::thread::Fiber::SwitchTo(static_cast<rex::thread::Fiber*>(main_fiber));
}

// Sets d8-d15 to 1.0-8.0, switches to `target` and back, and returns what d8-d15 hold then. One
// asm block, so the compiler cannot move its own values through those registers in between.
FpCalleeSaved SwitchWithFpCalleeSavedSet(rex::thread::Fiber* target) {
  void (*switch_to)(rex::thread::Fiber*) = &rex::thread::Fiber::SwitchTo;
  FpCalleeSaved after{};
  double* out = after.data();
  asm volatile(
      "fmov d8, #1.0\n"
      "fmov d9, #2.0\n"
      "fmov d10, #3.0\n"
      "fmov d11, #4.0\n"
      "fmov d12, #5.0\n"
      "fmov d13, #6.0\n"
      "fmov d14, #7.0\n"
      "fmov d15, #8.0\n"
      "mov x0, %[target]\n"
      "blr %[fn]\n"
      "stp d8, d9, [%[out]]\n"
      "stp d10, d11, [%[out], #16]\n"
      "stp d12, d13, [%[out], #32]\n"
      "stp d14, d15, [%[out], #48]\n"
      :
      : [target] "r"(target), [fn] "r"(switch_to), [out] "r"(out)
      : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13",
        "x14", "x15", "x16", "x17", "x30", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
        "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19", "v20",
        "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31", "memory",
        "cc");
  return after;
}

FpCalleeSaved FpCalleeSavedAfterFiberRoundTrip() {
  rex::thread::Fiber* main_fiber = rex::thread::Fiber::ConvertCurrentThread();
  rex::thread::Fiber* other = rex::thread::Fiber::Create(
      kFiberStackBytes, ClobberFpCalleeSavedAndSwitchBack, main_fiber);
  const FpCalleeSaved after = SwitchWithFpCalleeSavedSet(other);
  other->Destroy();
  main_fiber->Destroy();
  return after;
}

// AAPCS64 makes d8-d15 callee-saved, so a fiber switch must hand them back unchanged.
void CheckFiberSwitchKeepsFpRegisters() {
  Check("fiber switch keeps d8-d15", [] {
    FpCalleeSaved after{};
    std::thread worker([&after] { after = FpCalleeSavedAfterFiberRoundTrip(); });
    worker.join();
    return after == FpCalleeSaved{1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0};
  });
}

}  // namespace

int main() {
  CheckUtf8Counts();
  CheckUtf8Utf16();
  CheckUtf8Hashes();
  CheckUtf8CaseOps();
  CheckInstrumentSeesOverlap();
  CheckInputLock();
  CheckFiberSwitchKeepsFpRegisters();
  std::printf("runtime_selftest: %d checks, %d failed\n", g_checks, g_failures);
  std::fflush(stdout);
  return g_failures == 0 ? 0 : 1;
}
