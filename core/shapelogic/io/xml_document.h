// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Reading an XML document into an XmlElement tree, and writing one back.

#include "shapelogic/base/error.h"
#include "shapelogic/model/xml_element.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace sl::io {

/// How much of a document the reader takes before it refuses it: a file is
/// untrusted, and a small one can ask for a tree of gigabytes ("<a/>" is
/// four bytes of a file that may inflate to 256 MiB, and an XmlElement of
/// more than a hundred). The defaults are past what a puzzle file within
/// the inflate limit holds; a test gives smaller ones.
struct XmlLimits {
  /// Elements in the whole document.
  std::size_t elements = 16'000'000;
  /// Attributes on one element.
  std::size_t attributes = 4096;
};

/// Parses a document into its root element, with the original parser's
/// rules (see xml_pull_parser.h). Reading stops at the end of the root
/// element: whatever follows is not looked at, as in the original. Text
/// standing between child elements is dropped. Errors are ParseError; a
/// document past `limits`, or nested deeper than the readers can follow,
/// is one.
[[nodiscard]] Result<XmlElement> parseXmlDocument(std::string_view text,
                                                  const XmlLimits& limits = {});

/// A document with `root` as its root element, in the writer's layout.
[[nodiscard]] std::string writeXmlDocument(const XmlElement& root);

}  // namespace sl::io
