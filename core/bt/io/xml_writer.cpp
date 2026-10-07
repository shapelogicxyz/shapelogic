// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from xmlWriter_c in src/tools/xml.cpp.

#include "bt/io/xml_writer.h"

#include "bt/base/assert.h"

namespace bt::io {

namespace {

void outString(std::string& out, std::string_view text) {
  for (const char c : text) {
    switch (c) {
      case '>':
        out += "&gt;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '"':
        out += "&quot;";
        break;
      case '&':
        out += "&amp;";
        break;
      case '\'':
        out += "&apos;";
        break;
      default:
        out += c;
        break;
    }
  }
}

}  // namespace

XmlWriter::XmlWriter() { out_ = "<?xml version=\"1.0\"?>\n"; }

void XmlWriter::newTag(std::string_view name) {
  BT_ASSERT_MSG(state_ != State::InContent, "a new element inside an element with text");

  if (state_ == State::InTag) {
    // finish the open start tag first
    out_ += ">\n";
  }

  out_.append(tagStack_.size(), ' ');
  tagStack_.emplace_back(name);
  out_ += '<';
  out_ += name;
  state_ = State::InTag;
}

void XmlWriter::newAttrib(std::string_view attrib, std::string_view value) {
  BT_ASSERT_MSG(state_ == State::InTag, "an attribute outside a start tag");
  out_ += ' ';
  out_ += attrib;
  out_ += "=\"";
  outString(out_, value);
  out_ += '"';
}

void XmlWriter::newAttribRaw(std::string_view attrib, std::string_view value) {
  BT_ASSERT_MSG(state_ == State::InTag, "an attribute outside a start tag");
  out_ += ' ';
  out_ += attrib;
  out_ += "=\"";
  out_ += value;
  out_ += '"';
}

void XmlWriter::endTag(std::string_view name) {
  BT_ASSERT_MSG(!tagStack_.empty(), "closing an element, but none is open");
  BT_ASSERT_MSG(name == tagStack_.back(), "closing an element with the wrong name");

  switch (state_) {
    case State::InTag:
      out_ += "/>\n";
      break;
    case State::Base:
      out_.append(tagStack_.size() - 1, ' ');
      out_ += "</";
      out_ += name;
      out_ += ">\n";
      break;
    case State::InContent:
      out_ += "</";
      out_ += name;
      out_ += ">\n";
      break;
  }

  tagStack_.pop_back();
  state_ = State::Base;
}

void XmlWriter::startContent() {
  BT_ASSERT_MSG(state_ != State::Base, "text outside an element, or after a child element");
  if (state_ == State::InTag) {
    out_ += '>';
  }
  state_ = State::InContent;
}

void XmlWriter::addContent(std::string_view text) {
  startContent();
  outString(out_, text);
}

void XmlWriter::addContentRaw(std::string_view text) {
  startContent();
  out_ += text;
}

void XmlWriter::writeElement(const XmlElement& e) {
  newTag(e.name);
  for (const XmlAttribute& a : e.attributes) {
    newAttrib(a.name, a.value);
  }
  if (e.children.empty()) {
    if (!e.text.empty()) {
      addContent(e.text);
    }
  } else {
    for (const XmlElement& c : e.children) {
      writeElement(c);
    }
  }
  endTag(e.name);
}

const std::string& XmlWriter::str() const {
  BT_ASSERT_MSG(tagStack_.empty(), "not all elements were closed");
  return out_;
}

}  // namespace bt::io
