// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A small XML tree, and the file content the model keeps without
/// understanding it. The .xmpuzzle reader (bt/io) parses a file into an
/// XmlElement tree; the model objects keep the elements and attributes they
/// do not model in a PreservedXml, so they are written back unchanged
/// (decisions 0006 and 0008).

#include <string>
#include <string_view>
#include <vector>

namespace bt {

/// One attribute, as it appeared in the file (entities resolved).
struct XmlAttribute {
  std::string name;
  std::string value;

  bool operator==(const XmlAttribute&) const = default;
};

/// One element. The format has no mixed content: an element holds either
/// child elements or text. Text that stands between child elements is not
/// kept.
struct XmlElement {
  std::string name;
  std::vector<XmlAttribute> attributes;  ///< in file order
  std::vector<XmlElement> children;      ///< in file order
  std::string text;                      ///< character data of a leaf element
  int line = 0;                          ///< line of the start tag, for messages

  /// The value of an attribute, or nullptr when it is absent.
  [[nodiscard]] const std::string* findAttribute(std::string_view n) const {
    for (const XmlAttribute& a : attributes) {
      if (a.name == n) {
        return &a.value;
      }
    }
    return nullptr;
  }

  /// The value of an attribute, or "" when it is absent (the original
  /// parser's getAttributeValue(): absent and empty are the same).
  [[nodiscard]] std::string attribute(std::string_view n) const {
    const std::string* v = findAttribute(n);
    return v != nullptr ? *v : std::string();
  }
};

/// What a model object read from its element but does not model: unknown
/// attributes, and unknown child elements with the place they go back to.
struct PreservedXml {
  /// An unknown child element. `slot` says where it is written: after the
  /// known children whose writer position is below `slot` and before the
  /// others (each writer numbers its known children in output order).
  struct Child {
    unsigned int slot = 0;
    XmlElement element;
  };

  std::vector<XmlAttribute> attributes;  ///< in file order
  std::vector<Child> children;           ///< in file order

  [[nodiscard]] bool empty() const { return attributes.empty() && children.empty(); }

  /// The first kept child with this name, or nullptr.
  [[nodiscard]] const XmlElement* findChild(std::string_view n) const {
    for (const Child& c : children) {
      if (c.element.name == n) {
        return &c.element;
      }
    }
    return nullptr;
  }
};

}  // namespace bt
