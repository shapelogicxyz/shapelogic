// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// What the files of the puzzle file share (puzzle_file.cpp and
/// puzzle_file_{voxel,solution,problem,tray}.cpp, one an element: its
/// reader, its writer and its known attributes; cleanup item 21.5): the
/// model's write access, the reading helpers, the bounds on a file's
/// numbers, the writer positions of the known children, and each
/// element's reader and writer. Internal to them.

#include "shapelogic/io/xml_document.h"
#include "shapelogic/io/xml_writer.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/solution.h"
#include "shapelogic/model/tilted_tray.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sl {

// Write access to the model for the reader. Defined here so the model's
// public API keeps its checks.
struct PuzzleFileAccess {
  static std::vector<std::uint32_t>& colors(Puzzle& p) { return p.colors_; }
  static void setGridType(Puzzle& p, GridType gt) { p.gt_ = gt; }
  static const std::vector<std::uint32_t>& colors(const Puzzle& p) { return p.colors_; }
  static std::vector<PuzzleImage>& images(Puzzle& p) { return p.images_; }
  static std::optional<tilted::Tray>& tray(Puzzle& p) { return p.tray_; }
};

struct ProblemFileAccess {
  static std::vector<Part>& parts(Problem& p) { return p.parts_; }
  static unsigned int& result(Problem& p) { return p.result_; }
  static unsigned int& goal(Problem& p) { return p.goal_; }
  static std::vector<Solution>& solutions(Problem& p) { return p.solutions_; }
  static int& sortedBy(Problem& p) { return p.sortedBy_; }
  static bool& solutionsWithRotations(Problem& p) { return p.solutionsWithRotations_; }
  static std::set<std::uint32_t>& colorConstraints(Problem& p) { return p.colorConstraints_; }
  static ProblemState& state(Problem& p) { return p.state_; }
  static std::uint64_t& numAssemblies(Problem& p) { return p.numAssemblies_; }
  static std::uint64_t& numSolutions(Problem& p) { return p.numSolutions_; }
  static std::uint64_t& usedMs(Problem& p) { return p.usedMs_; }
  static std::string& assemblerState(Problem& p) { return p.assemblerState_; }
  static std::string& assemblerVersion(Problem& p) { return p.assemblerVersion_; }
  static unsigned int& maxHoles(Problem& p) { return p.maxHoles_; }
  static std::uint64_t& uid(Problem& p) { return p.uid_; }
  static unsigned int resultRaw(const Problem& p) { return p.result_; }
};

}  // namespace sl

namespace sl::io::detail {

// ---- reading helpers ------------------------------------------------------------

/// A file the reader cannot accept; turned into a Result at the top.
class ReadError : public std::runtime_error {
 public:
  ReadError(const std::string& what, int line)
      : std::runtime_error(line > 0 ? what + " (line " + std::to_string(line) + ")" : what) {}
};

[[noreturn]] inline void error(const XmlElement& e, const std::string& what) {
  throw ReadError(what, e.line);
}

/// The original's atoi(): strtol cast to int.
inline int atoiOf(const std::string& s) {
  return static_cast<int>(std::strtol(s.c_str(), nullptr, 10));
}

inline unsigned long strtoulOf(const std::string& s) { return std::strtoul(s.c_str(), nullptr, 10); }

inline unsigned long long strtoullOf(const std::string& s) {
  return std::strtoull(s.c_str(), nullptr, 10);
}

/// Collects unknown attributes and child elements into a PreservedXml, with
/// each kept child's slot: one past the writer position of the last known
/// child before it.
class Keeper {
 public:
  explicit Keeper(PreservedXml& p) : p_(p) {}

  void attributes(const XmlElement& e, std::initializer_list<std::string_view> known) {
    for (const XmlAttribute& a : e.attributes) {
      if (std::find(known.begin(), known.end(), a.name) == known.end()) {
        p_.attributes.push_back(a);
      }
    }
  }

  /// A known child with writer position `pos` was read.
  void known(unsigned int pos) { slot_ = std::max(slot_, pos + 1); }

  /// An unknown child: keep it.
  void unknown(const XmlElement& c) { p_.children.push_back(PreservedXml::Child{slot_, c}); }

