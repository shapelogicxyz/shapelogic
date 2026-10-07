// SPDX-License-Identifier: GPL-3.0-or-later
// Grouping. Ported from the original's test/test_grouping.cpp; the line
// numbers its comments cite are those of the original grouping.cpp. The
// cases at the end (groups in the take-apart) are new, from the stage 4
// audit.
#include "bt/base/stop.h"
#include "bt/grid/voxel.h"
#include "bt/io/puzzle_file.h"
#include "bt/model/assembly.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"
#include "bt/solve/assembler.h"
#include "bt/solve/disassembler.h"
#include "bt/solve/grouping.h"
#include "bt/solve/solver_type.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using bt::Grouping;

TEST_CASE("grouping: a group accepts pieces up to its count", "[solve][grouping]") {
  Grouping g;

  /* piece 0 may appear twice within group 1 */
  g.addPieces(0, 1, 2);

  g.newSet();
  REQUIRE(g.addPieceToSet(0));
  REQUIRE(g.addPieceToSet(0));
}

TEST_CASE("grouping: exhausting a group's count fails the assignment", "[solve][grouping]") {
  Grouping g;

  /* only one of piece 0 is available in group 1 */
  g.addPieces(0, 1, 1);

  g.newSet();
  REQUIRE(g.addPieceToSet(0));

  /* the second one has nowhere to go: group 1 is spent and there is no
     other group to back off into */
  REQUIRE_FALSE(g.addPieceToSet(0));
}

TEST_CASE("grouping: group 0 means no group and registers nothing", "[solve][grouping]") {
  Grouping g;

  /* addPieces returns early for group == 0 (grouping.cpp:27-28), so this
     registers NO capacity at all -- not "a group numbered 0", but
     genuinely nothing. To make that observable rather than assumed,
     register piece 0 in group 0 with a generous count AND, separately, in
     group 1 with a count of exactly one. If the group-0 call had silently
     contributed real capacity, more than one placement would succeed
     below; instead exactly ONE succeeds -- drawn entirely from group 1's
     count of one -- and the second fails, which is only possible if the
     group-0 registration added nothing at all. */
  g.addPieces(0, 0, 5);
  g.addPieces(0, 1, 1);

  g.newSet();
  REQUIRE(g.addPieceToSet(0));
  REQUIRE_FALSE(g.addPieceToSet(0));
}

TEST_CASE("grouping: an unregistered piece cannot be placed", "[solve][grouping]") {
  Grouping g;

  /* nothing was ever registered, so there is no group to place into */
  g.newSet();
  REQUIRE_FALSE(g.addPieceToSet(0));
}

TEST_CASE("grouping: capacity is global, so a new set does not replenish it", "[solve][grouping]") {
  Grouping g;

  g.addPieces(0, 1, 1);

  g.newSet();
  REQUIRE(g.addPieceToSet(0));

  /* a fresh set draws on the same pool; the single piece is already spent */
  g.newSet();
  REQUIRE_FALSE(g.addPieceToSet(0));
}

TEST_CASE("grouping: reSet gives the counts back", "[solve][grouping]") {
  Grouping g;

  g.addPieces(0, 1, 1);
  g.addPieces(0, 2, 1);

  g.newSet();
  REQUIRE(g.addPieceToSet(0));
  g.newSet();
  REQUIRE(g.addPieceToSet(0));
  g.newSet();
  REQUIRE_FALSE(g.addPieceToSet(0));

  /* The original's reSet cleared the sets and the failed flag but left
     the counts used up, so the next take-apart of a disassembler found
     the groups full (stage 4 audit, finding 2). Now each take-apart
     starts with the counts registered. */
  g.reSet();
  g.newSet();
  REQUIRE(g.addPieceToSet(0));
  g.newSet();
  REQUIRE(g.addPieceToSet(0));
}

