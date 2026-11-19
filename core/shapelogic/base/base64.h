// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Standard base64, for bytes inside text: the engine's JSON and the images
/// of a .xmpuzzle file (decision 0038).

#include <optional>
#include <string>
#include <string_view>

namespace bt {

/// Standard base64 with padding.
[[nodiscard]] std::string base64Encode(std::string_view bytes);
/// Decodes standard base64 (padding optional, ASCII white space between the
/// characters ignored, so a file may wrap it); empty on bad input.
[[nodiscard]] std::optional<std::string> base64Decode(std::string_view text);

}  // namespace bt
