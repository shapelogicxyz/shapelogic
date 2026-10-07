// SPDX-License-Identifier: GPL-3.0-or-later
#include "bt/base/assert.h"

namespace bt::detail {

void assertionFailed(const char* expr, const char* msg, const char* file, int line) {
  std::string what = std::string("assertion failed: ") + expr;
  if (msg != nullptr) {
    what += " (";
    what += msg;
    what += ")";
  }
  what += " at ";
  what += file;
  what += ":";
  what += std::to_string(line);
  throw InternalError(what, file, line);
}

}  // namespace bt::detail