TEST_CASE("grouping: backtracking into a newly-tried group can succeed for the whole set", "[solve][grouping]") {
  Grouping g;

  /* numGroups must be >= 3 for the backtracking loop to ever try a group
     other than "give up" (grouping.cpp:107's currentGroup >= numGroups
     check needs a third group to still be available after the first
     bump). Piece 0 is available in both group 1 and group 2; piece 1 is
     only available in group 2. */
  g.addPieces(0, 1, 1);
  g.addPieces(0, 2, 1);
  g.addPieces(1, 2, 1); // numGroups becomes 3

  g.newSet();

  /* fast path: piece 0 fits directly into group 1 */
  REQUIRE(g.addPieceToSet(0));

  /* piece 1 does not fit group 1 at all, so the set as a whole must move
     to group 2 (grouping.cpp:104 currentGroup++, then the newly-tried-
     group arm at :121-144, since 2 < numGroups(3) so the give-up branch
     at :107 is not taken). Placing BOTH set members (piece 0 and piece 1)
     into group 2 succeeds (:125-131 with no failure), the for loop
     completes without ever entering the undo branch at :133-141, the do
     loop's `set` reaches sets.size() and the loop exits normally so
     execution falls through to the backtracking-succeeded `return true`
     at grouping.cpp:149 rather than the fast-path return at :84. */
  REQUIRE(g.addPieceToSet(1));
}

TEST_CASE("grouping: a failed newly-tried group retries the next one", "[solve][grouping]") {
  Grouping g;

  /* Same numGroups >= 3 shape as above, but this time piece 0 is only
     ever available in group 1, and nothing is registered for it in group
     2 — group 2's dummy piece (99) exists purely to push numGroups to 3
     without interacting with this set. */
  g.addPieces(0, 1, 1);
  g.addPieces(99, 2, 1); // numGroups becomes 3, unrelated to pieces 0/1

  g.newSet();

  /* fast path: piece 0 fits group 1 */
  REQUIRE(g.addPieceToSet(0));

  /* piece 1 was never registered anywhere, so the set must move to group
     2 (grouping.cpp:104/:107, entering the newly-tried-group arm at
     :121-144 since 2 < numGroups(3)). There, piece 0 (the first member
     checked, p==0) is not available in group 2 either, so the failure
     branch at :133-141 runs: the "already placed" undo loop at :136-137
     executes zero iterations (p2 < p is 0 < 0, false), then :139-140 back
     off by one and retry. The next group increment reaches
     currentGroup(3) >= numGroups(3), taking the give-up branch at
     :107-112, and since this is the only (bottom) set, :110's set==0
     check fires and the whole call fails. */
  REQUIRE_FALSE(g.addPieceToSet(1));
}

TEST_CASE("grouping: undoing a multi-piece placement in a retried group lets a later group succeed", "[solve][grouping]") {
  Grouping g;

  /* This is the scenario the p2/p mixup at grouping.cpp:136 used to hang
     on: a set with three pieces, driven into a newly-tried group (group
     2) where the SECOND piece checked (p==1) fails after the first
     (p==0) already placed successfully. That forces the undo loop at
     :136-137 to run with p >= 1, walking p2 from 0 up to (not including)
     p to give back exactly the one piece already placed in that group.
     numGroups must be >= 4 so that after group 2 fails outright there is
     still a group 3 left to retry into.

     Piece 0 is available in groups 1, 2 and 3.
     Piece 1 is available in groups 1 and 3, but NOT group 2.
     Piece 2 is available in group 3 only. */
  g.addPieces(0, 1, 1);
  g.addPieces(1, 1, 1);
  g.addPieces(0, 2, 1);
  g.addPieces(0, 3, 1);
  g.addPieces(1, 3, 1);
  g.addPieces(2, 3, 1); // numGroups becomes 4

  g.newSet();

  /* fast path: pieces 0 and 1 both fit directly into group 1 */
  REQUIRE(g.addPieceToSet(0));
  REQUIRE(g.addPieceToSet(1));

  /* piece 2 does not fit group 1 at all (grouping.cpp:81), so the whole
     set backs out of group 1 (:88-89) and the backtracking do-loop takes
     over. It bumps to group 2 (:104) and tries to place all three set
     members there (:125-143): piece 0 (p==0) fits and is placed, but
     piece 1 (p==1) does not exist in group 2 at all, so the failure arm
     at :133-141 runs with p==1 -- the undo loop at :136-137 must give
     back piece 0's group-2 placement, not spin forever. The do-loop then
     bumps to group 3 (:104), where all three pieces (0, 1 and 2) fit, so
     the for loop at :125-143 completes with no failure, `set` reaches
     sets.size() and the call succeeds via :149.

     Before the fix (`p++` instead of `p2++` at :136) this call hung
     forever instead of returning.

     NOTE: this case exercises the undo path and checks the right answer,
     but it CANNOT detect a reintroduction of the p++/p2++ mistake at the
     optimization levels this project actually builds with (-O2 locally,
     -O3 in CI). The undone loop body is a plain non-volatile write with
     no I/O or atomics, so C++'s forward-progress rule lets the optimizer
     assume it terminates -- the UB is exploited away rather than
     manifesting as a hang, and the test passes either way. What would
     actually catch a regression here is review of that line, or a build
     that doesn't assume loop termination (e.g. Clang's
     -fno-finite-loops), not more test engineering against -O2. */
  REQUIRE(g.addPieceToSet(2));
}

