// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from the XML load and save code of src/lib/puzzle.cpp, problem.cpp,
// solution.cpp, assembly.cpp, disassembly.cpp, voxel.cpp and gridtype.cpp,
// and the file part of src/lib/stacking.cpp (saveRodSets, loadRodSets,
// saveProblem, loadProblem, encodeRod, decodeRod, countAttribute).
//
// The original read with a pull parser and skipped what it did not know. This
// reads the XmlElement tree of xml_document.h and keeps what it does not know
// (decision 0008). Numbers are parsed with the same C functions the original
// used (atoi is strtol cast to int), so odd values read the same.
//
// The elements are in files of their own (puzzle_file_internal.h): this one
// reads and writes the <puzzle> around them.

#include "shapelogic/io/puzzle_file.h"
#include "shapelogic/base/base64.h"
#include "shapelogic/base/int_cast.h"
#include "shapelogic/io/gzip_file.h"
#include "shapelogic/io/xml_document.h"
#include "shapelogic/io/xml_writer.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/sliding.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "shapelogic/io/puzzle_file_internal.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sl::io {

using namespace detail;

namespace {

// ---- voxel spaces and the grid --------------------------------------------------

GridType readGridType(const XmlElement& e) {
  const std::string typeStr = e.attribute("type");
  if (typeStr.empty()) {
    error(e, "grid type needs a valid 'type' attribute");
  }
  const int type = atoiOf(typeStr);
  if (type < 0 || type >= kNumGridKinds) {
    error(e, "puzzle with unknown grid type");
  }
  return GridType(static_cast<GridKind>(type));
}

/// The <images> of a puzzle: each <image> with its id, type and the file in
/// base64. A picture whose bytes are not a PNG or JPEG of the stated type,
/// or whose id comes twice, is left out (the shapes showing it lose it).
void readImages(const XmlElement& e, std::vector<PuzzleImage>& images) {
  for (const XmlElement& c : e.children) {
    if (c.name != "image") {
      continue;
    }
    const std::string idText = c.attribute("id");
    const int id = atoiOf(idText);
    if (idText.empty() || id < 0) {
      continue;
    }
    if (std::any_of(images.begin(), images.end(),
                    [id](const PuzzleImage& im) { return static_cast<int>(im.id) == id; })) {
      continue;
    }
    std::optional<std::string> bytes = base64Decode(c.text);
    if (!bytes) {
      continue;
    }
    const std::optional<ImageInfo> info = sniffImage(*bytes);
    if (!info || info->type != c.attribute("type")) {
      continue;
    }
    std::string hash = imageHash(*bytes);
    images.push_back(PuzzleImage{asUnsigned(id), info->type, info->width, info->height,
                                 std::make_shared<const std::string>(std::move(*bytes)),
                                 std::move(hash)});
  }
}

Puzzle readPuzzleElement(const XmlElement& root) {
  using A = PuzzleFileAccess;

  if (root.name != "puzzle") {
    error(root, "expected: START_TAGpuzzle");
  }

  const std::string versionStr = root.attribute("version");
  if (versionStr.empty()) {
    error(root, "puzzle node must have a 'version' attribute");
  }
  const int version = atoiOf(versionStr);
  if (version != 1 && version != 2) {
    error(root, "can only load files of version 1 and 2");
  }

  Puzzle puzzle;
  bool haveGrid = false;
  // A sliding file says where its starts and goals are: slideMarks
  // "problem", each problem has its own (decision 0027). Without that they
  // are the start/goal shapes': a shape that says start has both as marks
  // (decision 0021), and when none does the starts are the shapes' colors
  // (the original).
  const bool problemMarks = root.attribute("slideMarks") == "problem";
  bool startMarks = false;
  Keeper keep(puzzle.preserved());
  keep.attributes(root, {"version", "slideMarks"});

  for (const XmlElement& c : root.children) {
    if (c.name == "gridType") {
      keep.known(kSlotGridType);
      if (haveGrid) {
        error(c, "only one gridtype can be defined, and it must be before the first shape");
      }
      A::setGridType(puzzle, readGridType(c));
      haveGrid = true;
    } else if (c.name == "tray") {
      // a second one, or one that cannot be read, is kept as it is
      std::optional<tilted::Tray> tray = A::tray(puzzle) ? std::nullopt : readTray(c);
      if (tray) {
        keep.known(kSlotTray);
        A::tray(puzzle) = std::move(tray);
      } else {
        keep.unknown(c);
      }
    } else if (c.name == "colors") {
      keep.known(kSlotColors);
      for (const XmlElement& col : c.children) {
        if (col.name == "color") {
          A::colors(puzzle).push_back(
              (asUnsigned(atoiOf(col.attribute("red"))) & 0xFF) |
              (asUnsigned(atoiOf(col.attribute("green"))) & 0xFF) << 8 |
              (asUnsigned(atoiOf(col.attribute("blue"))) & 0xFF) << 16);
        }
      }
    } else if (c.name == "images") {
      keep.known(kSlotImages);
      readImages(c, A::images(puzzle));
    } else if (c.name == "shapes") {
      keep.known(kSlotShapes);
      // without a grid type, the grid is cubes
      haveGrid = true;
      for (const XmlElement& v : c.children) {
        if (v.name == "voxel") {
          startMarks = startMarks || v.findAttribute("start") != nullptr;
          puzzle.addShape(readVoxel(v, puzzle.getGridType()));
        }
      }
    } else if (c.name == "rodSets") {
      keep.known(kSlotRodSets);
      readRodSets(c, puzzle);
    } else if (c.name == "problems") {
      keep.known(kSlotProblems);
      for (const XmlElement& p : c.children) {
        if (p.name == "problem") {
          (void)puzzle.appendProblem(readProblem(p, puzzle));
        }
      }
    } else if (c.name == "comment") {
      keep.known(kSlotComment);
      if (c.findAttribute("popup") != nullptr) {
        puzzle.setCommentPopup(true);
      }
      puzzle.setComment(c.text);
    } else {
      keep.unknown(c);
    }
  }

  // a shape loses a picture the file does not have, or one whose part
  // shown misses it
  auto present = [&puzzle](ShapeImages stack) {
    std::erase_if(stack, [&puzzle](const ShapeImage& im) {
      const PuzzleImage* file = puzzle.findImage(im.id);
      return file == nullptr || !overlapsPicture(im.src, file->width, file->height);
    });
    return stack;
  };
  for (unsigned int s = 0; s < puzzle.getNumberOfShapes(); s++) {
    Voxel& v = puzzle.getShape(s);
    v.setImages(present(v.getImages()));
    TrayBorder b = v.getBorder();
    b.images = present(b.images);
    v.setBorder(b);
  }

  // only a cube-grid puzzle has a tilted tray
  if (puzzle.getGridType().getType() != GridKind::Bricks) {
    A::tray(puzzle).reset();
  }
  // a stacking puzzle has a board, even when its file has none
  if (puzzle.getGridType().getType() == GridKind::Stacking && puzzle.rodSetCount() == 0) {
    puzzle.addRodSet();
  }
  if (puzzle.getGridType().getType() == GridKind::Sliding && !problemMarks) {
    if (startMarks) {
      sliding::adoptBoardMarks(puzzle);
    } else {
      sliding::convertLegacyStarts(puzzle);
    }
  }
  giveUids(puzzle);
  if (problemMarks || puzzle.getGridType().getType() != GridKind::Sliding) {
    // the marks are the problems': a shape carries none; and only the
    // sliding grid has marks at all (its grid classes alone move them as
    // the cells move: cleanup item 11.4)
    for (unsigned int s = 0; s < puzzle.getNumberOfShapes(); s++) {
      puzzle.getShape(s).clearMarks();
    }
  }

  return puzzle;
}

}  // namespace

