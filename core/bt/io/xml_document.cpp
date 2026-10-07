// SPDX-License-Identifier: GPL-3.0-or-later

#include "bt/io/xml_document.h"

#include "bt/io/xml_pull_parser.h"
#include "bt/io/xml_writer.h"

#include <utility>
#include <vector>

namespace bt::io {

namespace {

/// Deeper documents are refused: the readers and the tree's destructor
/// recurse, and no puzzle file comes near this (a disassembly tree nests
/// once per piece).
constexpr std::size_t kMaxDepth = 5000;

XmlElement startElement(XmlPullParser& pars) {
  XmlElement e;
  e.name = pars.getName();
  e.line = pars.getLineNumber();
  const int n = pars.getAttributeCount();
  e.attributes.reserve(static_cast<std::size_t>(n));
  for (int i = 0; i < n; i++) {
    e.attributes.push_back(XmlAttribute{pars.getAttributeName(i), pars.getAttributeValue(i)});
  }
  return e;
}

/// Reads the element whose START_TAG the parser is at, up to its END_TAG.
XmlElement readElement(XmlPullParser& pars) {
  struct Open {
    XmlElement element;
    std::string text;
  };
  std::vector<Open> stack;
  stack.push_back(Open{startElement(pars), {}});

  while (true) {
    const int t = pars.next();
    if (t == XmlPullParser::START_TAG) {
      if (stack.size() >= kMaxDepth) {
        pars.exception("elements nested too deeply");
      }
      stack.push_back(Open{startElement(pars), {}});
    } else if (t == XmlPullParser::TEXT) {
      stack.back().text += pars.getText();
    } else if (t == XmlPullParser::END_TAG) {
      Open done = std::move(stack.back());
      stack.pop_back();
      // the format has no mixed content: text only counts in a leaf element
      if (done.element.children.empty()) {
        done.element.text = std::move(done.text);
      }
      if (stack.empty()) {
        return std::move(done.element);
      }
      stack.back().element.children.push_back(std::move(done.element));
    } else {
      pars.exception("unexpected end of document inside <" + stack.back().element.name + ">");
    }
  }
}

}  // namespace

Result<XmlElement> parseXmlDocument(std::string_view text) {
  try {
    XmlPullParser pars(text);
    pars.nextTag();
    pars.require(XmlPullParser::START_TAG, "");
    return readElement(pars);
  } catch (const XmlParseError& e) {
    return fail(Errc::ParseError, e.what());
  }
}

std::string writeXmlDocument(const XmlElement& root) {
  XmlWriter w;
  w.writeElement(root);
  return w.str();
}

}  // namespace bt::io
