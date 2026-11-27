// SPDX-License-Identifier: GPL-3.0-or-later
// The names of the saved searches, pinned (cleanup item 19.3). A slide
// search's folder, a Panex search's and the lock beside them are named by
// a hash over what the search depends on, and a problem a file gave no uid
// by a hash over what the writer makes of it. Nothing ties those hashes to
// what an earlier version worked out but these values: a name that changes
// (an accessor's type, an attribute the writer adds, a value mixed in
// another order) makes Continue search from the first move and leaves the
// user's saved search, hours or days of it, where nothing finds it
// (decisions 0041, 0043, 0044).
//
// A value here changes only together with the code that still finds the
// searches saved under the old name. A failure is not a reason to put the
// new value in.
#include "../model/model_test_helpers.h"

#include "shapelogic/base/stop.h"
#include "shapelogic/io/puzzle_file.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/solve/assembler.h"
#include "shapelogic/solve/panex_search.h"
#include "shapelogic/solve/sliding_search.h"
#include "shapelogic/solve/solve_session.h"
#include "shapelogic/solve/solver_type.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace bt;

namespace {

std::string hex(std::uint64_t v) {
  char text[17];
  std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(v));
  return text;
}

// The start layouts a solve of the problem searches from, in the order it
// finds them: the assemblies of the matrix a session builds (solveSetup),
// on one thread; the first `most` of them.
std::vector<Assembly> starts(Puzzle& puzzle, unsigned int problem, std::size_t most = 16) {
  Problem& pr = puzzle.getProblem(problem);
  (void)sliding::prepareForSolving(puzzle, pr);
  const SolveSetup setup =
      solveSetup(puzzle, pr, MatrixOptions{}, SolverType::Classic, SetupFlags{});
  const std::unique_ptr<Assembler> a = makeAssembler(puzzle, pr, setup.solver);
  REQUIRE(a->createMatrix(setup.matrix, StopToken()) == AssemblerError::None);
  std::vector<Assembly> found;
  (void)a->assemble(
      [&found, most](Assembly one) {
        found.push_back(std::move(one));
        return found.size() < most;
      },
      SearchOptions{}, StopToken{});
  REQUIRE_FALSE(found.empty());
  return found;
}

// One search whose name is pinned: a fixture's first start, searched as
// `set` says.
struct NamedSearch {
  const char* what;
  const char* fixture;
  std::function<void(sliding::SlideSearch&)> set;
  const char* folder;
  unsigned int keyWidth;
  bool packed;
  bool loose;
};

}  // namespace

