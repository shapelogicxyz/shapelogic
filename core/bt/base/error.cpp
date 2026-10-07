// SPDX-License-Identifier: GPL-3.0-or-later
#include "bt/base/error.h"

namespace bt {

std::string_view toString(Errc e) noexcept {
  switch (e) {
    case Errc::InvalidArgument: return "invalid_argument";
    case Errc::NotFound: return "not_found";
    case Errc::ParseError: return "parse_error";
    case Errc::IoError: return "io_error";
    case Errc::Unsupported: return "unsupported";
    case Errc::InvalidState: return "invalid_state";
    case Errc::Internal: return "internal";
  }
  return "unknown";
}

}  // namespace bt
