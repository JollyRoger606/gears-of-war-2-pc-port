// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors

#include "gow2_android_thermal.h"

#if defined(__ANDROID__)

#include <dlfcn.h>

#include <cmath>
#include <fstream>
#include <optional>
#include <string>

#include <fmt/format.h>

namespace rex::glue {

namespace {

constexpr const char* kKgslDir = "/sys/class/kgsl/kgsl-3d0/";
constexpr long kHzPerMhz = 1'000'000;
// AThermal_getThermalHeadroom's forecast; 0 is the current headroom.
constexpr int kHeadroomForecastSeconds = 0;

using AcquireThermalManagerFn = void* (*)();
using GetThermalStatusFn = int (*)(void*);
using GetThermalHeadroomFn = float (*)(void*, int);

// AThermal_* exist from API 30 (headroom from 31); the app's minSdk is 29, so they are looked up.
struct ThermalApi {
  void* manager = nullptr;
  GetThermalStatusFn get_status = nullptr;
  GetThermalHeadroomFn get_headroom = nullptr;
};

ThermalApi LoadThermalApi() {
  ThermalApi api;
  void* android = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
  if (!android) return api;
  auto acquire =
      reinterpret_cast<AcquireThermalManagerFn>(dlsym(android, "AThermal_acquireManager"));
  api.get_status = reinterpret_cast<GetThermalStatusFn>(
      dlsym(android, "AThermal_getCurrentThermalStatus"));
  api.get_headroom = reinterpret_cast<GetThermalHeadroomFn>(
      dlsym(android, "AThermal_getThermalHeadroom"));
  api.manager = acquire ? acquire() : nullptr;
  return api;
}

const ThermalApi& Thermal() {
  static const ThermalApi api = LoadThermalApi();
  return api;
}

std::optional<long> ReadLeadingNumber(const std::string& path) {
  std::ifstream file(path);
  long value = 0;
  if (!(file >> value)) return std::nullopt;
  return value;
}

std::string KgslValue(const char* name, long divisor, const char* unit) {
  const std::optional<long> value = ReadLeadingNumber(std::string(kKgslDir) + name);
  if (!value) return "n/a";
  return fmt::format("{}{}", *value / divisor, unit);
}

// Android 16 on the S25 Ultra hides gpuclk from apps; devfreq and clock_mhz give the same clock.
std::string GpuClock() {
  const std::string kgsl_dir = kKgslDir;
  for (const char* hz_file : {"gpuclk", "devfreq/cur_freq"}) {
    if (const std::optional<long> hz = ReadLeadingNumber(kgsl_dir + hz_file)) {
      return fmt::format("{} MHz", *hz / kHzPerMhz);
    }
  }
  return KgslValue("clock_mhz", 1, " MHz");
}

std::string ThermalStatus() {
  const ThermalApi& api = Thermal();
  if (!api.manager || !api.get_status) return "n/a";
  return std::to_string(api.get_status(api.manager));
}

std::string ThermalHeadroom() {
  const ThermalApi& api = Thermal();
  if (!api.manager || !api.get_headroom) return "n/a";
  const float headroom = api.get_headroom(api.manager, kHeadroomForecastSeconds);
  if (std::isnan(headroom)) return "n/a";
  return fmt::format("{:.2f}", headroom);
}

}  // namespace

std::string DescribeGpuAndThermalState() {
  return fmt::format("gpu {} of {} busy {} pwrlevel {} thermal {} headroom {}", GpuClock(),
                     KgslValue("max_gpuclk", kHzPerMhz, " MHz"),
                     KgslValue("gpu_busy_percentage", 1, "%"),
                     KgslValue("thermal_pwrlevel", 1, ""), ThermalStatus(), ThermalHeadroom());
}

}  // namespace rex::glue

#endif