// The fixtures are the original's files, which hold no uid: each name here
// also holds the uid worked out for its problem (giveUids). A search is
// named by its start too: the first the assembler gives (the oracle holds
// the order of the assemblies to the original's). The locks and the Panex
// folders below hold no start.
TEST_CASE("search names: a slide search's folder is named as it ever was", "[sliding][names]") {
  const std::vector<NamedSearch> searches = {
      {"a plain board, a code for each piece in 64 bits", "sliding/slide_klotski.xmpuzzle",
       [](sliding::SlideSearch&) {},
       "slide-90ee8a96ecb15aaa", 64, false, false},
      {"the same in a key of 128 bits", "sliding/slide_klotski.xmpuzzle",
       [](sliding::SlideSearch& s) { s.minKeyBits = 128; },
       "slide-4d8cd34d5746f4d2", 128, false, false},
      {"the same in the wider keys, which keep the copies of a piece as one number: 192 bits", "sliding/slide_klotski.xmpuzzle",
       [](sliding::SlideSearch& s) { s.minKeyBits = 192; },
       "slide-e263abf058749c7c", 192, true, false},
      {"256 bits", "sliding/slide_klotski.xmpuzzle",
       [](sliding::SlideSearch& s) { s.minKeyBits = 256; },
       "slide-516c7d0b172aa1a4", 256, true, false},
      {"512 bits", "sliding/slide_klotski.xmpuzzle",
       [](sliding::SlideSearch& s) { s.minKeyBits = 512; },
       "slide-ae1665028cd226c4", 512, true, false},
      {"192 bits with a code for each copy", "sliding/slide_klotski.xmpuzzle",
       [](sliding::SlideSearch& s) {
         s.minKeyBits = 192;
         s.packing = sliding::SlidePacking::Never;
       },
       "slide-d6525ea04f73f3ba", 192, false, false},
      {"64 bits with the copies of a piece as one number", "sliding/slide_klotski.xmpuzzle",
       [](sliding::SlideSearch& s) { s.packing = sliding::SlidePacking::Always; },
       "slide-062316314630ec6c", 64, true, false},
      {"like pieces told apart", "sliding/slide_klotski.xmpuzzle",
       [](sliding::SlideSearch& s) { s.likePiecesApart = true; },
       "slide-e2cc05a54ed1bf93", 64, false, false},
      {"a grid of 2, a code for each stop", "sliding/slide_klotski.xmpuzzle",
       [](sliding::SlideSearch& s) { s.grid = 2; },
       "slide-0463e0a3e38b0adf", 64, false, false},
      {"a grid of 2, a code for every position", "sliding/slide_klotski.xmpuzzle",
       [](sliding::SlideSearch& s) {
         s.grid = 2;
         s.plainKeys = true;
       },
       "slide-7022b34f5d4496ea", 64, false, false},
      {"vertical moves", "sliding/slide_klotski.xmpuzzle",
       [](sliding::SlideSearch& s) { s.verticalMoves = true; },
       "slide-b9ae6641c7905235", 64, false, false},
      {"nested slides", "sliding/slide_klotski.xmpuzzle",
       [](sliding::SlideSearch& s) { s.nested = true; },
       "slide-30a852f4d5333ecd", 64, false, false},
      {"partially nested slides, which bring nested slides with them", "sliding/slide_klotski.xmpuzzle",
       [](sliding::SlideSearch& s) { s.partialNested = true; },
       "slide-8cd35ab8a3dcf154", 64, false, false},
      {"fifteen pieces of one cell", "sliding/slide_swapped_fifteen.xmpuzzle",
       [](sliding::SlideSearch&) {},
       "slide-06e8ace31ffcbe96", 64, false, false},
      {"a board that takes a key of 128 bits", "sliding/slide_wide_keys.xmpuzzle",
       [](sliding::SlideSearch&) {},
       "slide-1f1fd76882890f51", 128, false, false},
      {"a tray of two layers with vertical moves", "sliding/slide_two_layers_open.xmpuzzle",
       [](sliding::SlideSearch& s) { s.verticalMoves = true; },
       "slide-17debfa95238ff53", 64, false, false},
      {"a pocket with nested slides", "sliding/slide_pocket_nested.xmpuzzle",
       [](sliding::SlideSearch& s) { s.nested = true; },
       "slide-f31d0011300c3ffa", 64, false, false},
      {"a pocket with partially nested slides", "sliding/slide_pocket_partial.xmpuzzle",
       [](sliding::SlideSearch& s) {
         s.nested = true;
         s.partialNested = true;
       },
       "slide-5e20dff760e8e592", 64, false, false},
  };
  for (const NamedSearch& n : searches) {
    INFO(n.what << " (" << n.fixture << ")");
    Puzzle p = test::loadFixture(n.fixture);
    sliding::SlideSearch search;
    n.set(search);
    // the first start with a search to name (one that is the goal has none)
    sliding::SlideSearchName name;
    for (const Assembly& start : starts(p, 0)) {
      name = sliding::searchName(p, p.getProblem(0), start, search);
      if (!name.folder.empty()) {
        break;
      }
    }
    CHECK(name.folder == n.folder);
    CHECK(name.keyWidth == n.keyWidth);
    CHECK(name.packedKeys == n.packed);
    CHECK(name.looseStops == n.loose);
  }
}