namespace {

// The stage 4 audit's puzzle: two separate 3x3x3 boxes, each a dumbbell
// (two 3x3 plates on a one-cube stem) with a ring around its stem, which
// cannot come apart; each shape has one piece in group 1 and one in group
// 2, so a dumbbell and a ring of one group may stay together. Every
// assembly's two boxes separate at once, and each box is one group's pair:
// every assembly is a solution. With `resultFirst` the result is shape 0
// and part k is shape k + 1 (as in every example), else the result is
// shape 2 and each part's shape id is its index.
bt::Puzzle twoBoxes(bool resultFirst) {
  const std::string box =
      R"(<voxel x="7" y="3" z="3" name="TwoBoxes" type="0">###_######_######_######_######_######_######_######_######_###</voxel>)";
  const std::string bell =
      R"(<voxel x="3" y="3" z="3" name="Dumbbell" type="0">#########____#____#########</voxel>)";
  const std::string ring = R"(<voxel x="3" y="3" z="1" name="Ring" type="0">####_####</voxel>)";
  const std::string groups = R"(<group group="1" count="1"/><group group="2" count="1"/>)";
  const std::string xml =
      R"(<puzzle version="2"><gridType type="0"/><colors/><shapes>)" +
      (resultFirst ? box + bell + ring : bell + ring + box) +
      R"(</shapes><problems><problem name="two boxes" state="0"><shapes>)" +
      (resultFirst ? R"(<shape id="1" count="2">)" : R"(<shape id="0" count="2">)") + groups +
      "</shape>" +
      (resultFirst ? R"(<shape id="2" count="2">)" : R"(<shape id="1" count="2">)") + groups +
      "</shape></shapes>" +
      (resultFirst ? R"(<result id="0"/>)" : R"(<result id="2"/>)") +
      "<bitmap/></problem></problems></puzzle>";
  bt::Result<bt::Puzzle> p = bt::io::parsePuzzle(xml);
  REQUIRE(p.has_value());
  return std::move(*p);
}

std::vector<bt::Assembly> assembliesOf(const bt::Puzzle& puzzle) {
  const bt::Problem& pr = puzzle.getProblem(0);
  std::unique_ptr<bt::Assembler> a = bt::makeAssembler(puzzle, pr, bt::SolverType::Classic);
  REQUIRE(a->createMatrix({}) == bt::AssemblerError::None);
  std::vector<bt::Assembly> res;
  REQUIRE(a->assemble(
              [&res](bt::Assembly x) {
                res.push_back(std::move(x));
                return true;
              },
              {}, bt::StopToken{}) == bt::RunEnd::Finished);
  return res;
}

}  // namespace

TEST_CASE("grouping: pieces in several groups stay together, whatever the shape order",
          "[solve][grouping]") {
  for (const bool resultFirst : {true, false}) {
    INFO((resultFirst ? "result shape 0" : "result shape 2"));
    const bt::Puzzle puzzle = twoBoxes(resultFirst);
    const std::vector<bt::Assembly> assemblies = assembliesOf(puzzle);
    REQUIRE(assemblies.size() == 4);

    /* The original registered the groups under the shape id and looked
       them up under the part index: with the result first nothing was
       found (0 solutions). And its reSet kept the counts used up, so one
       disassembler gave only its first assembly (1 solution). */
    bt::Disassembler d(puzzle, puzzle.getProblem(0), {});
    unsigned int solutions = 0;
    for (const bt::Assembly& a : assemblies) {
      solutions += d.disassemble(a).separation ? 1 : 0;
    }
    CHECK(solutions == 4);

    /* in the other order on the same disassembler, too */
    solutions = 0;
    for (auto it = assemblies.rbegin(); it != assemblies.rend(); ++it) {
      solutions += d.disassemble(*it).separation ? 1 : 0;
    }
    CHECK(solutions == 4);
  }
}
