// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A small XML writer that produces exactly the original's layout: the
/// header line, one space of indent per level, `<tag/>` for empty elements,
/// text right inside its element, and the five named escapes.
/// Ported from xmlWriter_c in src/tools/xml.{h,cpp}.

#include "shapelogic/model/xml_element.h"

#include <concepts>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sl::io {

/// Writes an XML document into a string. Misuse (an attribute after
/// content, an attribute given twice in a tag, closing the wrong tag, ...)
/// is a programming error and fails a SL_ASSERT.
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
  /// Writes ` name="` of an attribute of the open start tag, which has
  /// none of that name yet.
  void startAttrib(std::string_view attrib);
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
  /// Where the names of the open start tag's attributes stand in out_:
  /// (offset, length) each.
  std::vector<std::pair<std::size_t, std::size_t>> tagAttribs_;
};

}  // namespace sl::io
