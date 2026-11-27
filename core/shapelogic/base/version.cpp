// SPDX-License-Identifier: GPL-3.0-or-later
// The one source file that sees SL_VERSION_STRING (core/CMakeLists.txt
// sets it on this file only), so a new tag recompiles nothing else.
#include "shapelogic/base/version.h"

namespace sl {

std::string_view versionString() noexcept { return SL_VERSION_STRING; }

}  // namespace sl
