// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from test/test_xml.cpp of the original, plus cases for the tree
// reader (xml_document.h). The writer reports misuse with BT_ASSERT, so the
// cases that expected xmlWriterException_c expect InternalError.

#include "shapelogic/base/assert.h"
#include "shapelogic/io/xml_document.h"
#include "shapelogic/io/xml_pull_parser.h"
#include "shapelogic/io/xml_writer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

using bt::InternalError;
using bt::io::XmlParseError;
using bt::io::XmlPullParser;
using bt::io::XmlWriter;
using Catch::Matchers::ContainsSubstring;

namespace {

std::string written(void (*build)(XmlWriter&)) {
  XmlWriter xml;
  build(xml);
  return xml.str();
}

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

void readToEnd(XmlPullParser& pars) {
  while (pars.next() != XmlPullParser::END_DOCUMENT) {
  }
}

}  // namespace

TEST_CASE("xml writer: an empty tag uses the short form", "[io][xml]") {
  const std::string doc = written([](XmlWriter& xml) {
    xml.newTag("puzzle");
    xml.endTag("puzzle");
  });
  REQUIRE(doc == "<?xml version=\"1.0\"?>\n<puzzle/>\n");
}

TEST_CASE("xml writer: a tag with content uses the long form", "[io][xml]") {
  const std::string doc = written([](XmlWriter& xml) {
    xml.newTag("comment");
    xml.addContent("hello");
    xml.endTag("comment");
  });
  REQUIRE(contains(doc, "<comment>hello</comment>"));
}

TEST_CASE("xml writer: tags nest with one space of indent per level", "[io][xml]") {
  const std::string doc = written([](XmlWriter& xml) {
    xml.newTag("outer");
    xml.newTag("inner");
    xml.addContent("x");
    xml.endTag("inner");
    xml.newTag("empty");
    xml.endTag("empty");
    xml.endTag("outer");
  });
  REQUIRE(doc == "<?xml version=\"1.0\"?>\n<outer>\n <inner>x</inner>\n <empty/>\n</outer>\n");
}

TEST_CASE("xml writer: attributes carry their values", "[io][xml]") {
  const std::string doc = written([](XmlWriter& xml) {
    xml.newTag("shape");
    xml.newAttrib("name", "piece one");
    xml.newAttrib("count", 7u);
    xml.newAttrib("offset", -3);
    xml.endTag("shape");
  });
  REQUIRE(contains(doc, "<shape name=\"piece one\" count=\"7\" offset=\"-3\"/>"));
}

TEST_CASE("xml writer: all five special characters are escaped in content", "[io][xml]") {
  const std::string doc = written([](XmlWriter& xml) {
    xml.newTag("t");
    xml.addContent("<&>\"'");
    xml.endTag("t");
  });
  REQUIRE(contains(doc, "<t>&lt;&amp;&gt;&quot;&apos;</t>"));
}

TEST_CASE("xml writer: special characters are escaped in attribute values too", "[io][xml]") {
  const std::string doc = written([](XmlWriter& xml) {
    xml.newTag("t");
    xml.newAttrib("v", "a<b&c\"d");
    xml.endTag("t");
  });
  REQUIRE(contains(doc, "v=\"a&lt;b&amp;c&quot;d\""));
}

TEST_CASE("xml writer: misuse is an internal error", "[io][xml]") {
  SECTION("a tag left open") {
    XmlWriter xml;
    xml.newTag("never closed");
    REQUIRE_THROWS_AS(xml.str(), InternalError);
  }
  SECTION("closing with the wrong name") {
    XmlWriter xml;
    xml.newTag("outer");
    REQUIRE_THROWS_AS(xml.endTag("wrong"), InternalError);
  }
  SECTION("closing when none is open") {
    XmlWriter xml;
    REQUIRE_THROWS_AS(xml.endTag("puzzle"), InternalError);
  }
  SECTION("an attribute after content") {
    XmlWriter xml;
    xml.newTag("t");
    xml.addContent("x");
    REQUIRE_THROWS_AS(xml.newAttrib("a", "b"), InternalError);
  }
  SECTION("an element inside text") {
    XmlWriter xml;
    xml.newTag("t");
    xml.addContent("x");
    REQUIRE_THROWS_AS(xml.newTag("u"), InternalError);
  }
}