// The search v1.8.1 saved (tests/data/slide_paused_by_v1.8.1) is the one a
// search of that start from the start alone is named as today: nested
// slides on the grid of 3, the stops of a grid cell as one.
TEST_CASE("search names: the search v1.8.1 saved has the name a search has today",
          "[sliding][names]") {
  Result<Puzzle> loaded =
      io::loadPuzzle(std::filesystem::path("tests/data/slide_paused_by_v1.8.1") /
                     "cup_and_blocks.xmpuzzle");
  REQUIRE(loaded.has_value());
  const Problem& pr = loaded->getProblem(0);
  REQUIRE(pr.getPending().size() == 1);
  sliding::SlideSearch search;
  search.nested = true;
  search.grid = 3;
  search.oneEnd = true;
  search.plainKeys = true;
  const sliding::SlideSearchName name =
      sliding::searchName(*loaded, pr, pr.getPending().front().assembly, search);
  CHECK(name.folder == "slide-cafc026be534476e");
  CHECK(name.keyWidth == 64);
  CHECK(name.looseStops);
  CHECK_FALSE(name.packedKeys);
  // from both ends it is another search, with the stops apart
  search.oneEnd = false;
  const sliding::SlideSearchName both =
      sliding::searchName(*loaded, pr, pr.getPending().front().assembly, search);
  CHECK(both.folder == "slide-2cb44dd64334e6a6");
  CHECK_FALSE(both.looseStops);
}

// The lock of a problem's search folders is named by the problem alone
// (problemKeyOf: the uid, the tray with its marks, the pieces).
TEST_CASE("search names: a sliding problem's lock is named as it ever was", "[sliding][names]") {
  const auto lock = [](const char* fixture) {
    const Puzzle p = test::loadFixture(fixture);
    return std::filesystem::path(sliding::solveLockPath(p, p.getProblem(0), "work"))
        .filename()
        .string();
  };
  CHECK(lock("sliding/slide_klotski.xmpuzzle") == "solve-d117744c04e2a714.lock");
  CHECK(lock("sliding/slide_two_layers_open.xmpuzzle") == "solve-1186c345ea63cf7c.lock");
  CHECK(lock("sliding/slide_pocket_nested.xmpuzzle") == "solve-74d46a528ea9cfd9.lock");
}

TEST_CASE("search names: a Panex search's folder is named as it ever was", "[stacking][names]") {
  const auto folder = [](const char* fixture) {
    const Puzzle p = test::loadFixture(fixture);
    return std::filesystem::path(panex::searchFolder(p, p.getProblem(0), "work"))
        .filename()
        .string();
  };
  CHECK(folder("stacking/stack_panex_jr.xmpuzzle") == "7b25ab34ede811c5");
  CHECK(folder("stacking/stack_panex_jr_pocket_2.xmpuzzle") == "0a5914830d1cbcf2");
  CHECK(folder("stacking/stack_panex_jr_no_pocket.xmpuzzle") == "5427f18ccbdc5906");
  CHECK(folder("stacking/stack_panex_channel_3.xmpuzzle") == "a77b908d7fb10f26");
  CHECK(folder("stacking/stack_panex_swap_4.xmpuzzle") == "d4d8aed800c8f046");
  CHECK(folder("stacking/stack_hanoi_3.xmpuzzle") == "6e562735b228330c");
  CHECK(folder("stacking/stack_distance.xmpuzzle") == "e241fd45c57df314");
  // (more rods than the search takes: no folder)
  CHECK(folder("stacking/stack_nine_rods.xmpuzzle").empty());
}

// A problem of a file without uids (every file of the original's) gets one
// worked out from its place and from what the writer makes of its
// definition (giveUids), the same each time the file is read, and that uid
// names its saved searches. Whatever changes what writeProblem emits for a
// definition changes these.
TEST_CASE("search names: the uid worked out for an original file's problem is what it was",
          "[io][file][uid][names]") {
  const auto uids = [](const char* fixture) {
    const Puzzle p = test::loadFixture(fixture);
    std::string all;
    for (unsigned int i = 0; i < p.getNumberOfProblems(); i++) {
      all += (i == 0 ? "" : " ") + hex(p.getProblem(i).getUid());
    }
    return all;
  };
  // a cube puzzle with two problems, a sliding board, a stacking puzzle
  CHECK(uids("DemoMirrorParadox.xmpuzzle") == "ddf12f2d292198af 0cbb11f5b5bf1d0c");
  CHECK(uids("PelikanBurr.xmpuzzle") == "482e8d5abe80140b");
  CHECK(uids("sliding/slide_klotski.xmpuzzle") == "333919a0100f6193");
  CHECK(uids("stacking/stack_panex_jr.xmpuzzle") == "f73662df4e3ca9c9");
}

