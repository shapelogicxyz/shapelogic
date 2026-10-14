// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from xmlParser_c in src/tools/xml.cpp, which carries this notice:
//
// Copyright (c) 2005,2007 Vivek Krishna
//  Based on kxml2 by Stefan Haustein, Oberhausen, Rhld., Germany
//  Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The  above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//
// Differences from the original (see docs/handoff/stage-2.md): bytes are
// read as unsigned on every platform, and numeric character references
// above 127 are written as UTF-8 instead of being cut to one byte. The
// namespace bookkeeping, which the original kept but never used, is gone.

#include "shapelogic/io/xml_pull_parser.h"

#include <string>

namespace bt::io {

namespace {

const char* const kUnexpectedEof = "Unexpected EOF";
const char* const kIllegalType = "wrong Event Type";

/// The original's number parser for character references, quirks and all
/// (a hex "digit" is anything from NUL to '9').
int parseInt(const std::string& s, int radix) {
  const int len = static_cast<int>(s.size());
  int value = 0;
  if (s.empty()) {
    return -1;
  }
  for (int i = 0; i < len; i++) {
    const int c = static_cast<unsigned char>(s[static_cast<std::size_t>(i)]);
    if (radix == 10) {
      if (c <= '9' && c >= '0') {
        value = (i == 0) ? (c - '0') : radix * value + (c - '0');
      } else {
        return value;  // what is parsed so far
      }
    } else if (radix == 16) {
      if (c <= '9' && c >= 0) {
        value = (i == 0) ? (c - '0') : radix * value + (c - '0');
      } else if (c <= 'F' && c >= 'A') {
        value = (i == 0) ? (c - 'A') + 10 : radix * value + (c - 'A') + 10;
      } else if (c <= 'f' && c >= 'a') {
        value = (i == 0) ? (c - 'a') + 10 : radix * value + (c - 'a') + 10;
      }
    }
  }
  return value;
}

}  // namespace

XmlPullParser::XmlPullParser(std::string_view input) : input_(input) {
  entityMap_["apos"] = "'";
  entityMap_["gt"] = ">";
  entityMap_["lt"] = "<";
  entityMap_["quot"] = "\"";
  entityMap_["amp"] = "&";
}

std::string XmlPullParser::stateName(int eventType) {
  switch (eventType) {
    case START_DOCUMENT:
      return "START_DOCUMENT";
    case END_DOCUMENT:
      return "END_DOCUMENT";
    case START_TAG:
      return "START_TAG";
    case END_TAG:
      return "END_TAG";
    case TEXT:
      return "TEXT";
    case CDSECT:
      return "CDSECT";
    case ENTITY_REF:
      return "ENTITY_REF";
    case IGNORABLE_WHITESPACE:
      return "IGNORABLE_WHITESPACE";
    case PROCESSING_INSTRUCTION:
      return "PROCESSING_INSTRUCTION";
    case COMMENT:
      return "COMMENT";
    case DOCDECL:
      return "DOCDECL";
    default:
      return "Illegal state";
  }
}

void XmlPullParser::exception(const std::string& desc) {
  throw XmlParseError("xml Parser Exception : " + desc + " in state: " + stateName(type_) +
                      " at position: " + std::to_string(line_) + "; " +
                      std::to_string(column_));
}

/* The common part of next() and nextToken(): clears the state except txtPos_
 * and isWspace_, and does not set type_ for merging. */
void XmlPullParser::nextImpl() {
  if (type_ == END_TAG) {
    depth_--;
  }
  while (true) {
    attributeCount_ = -1;
    if (degenerated_) {
      degenerated_ = false;
      type_ = END_TAG;
      return;
    }
    name_.clear();
    type_ = peekType();
    switch (type_) {
      case ENTITY_REF:
        pushEntity();
        return;
      case START_TAG:
        parseStartTag(false);
        return;
      case END_TAG:
        parseEndTag();
        return;
      case END_DOCUMENT:
        return;
      case TEXT:
        pushText('<', !token_);
        if (depth_ == 0 && isWspace_) {
          type_ = IGNORABLE_WHITESPACE;
        }
        return;
      default:
        type_ = parseLegacy(token_);
        if (type_ != kXmlDecl) {
          return;
        }
    }
  }
}

int XmlPullParser::parseLegacy(bool bpush) {
  std::string req;
  int term = 0;
  int result = 0;
  int prev = 0;
  read();  // <
  int c = read();
  if (c == '?') {
    if ((peekbuf(0) == 'x' || peekbuf(0) == 'X') && (peekbuf(1) == 'm' || peekbuf(1) == 'M')) {
      if (bpush) {
        push(peekbuf(0));
        push(peekbuf(1));
      }
      read();
      read();

      if ((peekbuf(0) == 'l' || peekbuf(0) == 'L') && peekbuf(1) <= ' ') {
        if (line_ != 1 || column_ > 4) {
          exception("PI must not start with xml");
        }
        parseStartTag(true);
        if (attributeCount_ < 1 || "version" != attributes_[0]) {
          exception("version expected");
        }
        version_ = attributes_[1];
        int pos = 1;
        if (pos < attributeCount_ && "encoding" == attributes_[2]) {
          encoding_ = attributes_[3];
          pos++;
        }

        if (pos < attributeCount_ && "standalone" == attributes_[2 * static_cast<std::size_t>(pos)]) {
          const std::string st = attributes_[2 * static_cast<std::size_t>(pos) + 1];
          if ("yes" == st) {
            standalone_ = true;
          } else if ("no" == st) {
            standalone_ = false;
          } else {
            exception("illegal standalone value: " + st);
          }
          pos++;
        }

        if (pos != attributeCount_) {
          exception("illegal xmldecl");
        }
        isWspace_ = true;
        txtPos_ = 0;
        return kXmlDecl;
      }
    }
    term = '?';
    result = PROCESSING_INSTRUCTION;
  } else if (c == '!') {
    if (peekbuf(0) == '-') {
      result = COMMENT;
      req = "--";
      term = '-';
    } else if (peekbuf(0) == '[') {
      result = CDSECT;
      req = "[CDATA[";
      term = ']';
      bpush = true;
    } else {
      result = DOCDECL;
      req = "DOCTYPE";
      term = -1;
    }
  } else {
    exception("illegal: <" + std::string(1, static_cast<char>(c)));
  }

  for (const char ch : req) {
    read(ch);
  }
  if (result == DOCDECL) {
    parseDoctype(bpush);
  } else {
    while (true) {
      c = read();
      if (c == -1) {
        exception(kUnexpectedEof);
      }
      if (bpush) {
        push(c);
      }
      if ((term == '?' || c == term) && peekbuf(0) == term && peekbuf(1) == '>') {
        break;
      }
      prev = c;
    }

    if (term == '-' && prev == '-' && !relaxed_) {
      exception("illegal comment delimiter: --->");
    }
    read();
    read();
    if (bpush && term != '?') {
      txtPos_--;
    }
  }
  return result;
}

/* precondition: <! consumed */
void XmlPullParser::parseDoctype(bool bpush) {
  int nesting = 1;
  bool quoted = false;

  while (true) {
    const int i = read();
    switch (i) {
      case -1:
        exception(kUnexpectedEof);
      case '\'':
        quoted = !quoted;
        break;
      case '<':
        if (!quoted) {
          nesting++;
        }
        break;
      case '>':
        if (!quoted) {
          if ((--nesting) == 0) {
            return;
          }
        }
        break;
      default:
        break;
    }
    if (bpush) {
      push(i);
    }
  }
}

/* precondition: </ consumed */
void XmlPullParser::parseEndTag() {
  read();  // '<'
  read();  // '/'
  name_ = readName();
  skip();
  read('>');
  if (depth_ == 0) {
    if (!relaxed_) {
      exception("element stack empty");
    }
    return;
  }
  const std::string& open = elementStack_[static_cast<std::size_t>(depth_ - 1)];
  if (name_ != open) {
    if (!relaxed_) {
      exception("expected: " + open);
    }
    return;
  }
}

int XmlPullParser::peekType() {
  switch (peekbuf(0)) {
    case -1:
      return END_DOCUMENT;
    case '&':
      return ENTITY_REF;
    case '<':
      switch (peekbuf(1)) {
        case '/':
          return END_TAG;
        case '?':
        case '!':
          return kLegacy;
        default:
          return START_TAG;
      }
    default:
      return TEXT;
  }
}

std::string XmlPullParser::get(int pos) const {
  return std::string(txtBuf_.data() + pos, static_cast<std::size_t>(txtPos_ - pos));
}

void XmlPullParser::push(int c) {
  isWspace_ = isWspace_ && c <= ' ';
  const auto at = static_cast<std::size_t>(txtPos_);
  if (at >= txtBuf_.size()) {
    txtBuf_.resize(at * 4 / 3 + 16);
  }
  txtBuf_[at] = static_cast<char>(c);
  txtPos_++;
}

/* Sets name and attributes. */
void XmlPullParser::parseStartTag(bool xmldecl) {
  if (!xmldecl) {
    read();
  }
  name_ = readName();
  attributeCount_ = 0;
  attributes_.clear();

  while (true) {
    skip();
    const int c = peekbuf(0);
    if (xmldecl) {
      if (c == '?') {
        read();
        read('>');
        return;
      }
    } else {
      if (c == '/') {
        degenerated_ = true;
        read();
        skip();
        read('>');
        break;
      }
      if (c == '>') {
        read();
        break;
      }
    }
    if (c == -1) {
      exception(kUnexpectedEof);
    }
    std::string attrName = readName();
    if (attrName.empty()) {
      exception("attr name expected");
    }
    skip();
    read('=');
    skip();
    int delimiter = read();
    if (delimiter != '\'' && delimiter != '"') {
      if (!relaxed_) {
        exception("<" + name_ + ">: invalid delimiter: " +
                  std::string(1, static_cast<char>(delimiter)));
      }
      delimiter = ' ';
    }
    attributeCount_++;
    attributes_.push_back(std::move(attrName));
    const int p = txtPos_;
    pushText(delimiter, true);
    attributes_.push_back(get(p));
    txtPos_ = p;
    if (delimiter != ' ') {
      read();  // the closing quote
    }
  }

  const auto sp = static_cast<std::size_t>(depth_++);
  if (elementStack_.size() <= sp) {
    elementStack_.resize(sp + 1);
  }
  elementStack_[sp] = name_;

  for (int i = attributeCount_ - 1; i > 0; i--) {
    for (int j = 0; j < i; j++) {
      if (getAttributeName(i) == getAttributeName(j)) {
        exception("Duplicate Attribute: " + getAttributeName(i));
      }
    }
  }
}

void XmlPullParser::pushEntity() {
  read();  // &
  const int pos = txtPos_;
  while (true) {
    const int c = read();
    if (c == ';') {
      break;
    }
    if (relaxed_ && (c == '<' || c == '&' || c <= ' ')) {
      if (c != -1) {
        push(c);
      }
      return;
    }
    if (c == -1) {
      exception(kUnexpectedEof);
    }
    push(c);
  }
  const std::string code = get(pos);
  txtPos_ = pos;
  if (token_ && type_ == ENTITY_REF) {
    name_ = code;
  }
  if (!code.empty() && code[0] == '#') {
    const int c = (code.size() > 1 && code[1] == 'x') ? parseInt(code.substr(2), 16)
                                                       : parseInt(code.substr(1), 10);
    if (c < 0x80) {
      push(c);  // the original cut every value to one byte
    } else {
      // UTF-8; values past Unicode become U+FFFD
      const unsigned int u = c <= 0x10FFFF ? static_cast<unsigned int>(c) : 0xFFFDu;
      if (u < 0x800) {
        push(static_cast<int>(0xC0 | (u >> 6)));
        push(static_cast<int>(0x80 | (u & 0x3F)));
      } else if (u < 0x10000) {
        push(static_cast<int>(0xE0 | (u >> 12)));
        push(static_cast<int>(0x80 | ((u >> 6) & 0x3F)));
        push(static_cast<int>(0x80 | (u & 0x3F)));
      } else {
        push(static_cast<int>(0xF0 | (u >> 18)));
        push(static_cast<int>(0x80 | ((u >> 12) & 0x3F)));
        push(static_cast<int>(0x80 | ((u >> 6) & 0x3F)));
        push(static_cast<int>(0x80 | (u & 0x3F)));
      }
    }
    return;
  }
  const std::string result = entityMap_[code];
  unresolved_ = result.empty();
  if (unresolved_) {
    if (!token_) {
      exception("unresolved: &" + code + ";");
    }
  } else {
    for (const char ch : result) {
      push(static_cast<unsigned char>(ch));
    }
  }
}

/* delimiter '<': text up to the next tag; a quote: an attribute value; ' ':
 * an unquoted value up to whitespace or '>' */
void XmlPullParser::pushText(int delimiter, bool resolveEntities) {
  int next = peekbuf(0);
  while (next != -1 && next != delimiter) {
    if (delimiter == ' ') {
      if (next <= ' ' || next == '>') {
        break;
      }
    }
    // XML 1.0 3.1 forbids a literal '<' in an attribute value
    if (next == '<' && delimiter != '<') {
      exception("illegal character '<' in attribute value");
    }
    if (next == '&') {
      if (!resolveEntities) {
        break;
      }
      pushEntity();
    } else if (next == '\n' && type_ == START_TAG) {
      read();
      push(' ');
    } else {
      push(read());
    }
    next = peekbuf(0);
  }
}

void XmlPullParser::read(char c) {
  const int a = read();
  if (a != static_cast<unsigned char>(c)) {
    exception("expected: '" + std::string(1, c) + "' actual: '" +
              std::string(1, static_cast<char>(a)) + "'");
  }
}

int XmlPullParser::read() {
  int result = 0;
  if (peekCount_ == 0) {
    result = peekbuf(0);
  } else {
    result = peek_[0];
    peek_[0] = peek_[1];
  }
  peekCount_--;
  column_++;
  if (result == '\n') {
    line_++;
    column_ = 1;
  }
  return result;
}

/* Never reads more than needed. */
int XmlPullParser::peekbuf(int pos) {
  while (pos >= peekCount_) {
    const int nw = srcPos_ < input_.size() ? static_cast<unsigned char>(input_[srcPos_++]) : -1;
    if (nw == '\r') {
      wasCR_ = true;
      peek_[peekCount_++] = '\n';
    } else {
      if (nw == '\n') {
        if (!wasCR_) {
          peek_[peekCount_++] = '\n';
        }
      } else {
        peek_[peekCount_++] = nw;
      }
      wasCR_ = false;
    }
  }
  return peek_[pos];
}

std::string XmlPullParser::readName() {
  const int pos = txtPos_;
  int c = peekbuf(0);
  if ((c < 'a' || c > 'z') && (c < 'A' || c > 'Z') && c != '_' && c != ':' && c < 0x0c0) {
    exception("name expected");
  }

  do {
    push(read());
    c = peekbuf(0);
  } while ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '-' || c == ':' || c == '.' || c >= 0x0b7);