TEST_CASE("xml parser: walks a document tag by tag", "[io][xml]") {
  XmlPullParser pars("<a><b>text</b></a>");
  REQUIRE(pars.nextTag() == XmlPullParser::START_TAG);
  REQUIRE(pars.getName() == "a");
  REQUIRE(pars.nextTag() == XmlPullParser::START_TAG);
  REQUIRE(pars.getName() == "b");
}

TEST_CASE("xml parser: reads attribute values by name", "[io][xml]") {
  XmlPullParser pars("<shape name=\"piece one\" count=\"7\"/>");
  REQUIRE(pars.nextTag() == XmlPullParser::START_TAG);
  REQUIRE(pars.getName() == "shape");
  REQUIRE(pars.getAttributeValue("name") == "piece one");
  REQUIRE(pars.getAttributeValue("count") == "7");
  // an absent attribute reads as an empty string
  REQUIRE(pars.getAttributeValue("missing").empty());
}

TEST_CASE("xml parser: entities in content are resolved", "[io][xml]") {
  XmlPullParser pars("<t>a&lt;b&amp;c&gt;d&quot;e&apos;f</t>");
  REQUIRE(pars.nextTag() == XmlPullParser::START_TAG);
  REQUIRE(pars.next() == XmlPullParser::TEXT);
  REQUIRE(pars.getText() == "a<b&c>d\"e'f");
}

TEST_CASE("xml parser: an empty element tag is recognized as such", "[io][xml]") {
  XmlPullParser pars("<a><short/></a>");
  REQUIRE(pars.nextTag() == XmlPullParser::START_TAG);
  REQUIRE_FALSE(pars.isEmptyElementTag());
  REQUIRE(pars.nextTag() == XmlPullParser::START_TAG);
  REQUIRE(pars.getName() == "short");
  REQUIRE(pars.isEmptyElementTag());
}

TEST_CASE("xml parser: require accepts the expected tag and rejects others", "[io][xml]") {
  XmlPullParser good("<puzzle/>");
  good.nextTag();
  REQUIRE_NOTHROW(good.require(XmlPullParser::START_TAG, "puzzle"));

  XmlPullParser bad("<puzzle/>");
  bad.nextTag();
  REQUIRE_THROWS_AS(bad.require(XmlPullParser::START_TAG, "problem"), XmlParseError);
}

TEST_CASE("xml parser: skipSubTree steps over a whole nested element", "[io][xml]") {
  XmlPullParser pars("<root><skipme><deep><deeper/></deep></skipme><after/></root>");
  pars.nextTag();
  pars.nextTag();
  REQUIRE(pars.getName() == "skipme");
  pars.skipSubTree();
  REQUIRE(pars.nextTag() == XmlPullParser::START_TAG);
  REQUIRE(pars.getName() == "after");
}

TEST_CASE("xml parser: a custom entity replacement is applied", "[io][xml]") {
  XmlPullParser pars("<t>x&mine;</t>");
  pars.defineEntityReplacementText("mine", "replaced");
  pars.nextTag();
  REQUIRE(pars.next() == XmlPullParser::TEXT);
  REQUIRE(pars.getText() == "xreplaced");
}

TEST_CASE("xml parser: an entity that is an element's sole content resolves as text",
          "[io][xml]") {
  // next() merges entity references with the text around them; an entity
  // alone must still come out as TEXT (a comment of only special
  // characters is written that way)
  XmlPullParser pars("<t>&mine;</t>");
  pars.defineEntityReplacementText("mine", "replaced");
  pars.nextTag();
  REQUIRE(pars.next() == XmlPullParser::TEXT);
  REQUIRE(pars.getText() == "replaced");
}

