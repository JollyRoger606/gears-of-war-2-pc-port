// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Reads the "GOW2HOST {...}" line scripts/host-online.ps1 prints with -Json.
#pragma once

#include <optional>
#include <string_view>

#include "gow2_options_actions.h"

namespace rex::glue::host_line {

// Nullopt when the line has no "GOW2HOST " prefix or the JSON is malformed.
std::optional<options_actions::HostResult> ParseHostLine(std::string_view line);

}  // namespace rex::glue::host_line
