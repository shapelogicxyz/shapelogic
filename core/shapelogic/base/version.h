// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace sl {

/// The project version from CMake (the git tag's, cmake/ProjectVersion.cmake).
/// A function, so that only version.cpp sees the define and a new tag
/// recompiles that one file.
[[nodiscard]] std::string_view versionString() noexcept;

}  // namespace sl
