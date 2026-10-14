// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The original's pull XML parser, kept so that files are read exactly as
/// the original reads them (entities, text merged across comments and
/// CDATA, \r\n handling, its error messages). Internal to bt/io: use
/// parseXmlDocument() (xml_document.h). It throws XmlParseError, which the
/// io functions turn into Result errors; it never leaves the module
/// (decision 0008).
/// Ported from xmlParser_c in src/tools/xml.{h,cpp}, itself from kXML2 via
/// wsdlpull (MIT licence, see the .cpp).

#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace bt::io {

/// A malformed document.
class XmlParseError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

/// A pull parser over a document in memory. Bytes are read as unsigned
/// (0..255) on every platform.
class XmlPullParser {
 public:
  /// The events.
  enum EventType {
    START_DOCUMENT,
    END_DOCUMENT,
    START_TAG,  ///< an open tag (an empty-element tag too)
    END_TAG,    ///< the end of an element (a closing tag, or after an empty-element tag)
    TEXT,       ///< text
    CDSECT,
    ENTITY_REF,
    IGNORABLE_WHITESPACE,
    PROCESSING_INSTRUCTION,
    COMMENT,
    DOCDECL,
    UNKNOWN
  };

  explicit XmlPullParser(std::string_view input);

  /// Adds an entity besides the five predefined ones.
  void defineEntityReplacementText(const std::string& entity, const std::string& value);

  [[nodiscard]] int getDepth() const { return depth_; }
  [[nodiscard]] int getLineNumber() const { return line_; }

  /// True when the current text event is only whitespace.
  bool isWhitespace();

  /// The text of the current text event.
  [[nodiscard]] std::string getText() const;

  /// The name of the current tag.
  [[nodiscard]] const std::string& getName() const { return name_; }

  /// True for an empty-element tag (`<a/>`).
  bool isEmptyElementTag();

  [[nodiscard]] int getAttributeCount() const { return attributeCount_; }
  std::string getAttributeName(int index);
  std::string getAttributeValue(int index);
  /// The value of an attribute, or "" when it is absent.
  [[nodiscard]] std::string getAttributeValue(const std::string& name) const;

  [[nodiscard]] int getEventType() const { return type_; }

  /// Moves to the next event: START_TAG, END_TAG, TEXT (merged over entity
  /// references, comments, processing instructions and CDATA) or
  /// END_DOCUMENT.
  int next();
  /// Moves to the next token, without merging.
  int nextToken();
  /// next(), skipping whitespace text; anything but a tag is an error.
  int nextTag();
  /// Makes the following nextTag() return the current tag again.
  void prevTag() { skipNextTag_ = true; }

  /// Throws unless at an event of `type` (and, when given, named `name`).
  void require(int type, const std::string& name);

  /// The text of the current element, which must have no child elements.
  std::string nextText();

  /// From a START_TAG, moves to its END_TAG.
  void skipSubTree();

  /// Throws XmlParseError with the current position.
  [[noreturn]] void exception(const std::string& desc);

 private:
  static std::string stateName(int eventType);

  void nextImpl();
  int parseLegacy(bool push);
  void parseDoctype(bool push);
  void parseEndTag();
  int peekType();
  [[nodiscard]] std::string get(int pos) const;
  void push(int c);
  void parseStartTag(bool xmldecl);
  void pushEntity();
  void pushText(int delimiter, bool resolveEntities);
  void read(char c);
  int read();
  int peekbuf(int pos);
  std::string readName();
  void skip();

  static constexpr int kLegacy = 999;
  static constexpr int kXmlDecl = 998;

  std::string_view input_;
  std::size_t srcPos_ = 0;

  bool relaxed_ = false;
  std::map<std::string, std::string> entityMap_;
  int depth_ = 0;
  std::vector<std::string> elementStack_;

  std::string version_;
  std::string encoding_;
  bool standalone_ = false;

  int line_ = 1;
  int column_ = 0;

  std::vector<char> txtBuf_;
  int txtPos_ = 0;

  int type_ = START_DOCUMENT;
  bool isWspace_ = false;
  bool skipNextTag_ = false;
  std::string name_;
  bool degenerated_ = false;
  int attributeCount_ = -1;
  std::vector<std::string> attributes_;

  int peek_[2] = {0, 0};
  int peekCount_ = 0;
  bool wasCR_ = false;
  bool unresolved_ = false;
  bool token_ = false;
};

}  // namespace bt::io