TEST_CASE("xml parser: prevTag makes the following nextTag re-read the current tag",
          "[io][xml]") {
  XmlPullParser pars("<a><b></b><c></c></a>");
  pars.nextTag();
  REQUIRE(pars.nextTag() == XmlPullParser::START_TAG);
  REQUIRE(pars.getName() == "b");
  pars.prevTag();
  REQUIRE(pars.nextTag() == XmlPullParser::START_TAG);
  REQUIRE(pars.getName() == "b");
  REQUIRE(pars.nextTag() == XmlPullParser::END_TAG);
  REQUIRE(pars.getName() == "b");
  REQUIRE(pars.nextTag() == XmlPullParser::START_TAG);
  REQUIRE(pars.getName() == "c");
}

TEST_CASE("xml parser: malformed documents are rejected", "[io][xml]") {
  SECTION("mismatched closing tag") {
    XmlPullParser pars("<a></b>");
    REQUIRE_THROWS_WITH(readToEnd(pars), ContainsSubstring("expected: a"));
  }
  SECTION("unclosed tag") {
    XmlPullParser pars("<a><b></a>");
    REQUIRE_THROWS_AS(readToEnd(pars), XmlParseError);
  }
  SECTION("truncated document") {
    XmlPullParser pars("<a><b");
    REQUIRE_THROWS_AS(readToEnd(pars), XmlParseError);
  }
  SECTION("unterminated attribute value running into a tag") {
    XmlPullParser pars("<a name=\"unterminated></a>");
    REQUIRE_THROWS_WITH(pars.next(),
                        ContainsSubstring("illegal character '<' in attribute value"));
  }
  SECTION("unterminated attribute value running to the end") {
    XmlPullParser pars("<a name=\"unterminated");
    REQUIRE_THROWS_WITH(pars.next(), ContainsSubstring("EOF"));
  }
  SECTION("duplicate attribute") {
    XmlPullParser pars("<a x=\"1\" x=\"2\"/>");
    REQUIRE_THROWS_WITH(pars.next(), ContainsSubstring("Duplicate Attribute: x"));
  }
  SECTION("unknown entity") {
    XmlPullParser pars("<a>&nope;</a>");
    pars.nextTag();
    REQUIRE_THROWS_WITH(pars.next(), ContainsSubstring("unresolved: &nope;"));
  }
}

TEST_CASE("xml parser: an unquoted '=' inside an attribute value is ordinary text",
          "[io][xml]") {
  XmlPullParser pars("<a name=\"value1 name2=\"/>");
  REQUIRE(pars.next() == XmlPullParser::START_TAG);
  REQUIRE(pars.getAttributeCount() == 1);
  REQUIRE(pars.getAttributeName(0) == "name");
  REQUIRE(pars.getAttributeValue(0) == "value1 name2=");
  REQUIRE_NOTHROW(readToEnd(pars));
}

TEST_CASE("xml parser: a literal '<' inside an attribute value is rejected, with its position",
          "[io][xml]") {
  {
    XmlPullParser pars("<a name=\"x<y\"/>");
    REQUIRE_THROWS_WITH(pars.next(), ContainsSubstring("illegal character '<' in attribute value") &&
                                         ContainsSubstring("at position: 1; 10"));
  }
  {
    XmlPullParser pars("<a name=\"v><b/>\" z=\"2\"/>");
    REQUIRE_THROWS_WITH(pars.next(), ContainsSubstring("at position: 1; 11"));
  }
}

TEST_CASE("xml parser: an escaped '<' inside an attribute value still resolves", "[io][xml]") {
  for (const char* doc : {"<a name=\"x&lt;y\"/>", "<a name=\"x&#60;y\"/>"}) {
    XmlPullParser pars(doc);
    REQUIRE(pars.next() == XmlPullParser::START_TAG);
    REQUIRE(pars.getAttributeValue("name") == "x<y");
    REQUIRE_NOTHROW(readToEnd(pars));
  }
}

// ---- behavior kept from the original (new cases) -----------------------------------

