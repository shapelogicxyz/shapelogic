// SPDX-License-Identifier: GPL-3.0-or-later
// The <tray> element of a tilted tray (decision 0045;
// puzzle_file_internal.h; cleanup item 21.5 moved it here from
// puzzle_file.cpp), read and written.
#include "shapelogic/io/puzzle_file_internal.h"

#include <cmath>
#include <cstdio>

namespace bt::io::detail {

namespace {

// ---- the tilted tray (decision 0045) ---------------------------------------------

/// A number as the file holds it; nothing when the text is not one.
std::optional<double> realOf(const std::string& str) {
  if (str.empty()) {
    return std::nullopt;
  }
  char* end = nullptr;
  const double v = std::strtod(str.c_str(), &end);
  if (end != str.c_str() + str.size() || !std::isfinite(v)) {
    return std::nullopt;
  }
  return v;
}

/// A whole number from 0 to `most`; nothing when the attribute is missing
/// or is not one.
std::optional<unsigned int> wholeOf(const XmlElement& e, const char* name, unsigned int most) {
  const std::string str = e.attribute(name);
  char* end = nullptr;
  const long v = std::strtol(str.c_str(), &end, 10);
  if (str.empty() || end != str.c_str() + str.size() || v < 0 ||
      static_cast<unsigned long>(v) > most) {
    return std::nullopt;
  }
  return static_cast<unsigned int>(v);
}

}  // namespace

std::optional<tilted::Tray> readTray(const XmlElement& e) {
  tilted::Tray t;
  const std::optional<unsigned int> size = wholeOf(e, "size", tilted::Tray::kMaxSize);
  const auto sideOf = [&](const char* name) {
    return e.findAttribute(name) != nullptr ? wholeOf(e, name, tilted::Tray::kMaxSize) : size;
  };
  const std::optional<unsigned int> width = sideOf("width");
  const std::optional<unsigned int> height = sideOf("height");
  const std::optional<unsigned int> over = wholeOf(e, "over", tilted::Tray::kMaxSlope);
  const std::optional<unsigned int> down = wholeOf(e, "down", tilted::Tray::kMaxSlope);
  const std::optional<double> allowance = realOf(e.attribute("allowance"));
  const std::optional<double> shiftX = realOf(e.attribute("shiftX"));
  const std::optional<double> shiftY = realOf(e.attribute("shiftY"));
  const std::string placement = e.attribute("placement");
  if (!width || !height || !over || !down || !allowance || !shiftX || !shiftY ||
      (placement != "best" && placement != "custom")) {
    return std::nullopt;
  }
  // a size beside both sides says nothing, but a bad one is still a bad file
  if (e.findAttribute("size") != nullptr && !size) {
    return std::nullopt;
  }
  t.width = *width;
  t.height = *height;
  t.over = *over;
  t.down = *down;
  t.allowance = *allowance;
  t.placement = placement == "best" ? tilted::Tray::Placement::Best : tilted::Tray::Placement::Custom;
  t.shiftX = *shiftX;
  t.shiftY = *shiftY;
  // the walls: a character a square, row by row, '#' a wall and '_' open
  if (const std::string* walls = e.findAttribute("walls"); walls != nullptr) {
    if (walls->size() != static_cast<std::size_t>(t.width) * t.height) {
      return std::nullopt;
    }
    for (const char ch : *walls) {
      if (ch != '#' && ch != '_') {
        return std::nullopt;
      }
      t.walls.push_back(ch == '#' ? 1 : 0);
    }
    if (std::find(t.walls.begin(), t.walls.end(), 1) == t.walls.end()) {
      t.walls.clear();
    }
  }
  if (!t.valid()) {
    return std::nullopt;
  }
  return t;
}

namespace {

/// A number written in as few digits as read back to the same value.
std::string realText(double v) {
  std::array<char, 40> buf{};
  for (const int digits : {15, 16, 17}) {
    std::snprintf(buf.data(), buf.size(), "%.*g", digits, v);
    if (std::strtod(buf.data(), nullptr) == v) {
      break;
    }
  }
  return buf.data();
}

}  // namespace

void writeTray(XmlWriter& xml, const tilted::Tray& t) {
  xml.newTag("tray");
  if (t.square()) {
    xml.newAttrib("size", t.width);
  } else {
    xml.newAttrib("width", t.width);
    xml.newAttrib("height", t.height);
  }
  xml.newAttrib("over", t.over);
  xml.newAttrib("down", t.down);
  xml.newAttrib("allowance", realText(t.allowance));
  xml.newAttrib("placement", t.placement == tilted::Tray::Placement::Best ? "best" : "custom");
  xml.newAttrib("shiftX", realText(t.shiftX));
  xml.newAttrib("shiftY", realText(t.shiftY));
  if (!t.walls.empty()) {
    std::string walls;
    walls.reserve(t.walls.size());
    for (const std::uint8_t w : t.walls) {
      walls.push_back(w != 0 ? '#' : '_');
    }
    xml.newAttrib("walls", walls);
  }
  xml.endTag("tray");
}

}  // namespace bt::io::detail
