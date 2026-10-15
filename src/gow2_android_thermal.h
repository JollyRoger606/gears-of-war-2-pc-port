// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
//
// The phone's GPU clock and thermal state, appended to the FRAMES lines of gow2_frame_log so a
// frame rate can be read against the clock it ran at. Each value reads "n/a" when Android does not
// let the app see it.

#pragma once

#if defined(__ANDROID__)

#include <string>

namespace rex::glue {

std::string DescribeGpuAndThermalState();

}  // namespace rex::glue

#endif
