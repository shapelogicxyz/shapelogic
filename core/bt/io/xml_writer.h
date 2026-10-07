// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A small XML writer that produces exactly the original's layout: the
/// header line, one space of indent per level, `<tag/>` for empty elements,
/// text right inside its element, and the five named escapes.
/// Ported from xmlWriter_c in src/tools/xml.{h,cpp}.

#include "bt/model/xml_element.h"

#include <concepts>
#include <string>
#include <string_view>
#include <vector>

namespace bt::io {

/// Writes an XML document into a string. Misuse (an attribute after
/// content, closing the wrong tag, ...) is a programming error and fails a
/// BT_ASSERT.
class XmlWriter {
 public:
  /// Starts the document with the `<?xml version="1.0"?>` line.
  XmlWriter();

  /// Opens an element.
  void newTag(std::string_view name);

  /// Adds an attribute to the element just opened.
  void newAttrib(std::string_view attrib, std::string_view value);
  template <std::integral T>
  void newAttrib(std::string_view attrib, T value) {
    newAttribRaw(attrib, std::to_string(value));
  }

  /// Closes the innermost element, as `<tag/>` when it got no content.
  void endTag(std::string_view name);

  /// Adds text to the innermost element (escaped).
  void addContent(std::string_view text);
  template <std::integral T>
  void addContent(T value) {
    addContentRaw(std::to_string(value));
  }

  /// Writes a whole kept element (see PreservedXml).
  void writeElement(const XmlElement& e);

  /// The document; every element must be closed.
  [[nodiscard]] const std::string& str() const;

 private:
  void newAttribRaw(std::string_view attrib, std::string_view value);
  void addContentRaw(std::string_view text);
  void startContent();

  enum class State {
    Base,       ///< after an element, or at the start
    InTag,      ///< inside an open start tag
    InContent,  ///< inside an element's text
  };

  std::string out_;
  std::vector<std::string> tagStack_;
  State state_ = State::Base;
};

}  // namespace bt::io