  std::string result = get(pos);
  txtPos_ = pos;
  return result;
}

void XmlPullParser::skip() {
  while (true) {
    const int c = peekbuf(0);
    if (c > ' ' || c == -1) {
      break;
    }
    read();
  }
}

// ---- public part ----------------------------------------------------------------

void XmlPullParser::defineEntityReplacementText(const std::string& entity,
                                                const std::string& value) {
  entityMap_[entity] = value;
}

bool XmlPullParser::isWhitespace() {
  if (type_ != TEXT && type_ != IGNORABLE_WHITESPACE && type_ != CDSECT) {
    exception(kIllegalType);
  }
  return isWspace_;
}

std::string XmlPullParser::getText() const {
  return type_ < TEXT || (type_ == ENTITY_REF && unresolved_) ? std::string() : get(0);
}

bool XmlPullParser::isEmptyElementTag() {
  if (type_ != START_TAG) {
    exception(kIllegalType);
  }
  return degenerated_;
}

std::string XmlPullParser::getAttributeName(int index) {
  if (index >= attributeCount_) {
    exception("IndexOutOfBoundsException()");
  }
  return attributes_[2 * static_cast<std::size_t>(index)];
}

std::string XmlPullParser::getAttributeValue(int index) {
  if (index >= attributeCount_) {
    exception("IndexOutOfBoundsException()");
  }
  return attributes_[2 * static_cast<std::size_t>(index) + 1];
}