TEST_CASE("xml parser: line ends, attribute newlines, comments and CDATA", "[io][xml]") {
  SECTION("\\r\\n and a lone \\r become \\n") {
    XmlPullParser pars("<t>a\r\nb\rc</t>");
    pars.nextTag();
    pars.next();
    REQUIRE(pars.getText() == "a\nb\nc");
  }
  SECTION("a newline in an attribute value becomes a space") {
    XmlPullParser pars("<t v=\"a\nb\"/>");
    pars.next();
    REQUIRE(pars.getAttributeValue("v") == "a b");
  }
  SECTION("text is merged across comments, processing instructions and CDATA") {
    XmlPullParser pars("<t>a<!-- c -->b<?pi x?>c<![CDATA[<d>]]>e</t>");
    pars.nextTag();
    REQUIRE(pars.next() == XmlPullParser::TEXT);
    REQUIRE(pars.getText() == "abc<d>e");
    REQUIRE(pars.next() == XmlPullParser::END_TAG);
  }
  SECTION("numeric references above 127 are written as UTF-8") {
    XmlPullParser pars("<t>&#233;&#x20AC;&#65;</t>");
    pars.nextTag();
    pars.next();
    REQUIRE(pars.getText() == "\xC3\xA9\xE2\x82\xAC" "A");
  }
}

TEST_CASE("xml document: a tree with attributes in order, leaf text and lines", "[io][xml]") {
  const auto doc = bt::io::parseXmlDocument(
      "<?xml version=\"1.0\"?>\n<!-- c -->\n<root b=\"2\" a=\"1\">\n <leaf>  x  </leaf>\n"
      " <empty/>\n <ws>\n </ws>\n</root>\ntrailing garbage is not read <");
  REQUIRE(doc.has_value());
  REQUIRE(doc->name == "root");
  REQUIRE(doc->line == 3);
  REQUIRE(doc->attributes.size() == 2);
  REQUIRE(doc->attributes[0].name == "b");
  REQUIRE(doc->attribute("a") == "1");
  REQUIRE(doc->text.empty());  // whitespace between children is not kept
  REQUIRE(doc->children.size() == 3);
  REQUIRE(doc->children[0].text == "  x  ");  // leaf text is kept as it is
  REQUIRE(doc->children[1].text.empty());
  REQUIRE(doc->children[2].text == "\n ");
}

TEST_CASE("xml document: errors are ParseError results", "[io][xml]") {
  for (const char* bad : {"", "text before the root", "<a>", "<a></b>", "<a x='1' x='2'/>"}) {
    INFO(bad);
    const auto doc = bt::io::parseXmlDocument(bad);
    REQUIRE_FALSE(doc.has_value());
    REQUIRE(doc.error().code == bt::Errc::ParseError);
  }
}

TEST_CASE("xml document: a document nested too deeply is refused", "[io][xml]") {
  std::string deep;
  for (int i = 0; i < 6000; i++) {
    deep += "<a>";
  }
  const auto doc = bt::io::parseXmlDocument(deep);
  REQUIRE_FALSE(doc.has_value());
  REQUIRE_THAT(doc.error().message, ContainsSubstring("nested too deeply"));
}

// A file is untrusted (cleanup item 16.1). The check for an attribute given
// twice compared every pair, copying both names: an element with a million
// attributes, 11 MB of a file, held the engine's one request thread for
// hours. And every element of a document becomes an XmlElement of more
// than a hundred bytes, so 300 KB of gzip could ask for gigabytes. The
// reader now counts both, and the check costs the attributes' number.
TEST_CASE("xml parser: thousands of attributes are checked at once", "[io][xml]") {
  const int count = 50000;
  std::string many = "<a";
  for (int i = 0; i < count; i++) {
    many += " n" + std::to_string(i) + "=\"" + std::to_string(i) + "\"";
  }
  // (the parser reads the text where it stands: each document is kept)
  {
    const std::string doc = many + "/>";
    XmlPullParser pars(doc, 100000);
    REQUIRE(pars.next() == XmlPullParser::START_TAG);
    REQUIRE(pars.getAttributeCount() == count);
    REQUIRE(pars.getAttributeName(0) == "n0");
    REQUIRE(pars.getAttributeValue("n49999") == "49999");
    REQUIRE_NOTHROW(readToEnd(pars));
  }
  {
    // one given twice, far apart: still told, by its name
    const std::string doc = many + " n7=\"again\"/>";
    XmlPullParser pars(doc, 100000);
    REQUIRE_THROWS_WITH(pars.next(), ContainsSubstring("Duplicate Attribute: n7 "));
  }
  {
    // several given twice: the last one that repeats is named, as the
    // original's pairwise check named it
    const std::string doc = many + " n7=\"\" n9=\"\" n8=\"\" fresh=\"\"/>";
    XmlPullParser pars(doc, 100000);
    REQUIRE_THROWS_WITH(pars.next(), ContainsSubstring("Duplicate Attribute: n8 "));
  }
  // the same below and above where the set takes over from the pairs
  struct Few {
    const char* doc;
    const char* named;
  };
  const Few fews[] = {
      {"<a x='' y='' x=''/>", "Duplicate Attribute: x "},
      {"<a x='' y='' z='' y='' x='' w=''/>", "Duplicate Attribute: x "},
      {"<a a='' b='' c='' d='' e='' f='' g='' h='' i='' c='' a='' j=''/>",
       "Duplicate Attribute: a "},
      {"<a a='' b='' c='' d='' e='' f='' g='' h='' i='' a='' c='' j=''/>",
       "Duplicate Attribute: c "},
  };
  for (const Few& few : fews) {
    INFO(few.doc);
    XmlPullParser pars(few.doc);
    REQUIRE_THROWS_WITH(pars.next(), ContainsSubstring(few.named));
  }
}

