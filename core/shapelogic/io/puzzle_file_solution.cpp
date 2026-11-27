// SPDX-License-Identifier: GPL-3.0-or-later
// The <solution> element with its <assembly>, <separation> and
// <separationInfo> (puzzle_file_internal.h; cleanup item 21.5 moved them
// here from puzzle_file.cpp), read and written. Ported from the XML load and
// save code of src/lib/solution.cpp, assembly.cpp and disassembly.cpp.
#include "shapelogic/io/puzzle_file_internal.h"

#include "shapelogic/base/int_cast.h"

#include <array>
#include <cstdio>
#include <sstream>

namespace bt::io::detail {

namespace {

const std::string& requireText(const XmlElement& e, const char* what) {
  if (e.text.empty()) {
    error(e, what);
  }
  return e.text;
}

/// Space-separated integers, exactly `out.size()` of them (getNumbers()),
/// none further from zero than kMaxFileCoordinate.
void getNumbers(const XmlElement& e, const std::string& str, std::vector<int>& out,
                bool negAllowed) {
  std::size_t at = 0;
  int val = 0;
  bool gotNum = false;
  bool negative = false;

  for (const char c : str) {
    if (c == '-' && negAllowed) {
      // only one '-', and before the digits
      if (negative || gotNum) {
        error(e, "too many '-' signs in disassembly number");
      }
      negative = true;
    } else if (c >= '0' && c <= '9') {
      // (bounded at each digit: the next one cannot take it past an int)
      val = val * 10 + c - '0';
      if (val > kMaxFileCoordinate) {
        error(e, "number out of range in disassembly");
      }
      gotNum = true;
    } else if (c == ' ') {
      if (gotNum) {
        if (at == out.size()) {
          error(e, "too many numbers in disassembly");
        }
        out[at++] = negative ? -val : val;
        val = 0;
        gotNum = negative = false;
      }
      if (negative) {
        error(e, "only '-' encountered in disassembly");
      }
    } else {
      error(e, "not allowed character in disassembly");
    }
  }

  // a last number not followed by a space
  if (gotNum) {
    if (at == out.size()) {
      error(e, "too many numbers in disassembly");
    }
    out[at++] = negative ? -val : val;
  }

  if (at != out.size()) {
    error(e, "too few number in disassembly");
  }
}

}  // namespace

// ---- assemblies, disassemblies, solutions ---------------------------------------

Assembly readAssembly(const XmlElement& e, unsigned int pieces, GridType gt) {
  const std::string& str = requireText(e, "expected: TEXT");
  const unsigned int numTrans = gt.getSymmetries().getNumTransformations();

  Assembly a(gt);

  int x = 0;
  int y = 0;
  int z = 0;
  int trans = 0;
  int state = 0;
  int sign = 1;

  auto addPlacement = [&]() {
    if (trans != kUnplacedTrans && (trans < 0 || asUnsigned(trans) >= numTrans)) {
      error(e, "transformations need to be between 0 and NUM_TRANSFORMATIONS");
    }
    a.addRawPlacement(Placement{static_cast<unsigned char>(trans), x, y, z});
  };

  // a number with an optional leading '-', as the original's state machine
  auto digit = [&](int& value, char c) {
    if (c == '-') {
      if (value != 0) {
        error(e, "assembly placements minus in number");
      }
      sign = -1;
    } else {
      // (bounded at each digit: the next one cannot take it past an int)
      value *= 10;
      value += sign * (c - '0');
      if (value < -kMaxFileCoordinate || value > kMaxFileCoordinate) {
        error(e, "assembly placement out of range");
      }
    }
  };

  for (const char c : str) {
    if (c == ' ') {
      if (state == 3) {
        addPlacement();
        x = y = z = trans = state = 0;
      } else {
        sign = 1;
        state++;
      }
    } else if (c == 'x' && state == 0) {
      x = y = z = 0;
      trans = kUnplacedTrans;
      state = 3;
    } else {
      if (c != '-' && (c < '0' || c > '9')) {
        error(e, "non number character found where number is expected");
      }
      switch (state) {
        case 0:
          digit(x, c);
          break;
        case 1:
          digit(y, c);
          break;
        case 2:
          digit(z, c);
          break;
        case 3:
          digit(trans, c);
          break;
        default:
          break;
      }
    }
  }

  if (state != 3) {
    error(e, "not the right number of numbers in assembly");
  }
  addPlacement();

  if (a.placementCount() != pieces) {
    error(e, "not the right number of placements in assembly");
  }
  return a;
}

namespace {

/// A rotation's pivot, in half cells; an error when it is no position a
/// file may give (the conversion of such a number to int is undefined).
int parseDoubledPivot(const XmlElement& e, const std::string& s) {
  const double doubled = std::floor(std::strtod(s.c_str(), nullptr) * 2.0 + 0.5);
  if (!(doubled >= -kMaxFileCoordinate && doubled <= kMaxFileCoordinate)) {
    error(e, "rotation pivot out of range");
  }
  return static_cast<int>(doubled);
}

State readState(const XmlElement& e, unsigned int pn, GridType gt) {
  State st = State::withoutOrientations(pn);

  bool hasDx = false;
  bool hasDy = false;
  bool hasDz = false;

  for (const XmlElement& c : e.children) {
    if (c.name == "dx" || c.name == "dy" || c.name == "dz" || c.name == "dt") {
      std::vector<int> v(pn);
      getNumbers(c, c.text, v, c.name != "dt");
      if (c.name == "dx") {
        st.setXs(std::move(v));
        hasDx = true;
      } else if (c.name == "dy") {
        st.setYs(std::move(v));
        hasDy = true;
      } else if (c.name == "dz") {
        st.setZs(std::move(v));
        hasDz = true;
      } else {
        // an orientation is a transformation of the grid: it picks a row
        // of its tables
        const unsigned int orientations = gt.getSymmetries().getNumTransformationsMirror();
        for (const int t : v) {
          if (asUnsigned(t) >= orientations) {
            error(c, "orientation out of range in disassembly");
          }
        }
        st.setOrientations(std::move(v));
      }
    } else if (c.name == "rotation") {
      const std::string piece = c.attribute("piece");
      if (piece.empty()) {
        error(c, "rotation needs piece");
      }
      st.setRotationArrival(asUnsigned(atoiOf(piece)), parseDoubledPivot(c, c.attribute("px")),
                            parseDoubledPivot(c, c.attribute("py")),
                            parseDoubledPivot(c, c.attribute("pz")),
                            asUnsigned(atoiOf(c.attribute("axis"))),
                            asUnsigned(atoiOf(c.attribute("sense"))));
    }
  }

  if (!hasDx || !hasDy || !hasDz) {
    error(e, "disassembly state needs dx, dy and dz subnode");
  }
  return st;
}

/// A <separation> of `pieceCnt` pieces, `depth` branches below the root's;
/// the pieces it names are among the `allPieces` of its assembly.
Separation readSeparation(const XmlElement& e, unsigned int pieceCnt, unsigned int allPieces,
                          GridType gt, unsigned int depth = 0) {
  if (depth >= kMaxSeparationDepth) {
    error(e, "the disassembly nests too deeply");
  }
  std::vector<unsigned int> pieces;
  std::vector<State> states;
  ClonePtr<Separation> removed;
  ClonePtr<Separation> left;
  unsigned int piecenumber = 0;
  unsigned int removedPc = 0;
  unsigned int leftPc = 0;

  for (const XmlElement& c : e.children) {
    if (c.name == "pieces") {
      const std::string count = c.attribute("count");
      if (count.empty()) {
        error(c, "pieces node needs a 'count' attribute");
      }
      piecenumber = asUnsigned(atoiOf(count));
      if (piecenumber != pieceCnt) {
        error(c, "the number of pieces in the count array is not as expected");
      }
      std::vector<int> v(piecenumber);
      getNumbers(c, requireText(c, "expected: TEXT"), v, false);
      // a piece's number is its place in the assembly: the playback and
      // the text output index their arrays with it
      for (const int name : v) {
        if (asUnsigned(name) >= allPieces) {
          error(c, "the disassembly names a piece the assembly does not have");
        }
      }
      pieces.assign(v.begin(), v.end());
    } else if (c.name == "state") {
      if (piecenumber == 0) {
        error(c, "there must be a pieces array before the states");
      }
      if (removed || left) {
        error(c, "there are states behind the sub separations");
      }
      states.push_back(readState(c, piecenumber, gt));
    } else if (c.name == "separation") {
      if (states.empty()) {
        error(c, "there are no state nodes in the separation");
      }
      if (removedPc == 0) {
        for (unsigned int i = 0; i < pieceCnt; i++) {
          if (states.back().pieceRemoved(i)) {
            removedPc++;
          } else {
            leftPc++;
          }
        }
        if (removedPc == 0 || leftPc == 0) {
          error(c, "there need to be pieces in both parts of the tree");
        }
      }

      const std::string type = c.attribute("type");
      if (type.empty()) {
        error(c, "sub-sepatations need to have a type field");
      }
      if (type == "left") {
        if (left) {
          error(c, "more than one left branch in disassembly");
        }
        left = ClonePtr<Separation>(
            std::make_unique<Separation>(readSeparation(c, leftPc, allPieces, gt, depth + 1)));
      } else if (type == "removed") {
        if (removed) {
          error(c, "more than one removed branch in disassembly");
        }
        removed = ClonePtr<Separation>(
            std::make_unique<Separation>(readSeparation(c, removedPc, allPieces, gt, depth + 1)));
      } else {
        error(c, "subnodes must have either left or removed type");
      }
    }
  }

  if (states.empty()) {
    error(e, "there are no state nodes in the separation");
  }

  Separation sep(std::move(removed), std::move(left), std::move(pieces));
  for (State& st : states) {
    sep.appendState(std::move(st));
  }
  return sep;
}

SeparationInfo readSeparationInfo(const XmlElement& e) {
  const std::string& str = requireText(e, "expected: TEXT");

  std::vector<unsigned int> values;
  std::vector<unsigned int> rotValues;
  std::size_t pos = 0;
  unsigned int num = 0;
  bool inRots = false;

  while (pos < str.size()) {
    if (str[pos] == '|') {
      inRots = true;
      pos++;
      while (pos < str.size() && str[pos] == ' ') {
        pos++;
      }
      continue;
    }

    if (str[pos] == ' ') {
      (inRots ? rotValues : values).push_back(num);
      num = 0;
    }

    if (str[pos] >= '0' && str[pos] <= '9') {
      num = 10 * num + static_cast<unsigned int>(str[pos] - '0');
    }

    pos++;
  }

  (inRots ? rotValues : values).push_back(num);

  if (rotValues.empty()) {
    rotValues.assign(values.size(), 0);
  } else if (rotValues.size() != values.size()) {
    error(e, "separationInfo rotation counts do not match value counts");
  }

  // The tree must end: a node opens one more branch, a 0 closes one. And
  // it nests no deeper than a disassembly may (the level text walks it by
  // recursion): `open` holds, for each node on the way down to the value
  // at hand, how many of its two branches are still to come.
  unsigned int branches = 1;
  std::size_t at = 0;
  std::vector<unsigned char> open;
  while (branches != 0) {
    if (at >= values.size()) {
      error(e, "the tree in the disassemblyInformation tag is incomplete");
    }
    if (!open.empty()) {
      open.back()--;
    }
    if (values[at] != 0) {
      branches++;
      open.push_back(2);
      if (open.size() > kMaxSeparationDepth) {
        error(e, "the disassembly nests too deeply");
      }
    } else {
      branches--;
    }
    while (!open.empty() && open.back() == 0) {
      open.pop_back();
    }
    at++;
  }

  return SeparationInfo(std::move(values), std::move(rotValues));
}

}  // namespace

Solution readSolution(const XmlElement& e, unsigned int pieces, GridType gt) {
  // 64 bits, as the counts: a solve can find more than 2^32 assemblies
  const std::uint64_t assemblyNum = strtoullOf(e.attribute("asmNum"));
  const std::uint64_t solutionNum = strtoullOf(e.attribute("solNum"));

  std::optional<Assembly> assembly;
  std::optional<Separation> tree;
  std::optional<SeparationInfo> treeInfo;
  PreservedXml preserved;
  Keeper keep(preserved);
  keep.attributes(e, {"asmNum", "solNum"});

  for (const XmlElement& c : e.children) {
    if (c.name == "assembly") {
      assembly = readAssembly(c, pieces, gt);
      keep.known(0);
    } else if (c.name == "separation") {
      if (!assembly) {
        error(c, "an assembly must always be before a separation in a solution");
      }
      // the number of pieces really placed
      unsigned int pl = 0;
      for (unsigned int i = 0; i < assembly->placementCount(); i++) {
        if (assembly->isPlaced(i)) {
          pl++;
        }
      }
      tree = readSeparation(c, pl, assembly->placementCount(), gt);
      keep.known(1);
    } else if (c.name == "separationInfo") {
      treeInfo = readSeparationInfo(c);
      keep.known(1);
    } else {
      keep.unknown(c);
    }
  }

  if (!assembly) {
    error(e, "no assembly in solution");
  }

  // a full tree wins over its summary
  std::optional<Solution> sol;
  if (tree) {
    sol.emplace(std::move(*assembly), assemblyNum, std::move(*tree), solutionNum);
  } else if (treeInfo) {
    sol.emplace(std::move(*assembly), assemblyNum, std::move(*treeInfo), solutionNum);
  } else {
    // the number is kept (it sorts), though it is only written with a disassembly
    sol.emplace(std::move(*assembly), assemblyNum, solutionNum);
  }
  sol->preserved() = std::move(preserved);
  return std::move(*sol);
}

void writeAssembly(XmlWriter& xml, const Assembly& a) {
  xml.newTag("assembly");

  std::string str;
  const std::vector<Placement>& pl = a.getPlacements();
  for (std::size_t i = 0; i < pl.size(); i++) {
    if (pl[i].transformation != kUnplacedTrans) {
      str += std::to_string(pl[i].xpos) + " " + std::to_string(pl[i].ypos) + " " +
             std::to_string(pl[i].zpos) + " " + std::to_string(pl[i].transformation);
    } else {
      str += 'x';
    }
    if (i + 1 < pl.size()) {
      str += ' ';
    }
  }
  xml.addContent(str);

  xml.endTag("assembly");
}

namespace {

std::string joinNumbers(const std::vector<int>& v, unsigned int n) {
  std::string s;
  for (unsigned int i = 0; i < n; i++) {
    s += std::to_string(v[i]);
    if (i + 1 < n) {
      s += ' ';
    }
  }
  return s;
}

std::string doubledPivotAttr(int doubled) {
  std::array<char, 32> buf{};
  // NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
  if (doubled % 2 == 0) {
    std::snprintf(buf.data(), buf.size(), "%d", doubled / 2);
  } else {
    std::snprintf(buf.data(), buf.size(), "%.1f", doubled * 0.5);
  }
  // NOLINTEND(cppcoreguidelines-pro-type-vararg)
  return buf.data();
}

void writeState(XmlWriter& xml, const State& st, unsigned int piecenumber,
                bool includeRotationFields) {
  xml.newTag("state");

  xml.newTag("dx");
  xml.addContent(joinNumbers(st.xs(), piecenumber));
  xml.endTag("dx");

  xml.newTag("dy");
  xml.addContent(joinNumbers(st.ys(), piecenumber));
  xml.endTag("dy");

  xml.newTag("dz");
  xml.addContent(joinNumbers(st.zs(), piecenumber));
  xml.endTag("dz");

  /* only in <solutionsWithRotations>: older versions cannot skip unknown
   * elements inside <state>, so classic <solutions> keep dx/dy/dz only */
  if (includeRotationFields) {
    if (st.hasOrientations()) {
      xml.newTag("dt");
      xml.addContent(joinNumbers(st.orientations(), piecenumber));
      xml.endTag("dt");
    }

    if (st.isRotationArrival()) {
      xml.newTag("rotation");
      xml.newAttrib("piece", st.getRotPiece());
      xml.newAttrib("px", doubledPivotAttr(st.getRotPivotX()));
      xml.newAttrib("py", doubledPivotAttr(st.getRotPivotY()));
      xml.newAttrib("pz", doubledPivotAttr(st.getRotPivotZ()));
      xml.newAttrib("axis", st.getRotAxis());
      xml.newAttrib("sense", st.getRotSense());
      xml.endTag("rotation");
    }
  }

  xml.endTag("state");
}

void writeSeparation(XmlWriter& xml, const Separation& sep, int type,
                     bool includeRotationFields) {
  xml.newTag("separation");

  if (type == 1) {
    xml.newAttrib("type", "left");
  } else if (type == 2) {
    xml.newAttrib("type", "removed");
  }

  xml.newTag("pieces");
  xml.newAttrib("count", asInt(sep.getPieceNumber()));
  for (unsigned int i = 0; i < sep.getPieceNumber(); i++) {
    xml.addContent(sep.getPieceName(i));
    if (i + 1 < sep.getPieceNumber()) {
      xml.addContent(" ");
    }
  }
  xml.endTag("pieces");

  for (unsigned int i = 0; i < sep.getNumStates(); i++) {
    writeState(xml, sep.getState(i), sep.getPieceNumber(), includeRotationFields);
  }

  // the removed and the remaining part, told apart by their type
  if (sep.getRemoved() != nullptr) {
    writeSeparation(xml, *sep.getRemoved(), 2, includeRotationFields);
  }
  if (sep.getLeft() != nullptr) {
    writeSeparation(xml, *sep.getLeft(), 1, includeRotationFields);
  }

  xml.endTag("separation");
}

void writeSeparationInfo(XmlWriter& xml, const SeparationInfo& info,
                         bool includeRotationCounts) {
  xml.newTag("separationInfo");

  const std::vector<unsigned int>& values = info.getValues();
  const std::vector<unsigned int>& rotValues = info.getRotValues();

  for (std::size_t i = 0; i < values.size(); i++) {
    xml.addContent(values[i]);
    if (i + 1 < values.size()) {
      xml.addContent(" ");
    }
  }

  if (includeRotationCounts &&
      std::any_of(rotValues.begin(), rotValues.end(), [](unsigned int r) { return r != 0; })) {
    xml.addContent(" | ");
    for (std::size_t i = 0; i < rotValues.size(); i++) {
      xml.addContent(rotValues[i]);
      if (i + 1 < rotValues.size()) {
        xml.addContent(" ");
      }
    }
  }

  xml.endTag("separationInfo");
}

}  // namespace

void writeSolution(XmlWriter& xml, const Solution& sol, bool includeRotationFields) {
  xml.newTag("solution");

  if (sol.getAssemblyNumber() != 0) {
    xml.newAttrib("asmNum", sol.getAssemblyNumber());
  }

  if (sol.getDisassemblyInfo() != nullptr && sol.getSolutionNumber() != 0) {
    xml.newAttrib("solNum", sol.getSolutionNumber());
  }

  writeKeptAttributes(xml, sol.preserved());

  writeKept(xml, sol.preserved(), 0);
  writeAssembly(xml, sol.getAssembly());
  writeKept(xml, sol.preserved(), 1);

  if (sol.getDisassembly() != nullptr) {
    writeSeparation(xml, *sol.getDisassembly(), 0, includeRotationFields);
  } else if (sol.getSeparationInfo() != nullptr) {
    writeSeparationInfo(xml, *sol.getSeparationInfo(), includeRotationFields);
  }
  writeKept(xml, sol.preserved(), 2);

  xml.endTag("solution");
}

}  // namespace bt::io::detail