namespace {

/// What a reader answers when something it did not foresee was thrown: a
/// number of the file that breaks an invariant of the model (InternalError,
/// from an SL_ASSERT), or a size it asks for that there is no memory for. A
/// file is untrusted: whatever is wrong with it, reading it ends with a
/// message, never with the exception (cleanup item 16.2).
std::unexpected<Error> unreadable(const std::exception& e) {
  return fail(Errc::ParseError, std::string("the file could not be read: ") + e.what());
}

}  // namespace

Result<Puzzle> readPuzzle(const XmlElement& root) {
  try {
    return readPuzzleElement(root);
  } catch (const ReadError& e) {
    return fail(Errc::ParseError, e.what());
  } catch (const std::exception& e) {
    return unreadable(e);
  }
}

Result<Puzzle> parsePuzzle(std::string_view xml) {
  try {
    Result<XmlElement> root = parseXmlDocument(xml);
    if (!root) {
      return std::unexpected(root.error());
    }
    return readPuzzle(*root);
  } catch (const std::exception& e) {
    return unreadable(e);
  }
}

Result<Puzzle> loadPuzzle(const std::filesystem::path& path) {
  try {
    Result<std::string> data = readMaybeGzipFile(path);
    if (!data) {
      return std::unexpected(data.error());
    }
    Result<Puzzle> puzzle = parsePuzzle(*data);
    if (!puzzle) {
      return fail(puzzle.error().code, path.string() + ": " + puzzle.error().message);
    }
    return puzzle;
  } catch (const std::exception& e) {
    return unreadable(e);
  }
}