 private:
  PreservedXml& p_;
  unsigned int slot_ = 0;
};

// A file is untrusted (cleanup item 16.2): what it gives is bounded before
// it is used as a size, an index or a term of a sum.

/// How far from zero a position in a file may lie: a placement, a state's
/// offset, a hotspot, a rotation's pivot. Pieces lie within their result,
/// at most 65536 cells a side, and a piece taken off the puzzle stands a
/// few times movement::kRemovedScale away; sums and differences of a few
/// such numbers stay far inside an int.
inline constexpr int kMaxFileCoordinate = 1 << 20;

/// The most moves a file's sliding search may say it searched: each has an
/// entry in the problem's list of times. No search reaches this (a move
/// here would take a level of its own on the disk).
inline constexpr std::uint64_t kMaxSlideDepth = std::uint64_t{1} << 20;

/// The start or goal marks of a sliding puzzle as a file lists them: one
/// comma-separated number per cell. set(i, mark) is called for each cell
/// below `cells` that has a number.
template <class Set>
void readMarkList(const std::string& attr, unsigned int cells, Set set) {
  if (attr.empty()) {
    return;
  }
  unsigned int gi = 0;
  unsigned int val = 0;
  bool inNum = false;
  for (std::size_t pos = 0; pos <= attr.size(); pos++) {
    const char ch = pos < attr.size() ? attr[pos] : ',';
    if (ch >= '0' && ch <= '9') {
      val = val * 10 + static_cast<unsigned int>(ch - '0');
      inNum = true;
    } else if (ch == ',') {
      if (inNum && gi < cells) {
        set(gi, static_cast<unsigned char>(val));
      }
      gi++;
      val = 0;
      inNum = false;
    }
  }
}

// ---- problems -------------------------------------------------------------------

// Writer positions of the known children of <problem>.
enum ProblemSlot : unsigned int {
  kSlotParts = 0,
  kSlotResult,
  kSlotGoal,
  kSlotStacking,
  kSlotSlideMarks,
  kSlotSolverOptions,
  kSlotBitmap,
  kSlotAssembler,
  kSlotPending,
  kSlotSolutions,
};

// ---- the puzzle -----------------------------------------------------------------

// Writer positions of the known children of <puzzle>.
enum PuzzleSlot : unsigned int {
  kSlotGridType = 0,
  kSlotTray,
  kSlotColors,
  kSlotImages,
  kSlotShapes,
  kSlotRodSets,
  kSlotProblems,
  kSlotComment,
};

// ---- writing --------------------------------------------------------------------

inline void writeKept(XmlWriter& xml, const PreservedXml& p, unsigned int slot) {
  for (const PreservedXml::Child& c : p.children) {
    if (c.slot == slot) {
      xml.writeElement(c.element);
    }
  }
}

inline void writeKeptAttributes(XmlWriter& xml, const PreservedXml& p) {
  for (const XmlAttribute& a : p.attributes) {
    xml.newAttrib(a.name, a.value);
  }
}

// ---- each element's reader and writer (its file) ------------------------------

std::unique_ptr<Voxel> readVoxel(const XmlElement& e, GridType gt);

Assembly readAssembly(const XmlElement& e, unsigned int pieces, GridType gt);

Solution readSolution(const XmlElement& e, unsigned int pieces, GridType gt);

/// <rodSets>: each <rodSet> a board; anything else is skipped, as the
/// original did.
void readRodSets(const XmlElement& e, Puzzle& puzzle);

// Every problem a uid of its own (decision 0044), once the problems are as
// the file means them. A problem the file gives none gets one made from
// its place in the file and its definition, so that the same file opened
// again, or in another window, names its problems' searches as before,
// and two problems alike in every cell are told apart; so does a problem
// whose uid an earlier one has (copied by hand in the file). A problem
// paused by a version before uids keeps none, so its saved search, named
// without one, is still its own; of such problems with the same
// definition only the first does, as they shared one folder.
void giveUids(Puzzle& puzzle);

Problem readProblem(const XmlElement& e, const Puzzle& puzzle);

/// <tray>: the tray of a Tilted Square puzzle. Nothing when a setting is
/// missing or out of range (an edited file can hold anything); the element
/// is then kept as the file has it. A square tray has `size`, a rectangle
/// `width` and `height` (writeTray()); either of those two, when there,
/// goes over `size`.
std::optional<tilted::Tray> readTray(const XmlElement& e);

void writeVoxel(XmlWriter& xml, const Voxel& v);

void writeAssembly(XmlWriter& xml, const Assembly& a);

void writeSolution(XmlWriter& xml, const Solution& sol, bool includeRotationFields);

/// The puzzle's <tray> (decision 0045). A square tray is written with
/// `size`, as every tray was before there were rectangles, so those files
/// read and write as they did; a rectangle with `width` and `height`.
void writeTray(XmlWriter& xml, const tilted::Tray& t);

/// The puzzle's <rodSets> (stacking::saveRodSets): on a stacking puzzle, or
/// when there are any.
void writeRodSets(XmlWriter& xml, const Puzzle& puzzle);

void writeProblem(XmlWriter& xml, const Puzzle& puzzle, const Problem& prob);

}  // namespace sl::io::detail
