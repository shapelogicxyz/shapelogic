// SPDX-License-Identifier: GPL-3.0-or-later
// The <problem> element (puzzle_file_internal.h; cleanup item 21.5 moved
// it here from puzzle_file.cpp), with its stacking, its slide marks and its
// uid, and the puzzle's rod sets, read and written. Ported from the XML load
// and save code of src/lib/problem.cpp and the file part of
// src/lib/stacking.cpp (saveRodSets, loadRodSets, saveProblem,
// loadProblem, encodeRod, decodeRod, countAttribute).
#include "shapelogic/io/puzzle_file_internal.h"

#include "shapelogic/base/int_cast.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/model/stacking.h"

#include <array>
#include <cstdio>
#include <map>
#include <sstream>

namespace bt::io::detail {

namespace {

// ---- stacking (stacking.cpp) --------------------------------------------------

/// Rods, or discs a rod holds, that a file may ask for (MAX_FILE_COUNT).
constexpr unsigned int kMaxFileCount = 1000;

/// An attribute as a count from 0 to `most`; `fallback` when it is missing
/// or not a number in range (an edited file can hold anything).
unsigned int countAttribute(const XmlElement& e, const char* name, unsigned int fallback,
                            unsigned int most) {
  const std::string str = e.attribute(name);
  if (str.empty()) {
    return fallback;
  }
  char* end = nullptr;
  const long v = std::strtol(str.c_str(), &end, 10);
  if (end == str.c_str() || v < 0 || static_cast<unsigned long>(v) > most) {
    return fallback;
  }
  return static_cast<unsigned int>(v);
}

}  // namespace

void readRodSets(const XmlElement& e, Puzzle& puzzle) {
  for (const XmlElement& c : e.children) {
    if (c.name != "rodSet") {
      continue;
    }
    stacking::RodSet r;
    r.name = c.attribute("name");
    r.rodCount = std::max(1U, countAttribute(c, "rods", r.rodCount, kMaxFileCount));
    r.growHeight = countAttribute(c, "grow", r.growHeight ? 1U : 0U, 1) != 0;
    r.definedHeight = std::max(1U, countAttribute(c, "height", r.definedHeight, kMaxFileCount));
    r.sizeMatters = countAttribute(c, "sizeMatters", r.sizeMatters ? 1U : 0U, 1) != 0;
    r.distanceMatters = countAttribute(c, "distanceMatters", r.distanceMatters ? 1U : 0U, 1) != 0;
    r.canMoveOver = countAttribute(c, "moveOver", r.canMoveOver ? 1U : 0U, 1) != 0;
    r.panexColumns = countAttribute(c, "panex", r.panexColumns ? 1U : 0U, 1) != 0;
    const unsigned int pocket = countAttribute(c, "pocket", 0, kMaxFileCount);
    if (pocket > 0) {
      r.pocketColumn = true;
      r.pocketHeight = pocket;
    }
    r.outsideChannel = countAttribute(c, "channel", r.outsideChannel ? 1U : 0U, 1) != 0;
    puzzle.addRodSet(r);
  }
}

namespace {

/// A rod's discs, "shape:copy,shape:copy", read as the original's stream
/// read them (decodeRod): it stops at the first thing that is not one.
std::vector<stacking::DiskRef> decodeRod(const std::string& str) {
  std::vector<stacking::DiskRef> rod;
  if (str.empty()) {
    return rod;
  }
  std::istringstream in(str);
  while (in.good()) {
    unsigned int shape = 0;
    unsigned int inst = 0;
    char colon = 0;
    in >> shape >> colon >> inst;
    if (!in.fail() && colon == ':') {
      rod.push_back(stacking::DiskRef{shape, inst});
    }
    if (in.peek() == ',') {
      in.get();
    }
  }
  return rod;
}

/// <start> or <goal>: a <rod> per rod; anything else is skipped.
stacking::StackMap readStackMap(const XmlElement& e) {
  stacking::StackMap map;
  for (const XmlElement& c : e.children) {
    if (c.name == "rod") {
      map.rods.push_back(decodeRod(c.attribute("pieces")));
    }
  }
  return map;
}

/// A problem's <stacking>: its rod set and the start and goal stacks.
void readStacking(const XmlElement& e, Problem& prob) {
  const std::string id = e.attribute("rodSet");
  if (!id.empty()) {
    prob.setRodSetId(static_cast<unsigned int>(atoiOf(id)));
  }
  for (const XmlElement& c : e.children) {
    if (c.name == "start") {
      prob.editStart() = readStackMap(c);
    } else if (c.name == "goal") {
      prob.editGoal() = readStackMap(c);
    }
  }
}

/// The most cells a problem's <slideMarks> may name (a board is at most
/// 1024 cells each way; an edited file can hold anything).
constexpr unsigned int kMaxMarkCells = 1U << 30U;

/// A problem's <slideMarks>: its starts and goals on its board.
void readSlideMarks(const XmlElement& e, Problem& prob) {
  SlideMarks& marks = prob.editSlideMarks();
  marks.clear();
  for (int goal = 0; goal < 2; goal++) {
    // the last cell first, so the marks grow once
    std::vector<std::pair<unsigned int, unsigned int>> read;
    readMarkList(e.attribute(goal != 0 ? "goal" : "start"), kMaxMarkCells,
                 [&read](unsigned int i, unsigned int mark) {
                   if (mark != 0) {
                     read.emplace_back(i, mark);
                   }
                 });
    for (auto it = read.rbegin(); it != read.rend(); ++it) {
      marks.set(it->first, goal != 0, it->second);
    }
  }
}

}  // namespace

void giveUids(Puzzle& puzzle) {
  using A = ProblemFileAccess;
  std::set<std::uint64_t> seen;
  std::set<std::string> legacy;
  for (unsigned int i = 0; i < puzzle.getNumberOfProblems(); i++) {
    Problem& prob = puzzle.getProblem(i);
    std::uint64_t uid = prob.getUid();
    if (uid != 0 && seen.insert(uid).second) {
      continue;
    }
    Problem def = prob.copyDefinition();
    A::uid(def) = 0;
    XmlWriter xml;
    writeProblem(xml, puzzle, def);
    const std::string text = xml.str();
    if (uid == 0 && prob.getSolveState() == ProblemState::Solving &&
        legacy.insert(text).second) {
      continue;
    }
    // FNV-1a over the place, the definition and the uid it had
    std::uint64_t h = 14695981039346656037ULL;
    const auto mix = [&h](std::uint64_t v) {
      for (int b = 0; b < 8; b++) {
        h ^= (v >> (8 * b)) & 0xFF;
        h *= 1099511628211ULL;
      }
    };
    mix(i);
    mix(uid);
    for (const char ch : text) {
      mix(static_cast<unsigned char>(ch));
    }
    while (h == 0 || seen.contains(h)) {
      mix(h);
    }
    A::uid(prob) = h;
    seen.insert(h);
  }
}

Problem readProblem(const XmlElement& e, const Puzzle& puzzle) {
  using A = ProblemFileAccess;
  Problem prob;
  prob.setName(e.attribute("name"));

  Keeper keep(prob.preserved());
  keep.attributes(e, {"name", "uid", "state", "assemblies", "solutions", "time", "timeMs",
                      "searched", "searchDepth", "triedApart", "slideGrid", "slideLevelMs",
                      "maxHoles"});

  // 16 hex digits; anything else is no uid, which giveUids settles
  std::string str = e.attribute("uid");
  if (str.size() == 16 && std::ranges::all_of(str, [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
      })) {
    A::uid(prob) = std::stoull(str, nullptr, 16);
  }

  str = e.attribute("maxHoles");
  if (!str.empty()) {
    A::maxHoles(prob) = asUnsigned(atoiOf(str));
  }

  str = e.attribute("state");
  if (!str.empty()) {
    const int st = atoiOf(str);
    if (st == 1 || st == 2 || st == 3) {
      A::state(prob) = static_cast<ProblemState>(st);
    }
  }

  if (prob.getSolveState() != ProblemState::Unsolved) {
    // the original read these with atoi(); larger counts are kept here
    str = e.attribute("assemblies");
    if (!str.empty()) {
      A::numAssemblies(prob) = strtoullOf(str);
    }
    str = e.attribute("solutions");
    if (!str.empty()) {
      A::numSolutions(prob) = strtoullOf(str);
    }
    str = e.attribute("time");
    if (!str.empty()) {
      A::usedMs(prob) = 1000ull * strtoulOf(str);
    }
    str = e.attribute("timeMs");
    if (!str.empty()) {
      A::usedMs(prob) = strtoullOf(str);
    }
    std::uint64_t searched = 0;
    std::uint64_t depth = 0;
    std::uint64_t apart = 0;
    str = e.attribute("searched");
    if (!str.empty()) {
      searched = strtoullOf(str);
    }
    str = e.attribute("searchDepth");
    if (!str.empty()) {
      depth = strtoulOf(str);
    }
    str = e.attribute("triedApart");
    if (!str.empty()) {
      apart = strtoulOf(str);
    }
    prob.setSearchStats(searched, depth, apart);
    str = e.attribute("slideGrid");
    if (!str.empty()) {
      prob.setSlideGrid(static_cast<unsigned int>(strtoulOf(str)));
    }
    // each move's time, "-" for one not timed
    str = e.attribute("slideLevelMs");
    std::vector<std::uint64_t> ms;
    std::istringstream in(str);
    std::string v;
    while (in >> v) {
      ms.push_back(v == "-" ? kUntimedMs : strtoullOf(v));
    }
    // a sliding solve paused by a version that kept no times: the moves
    // searched are there, untimed, and the times go on from them
    // (the depth is what the file says: past any a search reaches, no list
    // is made for it, which would be gigabytes; the search pads its times
    // itself when it carries on)
    if (ms.empty() && depth > 0 && depth <= kMaxSlideDepth &&
        prob.getSolveState() == ProblemState::Solving && sliding::isSliding(puzzle)) {
      ms.assign(static_cast<std::size_t>(depth), kUntimedMs);
    }
    prob.setSlideLevelMs(std::move(ms));
  }

  unsigned int pieces = 0;

  for (const XmlElement& c : e.children) {
    if (c.name == "shapes") {
      // the parts are saved as <shape> for historical reasons
      keep.known(kSlotParts);
      for (const XmlElement& s : c.children) {
        if (s.name != "shape") {
          continue;
        }

        str = s.attribute("id");
        if (str.empty()) {
          error(s, "a shape node must have an 'idt' attribute");
        }
        const auto id = static_cast<unsigned short>(atoiOf(str));

        unsigned short min = 0;
        unsigned short max = 0;
        str = s.attribute("count");
        if (!str.empty()) {
          min = max = static_cast<unsigned short>(atoiOf(str));
        } else if (!s.attribute("min").empty() && !s.attribute("max").empty()) {
          min = static_cast<unsigned short>(atoiOf(s.attribute("min")));
          max = static_cast<unsigned short>(atoiOf(s.attribute("max")));
          if (min > max) {
            error(s, "min of shape count must by <= max");
          }
        } else {
          min = max = 1;
        }

        const auto grp = static_cast<unsigned short>(atoiOf(s.attribute("group")));

        pieces += max;

        if (id >= puzzle.getNumberOfShapes()) {
          error(s, "the shape ids must be for valid shapes");
        }

        /* the groups are either one group attribute (all pieces in it) or a
         * list of <group> elements with a group and a count each */
        Part part{id, min, max, {}};
        if (grp != 0) {
          part.groups.push_back(PartGroup{grp, max});
        } else {
          for (const XmlElement& g : s.children) {
            if (g.name != "group") {
              continue;
            }
            str = g.attribute("group");
            if (str.empty()) {
              error(g, "a group node must have a valid group attribute");
            }
            const auto gr = static_cast<unsigned short>(atoiOf(str));
            str = g.attribute("count");
            if (str.empty()) {
              error(g, "a group node must have a valid count attribute");
            }
            part.groups.push_back(PartGroup{gr, static_cast<unsigned short>(atoiOf(str))});
          }
        }
        A::parts(prob).push_back(std::move(part));
      }
    } else if (c.name == "result") {
      keep.known(kSlotResult);
      str = c.attribute("id");
      if (str.empty()) {
        error(c, "the result node must have an 'id' attribute with content");
      }
      A::result(prob) = asUnsigned(atoiOf(str));
    } else if (c.name == "goal") {
      keep.known(kSlotGoal);
      str = c.attribute("id");
      if (str.empty()) {
        error(c, "the goal node must have an 'id' attribute with content");
      }
      A::goal(prob) = asUnsigned(atoiOf(str));
    } else if (c.name == "stacking") {
      keep.known(kSlotStacking);
      readStacking(c, prob);
    } else if (c.name == "slideMarks") {
      keep.known(kSlotSlideMarks);
      readSlideMarks(c, prob);
    } else if (c.name == "solutions" || c.name == "solutionsWithRotations") {
      keep.known(kSlotSolutions);
      if (c.name == "solutionsWithRotations") {
        // the rotation-aware block wins; drop classic solutions read before it
        A::solutionsWithRotations(prob) = true;
        A::solutions(prob).clear();
        A::sortedBy(prob) = -1;
      } else if (prob.getSolutionsWithRotations()) {
        continue;  // a classic block after the rotation-aware one is ignored
      }
      for (const XmlElement& s : c.children) {
        if (s.name == "solution") {
          A::solutions(prob).push_back(readSolution(s, pieces, puzzle.getGridType()));
        }
      }
      prob.assignSolutionIds();
    } else if (c.name == "bitmap") {
      keep.known(kSlotBitmap);
      for (const XmlElement& p : c.children) {
        if (p.name != "pair") {
          continue;
        }
        str = p.attribute("piece");
        if (str.empty()) {
          error(p, "a pair node must have a piece attribute");
        }
        const auto piece = asUnsigned(atoiOf(str));
        str = p.attribute("result");
        if (str.empty()) {
          error(p, "a pair node must have a result attribute");
        }
        const auto result = asUnsigned(atoiOf(str));
        A::colorConstraints(prob).insert(piece << 16 | result);
      }
    } else if (c.name == "solverOptions") {
      keep.known(kSlotSolverOptions);
      std::map<std::string, std::string> opts;
      for (const XmlAttribute& a : c.attributes) {
        opts[a.name] = a.value;
      }
      prob.setSolverOptions(opts);
    } else if (c.name == "pending") {
      keep.known(kSlotPending);
      for (const XmlElement& item : c.children) {
        if (item.name != "item") {
          continue;
        }
        std::optional<Assembly> assembly;
        for (const XmlElement& a : item.children) {
          if (a.name == "assembly") {
            assembly = readAssembly(a, pieces, puzzle.getGridType());
          }
        }
        // an item without an assembly is dropped
        if (assembly) {
          prob.addPending(PendingAssembly{std::move(*assembly),
                                          atoiOf(item.attribute("counted")) != 0,
                                          strtoullOf(item.attribute("assembly")),
                                          strtoullOf(item.attribute("solution"))});
        }
      }
    } else if (c.name == "assembler") {
      keep.known(kSlotAssembler);
      // the saved search position; without a version it is useless
      str = c.attribute("version");
      if (!str.empty()) {
        A::assemblerVersion(prob) = str;
        A::assemblerState(prob) = c.text;
      }
    } else {
      keep.unknown(c);
    }
  }

  // Every assembly places all the problem's pieces. Each was read against
  // the parts that stood before it; a file that lists parts after its
  // solutions would leave them short, and what walks a problem's pieces
  // reads an assembly's placements by their number.
  for (const Solution& sol : A::solutions(prob)) {
    if (sol.getAssembly().placementCount() != pieces) {
      error(e, "not the right number of placements in assembly");
    }
  }
  for (const PendingAssembly& p : prob.getPending()) {
    if (p.assembly.placementCount() != pieces) {
      error(e, "not the right number of placements in assembly");
    }
  }

  // a shape id that is not right is reset to "none"
  if (A::result(prob) >= puzzle.getNumberOfShapes()) {
    A::result(prob) = kNoShape;
  }
  if (A::goal(prob) >= puzzle.getNumberOfShapes()) {
    A::goal(prob) = kNoShape;
  }

  return prob;
}

namespace {

/// A rod's discs as the file holds them (encodeRod).
std::string encodeRod(const std::vector<stacking::DiskRef>& rod) {
  std::string str;
  for (std::size_t i = 0; i < rod.size(); i++) {
    if (i != 0) {
      str += ',';
    }
    str += std::to_string(rod[i].shapeId);
    str += ':';
    str += std::to_string(rod[i].instance);
  }
  return str;
}

void writeStackMap(XmlWriter& xml, const char* tag, const stacking::StackMap& map) {
  xml.newTag(tag);
  for (const std::vector<stacking::DiskRef>& rod : map.rods) {
    xml.newTag("rod");
    const std::string body = encodeRod(rod);
    if (!body.empty()) {
      xml.newAttrib("pieces", body);
    }
    xml.endTag("rod");
  }
  xml.endTag(tag);
}

/// A problem's <stacking> (stacking::saveProblem): written when it has a
/// rod set of the puzzle or stacks; a rod set the puzzle does not have is
/// not written.
void writeStacking(XmlWriter& xml, const Puzzle& puzzle, const Problem& prob) {
  const bool valid = prob.rodSetValid(puzzle);
  if (!valid && prob.startStacks().rods.empty() && prob.goalStacks().rods.empty()) {
    return;
  }
  xml.newTag("stacking");
  if (valid) {
    xml.newAttrib("rodSet", prob.getRodSetId());
  }
  writeStackMap(xml, "start", prob.startStacks());
  writeStackMap(xml, "goal", prob.goalStacks());
  xml.endTag("stacking");
}

/// A problem's <slideMarks>: the starts and goals of a sliding problem on
/// its board, a comma-separated mark per cell up to the last marked one
/// (a mark is a piece's shape index + 1; 0 for none). Written when it has
/// any.
void writeSlideMarks(XmlWriter& xml, const Problem& prob) {
  const SlideMarks& marks = prob.slideMarks();
  if (marks.empty()) {
    return;
  }
  xml.newTag("slideMarks");
  for (int goal = 0; goal < 2; goal++) {
    if (!marks.any(goal != 0)) {
      continue;
    }
    std::string list;
    for (std::size_t i = 0; i < marks.size(goal != 0); i++) {
      if (i != 0) {
        list += ',';
      }
      list += std::to_string(marks.get(i, goal != 0));
    }
    xml.newAttrib(goal != 0 ? "goal" : "start", list);
  }
  xml.endTag("slideMarks");
}

}  // namespace

void writeRodSets(XmlWriter& xml, const Puzzle& puzzle) {
  if (puzzle.getGridType().getType() != GridKind::Stacking && puzzle.rodSetCount() == 0) {
    return;
  }
  xml.newTag("rodSets");
  for (unsigned int i = 0; i < puzzle.rodSetCount(); i++) {
    const stacking::RodSet& r = puzzle.getRodSet(i);
    xml.newTag("rodSet");
    if (!r.name.empty()) {
      xml.newAttrib("name", r.name);
    }
    xml.newAttrib("rods", r.rodCount);
    xml.newAttrib("grow", r.growHeight ? 1U : 0U);
    xml.newAttrib("height", r.definedHeight);
    xml.newAttrib("sizeMatters", r.sizeMatters ? 1U : 0U);
    xml.newAttrib("distanceMatters", r.distanceMatters ? 1U : 0U);
    xml.newAttrib("moveOver", r.canMoveOver ? 1U : 0U);
    if (r.panexColumns) {
      xml.newAttrib("panex", 1U);
    }
    if (r.pocketColumn) {
      xml.newAttrib("pocket", r.pocketHeight);
    }
    if (r.outsideChannel) {
      xml.newAttrib("channel", 1U);
    }
    xml.endTag("rodSet");
  }
  xml.endTag("rodSets");
}

void writeProblem(XmlWriter& xml, const Puzzle& puzzle, const Problem& prob) {
  const PreservedXml& kept = prob.preserved();

  xml.newTag("problem");

  if (!prob.getName().empty()) {
    xml.newAttrib("name", prob.getName());
  }
  if (prob.getUid() != 0) {
    char uid[17];
    std::snprintf(uid, sizeof(uid), "%016llx", static_cast<unsigned long long>(prob.getUid()));
    xml.newAttrib("uid", uid);
  }

  xml.newAttrib("state", static_cast<int>(prob.getSolveState()));

  if (prob.getSolveState() != ProblemState::Unsolved) {
    xml.newAttrib("assemblies", prob.getNumAssemblies());
    xml.newAttrib("solutions", prob.getNumSolutions());
    xml.newAttrib("time", prob.getUsedMs() / 1000);
    // the same to the millisecond; older versions read only "time"
    xml.newAttrib("timeMs", prob.getUsedMs());
    if (prob.getSearched() != 0) {
      xml.newAttrib("searched", prob.getSearched());
    }
    if (prob.getSearchDepth() != 0) {
      xml.newAttrib("searchDepth", prob.getSearchDepth());
    }
    if (prob.getTriedApart() != 0) {
      xml.newAttrib("triedApart", prob.getTriedApart());
    }
    if (prob.getSlideGrid() != 0) {
      xml.newAttrib("slideGrid", prob.getSlideGrid());
    }
    if (!prob.getSlideLevelMs().empty()) {
      std::string ms;
      for (const std::uint64_t v : prob.getSlideLevelMs()) {
        ms += (ms.empty() ? "" : " ") + (v == kUntimedMs ? std::string("-") : std::to_string(v));
      }
      xml.newAttrib("slideLevelMs", ms);
    }
  }

  if (prob.maxHolesDefined()) {
    xml.newAttrib("maxHoles", prob.getMaxHoles());
  }

  writeKeptAttributes(xml, kept);

  // the parts are saved as <shape> for historical reasons
  writeKept(xml, kept, kSlotParts);
  xml.newTag("shapes");
  for (const Part& part : prob.getParts()) {
    xml.newTag("shape");
    xml.newAttrib("id", part.shapeId);

    if (part.min == part.max) {
      xml.newAttrib("count", part.min);
    } else {
      xml.newAttrib("min", part.min);
      xml.newAttrib("max", part.max);
    }

    if (part.groups.size() == 1 && part.groups[0].count == part.max) {
      // all pieces in one group: an attribute, and none for group 0
      if (part.groups[0].group != 0) {
        xml.newAttrib("group", part.groups[0].group);
      }
    } else {
      for (const PartGroup& g : part.groups) {
        if (g.group != 0) {
          xml.newTag("group");
          xml.newAttrib("group", g.group);
          xml.newAttrib("count", g.count);
          xml.endTag("group");
        }
      }
    }
    xml.endTag("shape");
  }
  xml.endTag("shapes");

  writeKept(xml, kept, kSlotResult);
  xml.newTag("result");
  xml.newAttrib("id", ProblemFileAccess::resultRaw(prob));
  xml.endTag("result");

  writeKept(xml, kept, kSlotGoal);
  if (prob.goalValid()) {
    xml.newTag("goal");
    xml.newAttrib("id", prob.getGoalId());
    xml.endTag("goal");
  }

  writeKept(xml, kept, kSlotStacking);
  writeStacking(xml, puzzle, prob);

  writeKept(xml, kept, kSlotSlideMarks);
  writeSlideMarks(xml, prob);

  writeKept(xml, kept, kSlotSolverOptions);
  if (!prob.getSolverOptions().empty()) {
    xml.newTag("solverOptions");
    for (const auto& [name, value] : prob.getSolverOptions()) {
      xml.newAttrib(name, value);
    }
    xml.endTag("solverOptions");
  }

  writeKept(xml, kept, kSlotBitmap);
  xml.newTag("bitmap");
  for (const std::uint32_t c : prob.getColorConstraints()) {
    xml.newTag("pair");
    xml.newAttrib("piece", c >> 16);
    xml.newAttrib("result", c & 0xFFFF);
    xml.endTag("pair");
  }
  xml.endTag("bitmap");

  writeKept(xml, kept, kSlotAssembler);
  if (prob.getSolveState() == ProblemState::Solving && !prob.getAssemblerState().empty() &&
      !prob.getAssemblerVersion().empty()) {
    xml.newTag("assembler");
    xml.newAttrib("version", prob.getAssemblerVersion());
    xml.addContent(prob.getAssemblerState());
    xml.endTag("assembler");
  }

  // unfinished work of a paused solve; older versions skip the element
  writeKept(xml, kept, kSlotPending);
  if (prob.getSolveState() == ProblemState::Solving && !prob.getPending().empty()) {
    xml.newTag("pending");
    for (const PendingAssembly& p : prob.getPending()) {
      xml.newTag("item");
      xml.newAttrib("counted", p.counted ? 1u : 0u);
      xml.newAttrib("assembly", p.assemblyNumber);
      xml.newAttrib("solution", p.solutionNumber);
      writeAssembly(xml, p.assembly);
      xml.endTag("item");
    }
    xml.endTag("pending");
  }

  writeKept(xml, kept, kSlotSolutions);
  if (prob.getNumberOfSavedSolutions() != 0) {
    /* rotation-aware solutions go into a sibling element that older versions
     * skip; classic <solutions> stay free of <dt>/<rotation> */
    const bool rot = prob.getSolutionsWithRotations();
    const char* tag = rot ? "solutionsWithRotations" : "solutions";
    xml.newTag(tag);
    for (unsigned int i = 0; i < prob.getNumberOfSavedSolutions(); i++) {
      writeSolution(xml, prob.getSavedSolution(i), rot);
    }
    xml.endTag(tag);
  }
  writeKept(xml, kept, kSlotSolutions + 1);

  xml.endTag("problem");
}

}  // namespace bt::io::detail
