// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace bt {

/// The project version from CMakeLists.txt.
inline constexpr std::string_view kVersion = BT_VERSION_STRING;

}  // namespace bt
