// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
//
// Which input devices are attached to the phone. SDL 3.4.14 keeps no keyboard or mouse list on
// Android, so Android itself is asked through JNI.

#pragma once

#if defined(__ANDROID__)

namespace rex::glue {

bool PhysicalKeyboardConnected();
bool ExternalMouseConnected();
bool TouchscreenPresent();

}  // namespace rex::glue

#endif