TEST_CASE("xml parser: an element's attributes are counted", "[io][xml]") {
  const auto element = [](std::size_t attributes) {
    std::string doc = "<a";
    for (std::size_t i = 0; i < attributes; i++) {
      doc += " n" + std::to_string(i) + "=''";
    }
    return doc + "/>";
  };
  // the parser's own bound, and one a caller gives
  REQUIRE(bt::io::parseXmlDocument(element(XmlPullParser::kMaxAttributes)).has_value());
  const auto over = bt::io::parseXmlDocument(element(XmlPullParser::kMaxAttributes + 1));
  REQUIRE_FALSE(over.has_value());
  CHECK(over.error().code == bt::Errc::ParseError);
  CHECK_THAT(over.error().message, ContainsSubstring("too many attributes in <a>"));
  CHECK(bt::io::XmlLimits{}.attributes == XmlPullParser::kMaxAttributes);

  bt::io::XmlLimits few;
  few.attributes = 3;
  CHECK(bt::io::parseXmlDocument(element(3), few).has_value());
  CHECK_FALSE(bt::io::parseXmlDocument(element(4), few).has_value());
  // each element has its own count
  CHECK(bt::io::parseXmlDocument("<a x='' y='' z=''><b x='' y='' z=''/><c x=''/></a>", few)
            .has_value());
}

TEST_CASE("xml document: a document's elements are counted", "[io][xml]") {
  const auto document = [](std::size_t elements, bool nested) {
    // the root and `elements - 1` below it: side by side, or half of them
    // each inside the last
    std::string doc = "<r>";
    std::string close;
    for (std::size_t i = 1; i < elements; i++) {
      if (nested && i % 2 == 0) {
        doc += "<n>";
        close += "</n>";
      } else {
        doc += "<e/>";
      }
    }
    return doc + close + "</r>";
  };
  bt::io::XmlLimits limits;
  limits.elements = 1000;
  for (const bool nested : {false, true}) {
    INFO((nested ? "nested" : "side by side"));
    const auto within = bt::io::parseXmlDocument(document(1000, nested), limits);
    REQUIRE(within.has_value());
    const auto over = bt::io::parseXmlDocument(document(1001, nested), limits);
    REQUIRE_FALSE(over.has_value());
    CHECK(over.error().code == bt::Errc::ParseError);
    CHECK_THAT(over.error().message, ContainsSubstring("too many elements"));
  }
  // the default is far past these
  CHECK(bt::io::parseXmlDocument(document(5000, false)).has_value());
}

TEST_CASE("xml document: written back in the writer's layout", "[io][xml]") {
  const auto doc = bt::io::parseXmlDocument("<r a=\"&amp;\"><x>t</x><y/></r>");
  REQUIRE(doc.has_value());
  REQUIRE(bt::io::writeXmlDocument(*doc) ==
          "<?xml version=\"1.0\"?>\n<r a=\"&amp;\">\n <x>t</x>\n <y/>\n</r>\n");
}
