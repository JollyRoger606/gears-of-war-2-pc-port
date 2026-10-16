// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Minimal TOML key editor: the options menu saves without dropping user comments.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rex::glue::toml_edit {

enum class ValueKind { kBool, kInt, kFloat, kString };

struct Assignment {
  std::string key;    // bare key: [A-Za-z0-9_-]+
  ValueKind kind;
  std::string value;  // kBool: "true"/"false"; kInt: decimal (optional leading '-');
                      // kFloat: decimal text like "1.5"; kString: raw text, NOT escaped
};

std::string FormatValue(ValueKind kind, std::string_view value);
std::string Upsert(std::string_view text, const std::vector<Assignment>& assignments);
std::string Remove(std::string_view text, const std::vector<std::string>& keys);
bool ReadFile(const std::filesystem::path& path, std::string* text);
bool WriteFileAtomic(const std::filesystem::path& path, std::string_view text, std::string* error);

}  // namespace rex::glue::toml_edit