std::string writePuzzle(const Puzzle& puzzle) {
  const PreservedXml& kept = puzzle.preserved();
  XmlWriter xml;

  xml.newTag("puzzle");
  xml.newAttrib("version", "2");
  if (puzzle.getGridType().getType() == GridKind::Sliding) {
    // the starts and goals are the problems' (see readPuzzleElement)
    xml.newAttrib("slideMarks", "problem");
  }
  writeKeptAttributes(xml, kept);

  writeKept(xml, kept, kSlotGridType);
  xml.newTag("gridType");
  xml.newAttrib("type", static_cast<unsigned long>(puzzle.getGridType().getType()));
  xml.endTag("gridType");

  // the tilted tray, when the puzzle has one (decision 0045); the original
  // passes over the element
  writeKept(xml, kept, kSlotTray);
  if (puzzle.tray()) {
    writeTray(xml, *puzzle.tray());
  }

  writeKept(xml, kept, kSlotColors);
  xml.newTag("colors");
  for (const std::uint32_t c : PuzzleFileAccess::colors(puzzle)) {
    xml.newTag("color");
    xml.newAttrib("red", (c >> 0) & 0xFF);
    xml.newAttrib("green", (c >> 8) & 0xFF);
    xml.newAttrib("blue", (c >> 16) & 0xFF);
    xml.endTag("color");
  }
  xml.endTag("colors");

  // the pictures some shape shows; none, no element (decision 0038)
  writeKept(xml, kept, kSlotImages);
  if (std::any_of(puzzle.images().begin(), puzzle.images().end(),
                  [&puzzle](const PuzzleImage& im) { return puzzle.imageUsed(im.id); })) {
    xml.newTag("images");
    for (const PuzzleImage& im : puzzle.images()) {
      if (puzzle.imageUsed(im.id)) {
        xml.newTag("image");
        xml.newAttrib("id", im.id);
        xml.newAttrib("type", im.type);
        xml.newAttrib("width", im.width);
        xml.newAttrib("height", im.height);
        xml.addContent(base64Encode(*im.bytes));
        xml.endTag("image");
      }
    }
    xml.endTag("images");
  }

  writeKept(xml, kept, kSlotShapes);
  xml.newTag("shapes");
  for (unsigned int i = 0; i < puzzle.getNumberOfShapes(); i++) {
    writeVoxel(xml, puzzle.getShape(i));
  }
  xml.endTag("shapes");

  writeKept(xml, kept, kSlotRodSets);
  writeRodSets(xml, puzzle);

  writeKept(xml, kept, kSlotProblems);
  xml.newTag("problems");
  for (unsigned int i = 0; i < puzzle.getNumberOfProblems(); i++) {
    writeProblem(xml, puzzle, puzzle.getProblem(i));
  }
  xml.endTag("problems");

  writeKept(xml, kept, kSlotComment);
  xml.newTag("comment");
  if (!puzzle.getComment().empty()) {
    if (puzzle.getCommentPopup()) {
      xml.newAttrib("popup", "");
    }
    xml.addContent(puzzle.getComment());
  }
  xml.endTag("comment");
  writeKept(xml, kept, kSlotComment + 1);

  xml.endTag("puzzle");
  return xml.str();
}

Result<void> savePuzzle(const Puzzle& puzzle, const std::filesystem::path& path) {
  return writeGzipFile(path, writePuzzle(puzzle));
}

}  // namespace sl::io
