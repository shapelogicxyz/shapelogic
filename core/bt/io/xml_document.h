// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Reading an XML document into an XmlElement tree, and writing one back.

#include "bt/base/error.h"
#include "bt/model/xml_element.h"

#include <string>
#include <string_view>

namespace bt::io {

/// Parses a document into its root element, with the original parser's
/// rules (see xml_pull_parser.h). Reading stops at the end of the root
/// element: whatever follows is not looked at, as in the original. Text
/// standing between child elements is dropped. Errors are ParseError.
[[nodiscard]] Result<XmlElement> parseXmlDocument(std::string_view text);

/// A document with `root` as its root element, in the writer's layout.
[[nodiscard]] std::string writeXmlDocument(const XmlElement& root);

}  // namespace bt::io