// The list of what names a search (sliding::SlideNaming) holds nothing
// that does not: each member of a search's own changes the folder a new
// search is saved in. (The other direction is the build's: nameSpace is
// handed the options as a SlideNaming alone, and slideNaming names every
// setting.) The refusal that walks the same list is tested in
// test_sliding_session.cpp ("a paused search is not carried on as another
// search").
TEST_CASE("search names: every option of the naming list changes a search's name",
          "[sliding][names]") {
  // what the member is changed from (klotski's first start), and to
  struct Change {
    std::function<void(sliding::SlideSearch&)> from;
    std::function<void(sliding::SlideSearch&)> to;
  };
  const auto none = [](sliding::SlideSearch&) {};
  // nested slides on a grid, from the start alone: the stops of a grid
  // cell as one, or with gridStopsApart each its own
  const auto looseStops = [](sliding::SlideSearch& s) {
    s.nested = true;
    s.grid = 2;
    s.oneEnd = true;
  };
  const auto onGrid = [](sliding::SlideSearch& s) { s.grid = 2; };
  const std::map<std::string_view, Change> changes = {
      {"nested slides", {none, [](sliding::SlideSearch& s) { s.nested = true; }}},
      {"partially nested slides",
       {[](sliding::SlideSearch& s) { s.nested = true; },
        [](sliding::SlideSearch& s) { s.partialNested = true; }}},
      {"vertical moves", {none, [](sliding::SlideSearch& s) { s.verticalMoves = true; }}},
      {"optimized paths", {none, onGrid}},
      {"like pieces told apart",
       {none, [](sliding::SlideSearch& s) { s.likePiecesApart = true; }}},
      {"how the copies of a piece are kept in a key",
       {none, [](sliding::SlideSearch& s) { s.packing = sliding::SlidePacking::Always; }}},
      {"the narrowest key", {none, [](sliding::SlideSearch& s) { s.minKeyBits = 128; }}},
      {"a code for every position",
       {onGrid, [](sliding::SlideSearch& s) { s.plainKeys = true; }}},
      {"the stops of a grid cell told apart",
       {looseStops, [](sliding::SlideSearch& s) { s.gridStopsApart = true; }}},
      {"the search over text keys", {none, [](sliding::SlideSearch& s) { s.legacy = true; }}},
  };
  // Not a search's own: the orientations choose the start layouts the
  // assembler gives (solveSetup, sliding::partTransforms), and a search is
  // named by its start; test_sliding_session.cpp's "any orientation" cases
  // show the starts they add.
  const std::set<std::string_view> throughTheStarts = {"any orientation for unplaced pieces",
                                                       "mirrored orientations"};

  Puzzle p = test::loadFixture("sliding/slide_klotski.xmpuzzle");
  const Assembly start = starts(p, 0, 1).front();
  const auto nameOf = [&](const sliding::SlideSearch& search) {
    return sliding::searchName(p, p.getProblem(0), start, search).folder;
  };
  std::size_t walked = 0;
  for (const sliding::SlideNameValue& field :
       sliding::slideNameValues(sliding::SlideNaming{})) {
    INFO(field.label);
    walked++;
    if (throughTheStarts.contains(field.label)) {
      continue;
    }
    const auto change = changes.find(field.label);
    REQUIRE(change != changes.end());
    sliding::SlideSearch search;
    change->second.from(search);
    const std::string before = nameOf(search);
    // the member alone differs between the two namings
    const sliding::SlideNaming was = sliding::slideNaming(search, search.grid);
    change->second.to(search);
    const sliding::SlideNaming now = sliding::slideNaming(search, search.grid);
    const std::vector<std::string_view> differ = sliding::namesDiffer(was, now);
    REQUIRE(differ.size() == 1);
    CHECK(differ.front() == field.label);
    CHECK_FALSE(before.empty());
    CHECK(nameOf(search) != before);
  }
  CHECK(walked == changes.size() + throughTheStarts.size());

  // and what is no part of a name leaves it as it is
  sliding::SlideSearch other;
  const std::string plain = nameOf(other);
  other.highMemory = true;
  other.memoryBytes = 1000000;
  other.workDir = "elsewhere";
  other.diskBudget = 1000000;
  other.compression = 1;
  other.oneEnd = true;
  other.threads = 3;
  other.resume = false;
  CHECK(nameOf(other) == plain);
  CHECK(sliding::namesDiffer(sliding::slideNaming(sliding::SlideSearch{}, 0),
                             sliding::slideNaming(other, 0))
            .empty());
}