std::string XmlPullParser::getAttributeValue(const std::string& name) const {
  for (int i = attributeCount_ - 1; i >= 0; i--) {
    if (attributes_[2 * static_cast<std::size_t>(i)] == name) {
      return attributes_[2 * static_cast<std::size_t>(i) + 1];
    }
  }
  return {};
}

int XmlPullParser::next() {
  txtPos_ = 0;
  isWspace_ = true;
  int minType = 9999;
  token_ = false;

  do {
    nextImpl();
    if (type_ < minType) {
      minType = type_;
    }
  } while (minType > ENTITY_REF  // ignorable
           || (minType >= TEXT && peekType() >= TEXT));

  type_ = minType;

  if (type_ > TEXT) {
    type_ = TEXT;
  }

  return type_;
}

int XmlPullParser::nextToken() {
  isWspace_ = true;
  txtPos_ = 0;
  token_ = true;
  nextImpl();
  return type_;
}

int XmlPullParser::nextTag() {
  if (skipNextTag_) {
    skipNextTag_ = false;
    return type_;
  }
  next();

  if (type_ == TEXT && isWspace_) {
    next();
  }

  if (type_ != END_TAG && type_ != START_TAG && type_ != END_DOCUMENT) {
    exception("unexpected type");
  }

  return type_;
}

void XmlPullParser::require(int type, const std::string& name) {
  if (type != type_ || (!name.empty() && name != getName())) {
    exception("expected: " + stateName(type) + name);
  }
}

std::string XmlPullParser::nextText() {
  if (type_ != START_TAG) {
    exception("precondition: START_TAG");
  }

  next();
  std::string result;

  if (type_ == TEXT) {
    result = getText();
    next();
  }

  if (type_ != END_TAG) {
    exception("END_TAG expected");
  }

  return result;
}

/* From a START_TAG to its END_TAG. */
void XmlPullParser::skipSubTree() {
  require(START_TAG, "");
  int level = 1;
  while (level > 0) {
    const int eventType = next();
    if (eventType == END_TAG) {
      --level;
    } else if (eventType == START_TAG) {
      ++level;
    }
  }
}

}  // namespace bt::io
