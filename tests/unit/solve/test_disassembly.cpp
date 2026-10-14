// SPDX-License-Identifier: GPL-3.0-or-later
// The take-apart's parts: nodes, the position sets, the separation trees it
// builds (Cube in Cage) or that files hold (Prisgon problem 1), DisasmToMoves
// and the movement caches. Ported from the original's
// test/test_disassembly.cpp, adapted to the new API: nodes are counted
// references (NodeRef), so the original's rules for declaring them in order
// fall away, and trees are values.
#include "../model/model_test_helpers.h"

#include "shapelogic/grid/grid_type.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/solution.h"
#include "shapelogic/solve/assembler.h"
#include "shapelogic/solve/disasm_to_moves.h"
#include "shapelogic/solve/disassembler.h"
#include "shapelogic/solve/disassembler_node.h"
#include "shapelogic/solve/movement_cache.h"
#include "shapelogic/solve/node_hash.h"
#include "shapelogic/solve/solver_type.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace bt;

namespace {

// a start node with three pieces at the given offsets
NodeRef node3(int dx, int dy, int dz) {
  NodeRef n = NodeRef::make(3U);
  n->set(0, 0 + dx, 0 + dy, 0 + dz, 0);
  n->set(1, 2 + dx, 0 + dy, 0 + dz, 0);
  n->set(2, 0 + dx, 3 + dy, 1 + dz, 0);
  return n;
}

// a start node with three pieces whose arrangement depends on idx: distinct
// idx give different nodes, any shift of one idx equal ones
NodeRef nodeDistinct(int idx, int dx = 0, int dy = 0, int dz = 0) {
  NodeRef n = NodeRef::make(3U);
  n->set(0, 0 + dx, 0 + dy, 0 + dz, 0);
  n->set(1, 2 + idx + dx, 0 + dy, 0 + dz, 0);
  n->set(2, 0 + dx, 3 + dy, 1 + dz, 0);
  return n;
}

}  // namespace

TEST_CASE("disassembler node: positions and orientation are stored per piece",
          "[solve][disasm][node]") {
  NodeRef n = NodeRef::make(2U);
  n->set(0, 1, 2, 3, 0);
  n->set(1, -4, 5, -6, 7);

  REQUIRE(n->getPiecenumber() == 2);

  REQUIRE(n->getX(0) == 1);
  REQUIRE(n->getY(0) == 2);
  REQUIRE(n->getZ(0) == 3);
  REQUIRE(n->getTrans(0) == 0);

  REQUIRE(n->getX(1) == -4);
  REQUIRE(n->getY(1) == 5);
  REQUIRE(n->getZ(1) == -6);
  REQUIRE(n->getTrans(1) == 7);
}

TEST_CASE("disassembler node: equality ignores a shift applied to every piece",
          "[solve][disasm][node]") {
  // the comparison puts piece 0 at the origin: moving the whole assembly
  // does not make another node
  REQUIRE(*node3(0, 0, 0) == *node3(5, -3, 2));
}

TEST_CASE("disassembler node: equality notices a shift applied to one piece",
          "[solve][disasm][node]") {
  const NodeRef a = node3(0, 0, 0);

  NodeRef b = NodeRef::make(3U);
  b->set(0, 0, 0, 0, 0);
  b->set(1, 2, 0, 0, 0);
  b->set(2, 0, 3, 2, 0);  // z differs by one

  REQUIRE_FALSE(*a == *b);
}

TEST_CASE("disassembler node: equality notices a changed orientation", "[solve][disasm][node]") {
  const NodeRef a = node3(0, 0, 0);
  NodeRef b = node3(0, 0, 0);
  b->set(1, 2, 0, 0, 1);  // same place, another orientation

  REQUIRE_FALSE(*a == *b);
}

TEST_CASE("disassembler node: equal nodes hash equal", "[solve][disasm][node][hash]") {
  const NodeRef a = node3(0, 0, 0);
  const NodeRef b = node3(5, -3, 2);

  REQUIRE(*a == *b);
  REQUIRE(a->hash() == b->hash());
}

TEST_CASE("disassembler node: a removed piece is marked and detected", "[solve][disasm][node]") {
  NodeRef n = NodeRef::make(2U);
  n->set(0, 0, 0, 0, 0);
  n->set(1, 1, 0, 0, 0);

  REQUIRE_FALSE(n->isPieceRemoved(0));
  REQUIRE_FALSE(n->isPieceRemoved(1));
  REQUIRE_FALSE(n->isSeparation());

  n->setRemove(1, 10, 0, 0);

  REQUIRE_FALSE(n->isPieceRemoved(0));
  REQUIRE(n->isPieceRemoved(1));
  REQUIRE(n->isSeparation());
}

TEST_CASE("disassembler node: come-from constructor accumulates relative moves",
          "[solve][disasm][node]") {
  // the four-argument set() moves from where the piece was in the node this
  // one came from
  NodeRef root = NodeRef::make(2U);
  root->set(0, 0, 0, 0, 0);
  root->set(1, 5, 0, 0, 0);

  NodeRef child = NodeRef::make(2U, root, /*dir*/ 0, /*amount*/ 3, /*step*/ 1);
  child->set(0, 3, 0, 0);  // root's (0, 0, 0) + (3, 0, 0)
  child->set(1, 0, 4, 0);  // root's (5, 0, 0) + (0, 4, 0)

  REQUIRE(child->getX(0) == 3);
  REQUIRE(child->getY(0) == 0);
  REQUIRE(child->getZ(0) == 0);

  REQUIRE(child->getX(1) == 5);
  REQUIRE(child->getY(1) == 4);
  REQUIRE(child->getZ(1) == 0);

  REQUIRE(child->getComefrom() == root.get());
  REQUIRE(child->getAmount() == 3);
  REQUIRE(child->getDirection() == 0U);
  REQUIRE(child->getWaylength() == root->getWaylength() + 1);

  // the child keeps the root: letting go of this reference leaves it
  root.reset();
  REQUIRE(child->getComefrom() != nullptr);
  REQUIRE(child->getComefrom()->getX(1) == 5);
}

TEST_CASE("nodeHash: an inserted node is found by contains", "[solve][disasm][hash]") {
  NodeHash table;
  const NodeRef a = node3(1, 2, 3);
  REQUIRE(table.insert(a) == nullptr);
  REQUIRE(table.contains(a.get()));
}

TEST_CASE("nodeHash: contains is false for a node that was never inserted",
          "[solve][disasm][hash]") {
  NodeHash table;
  REQUIRE(table.insert(node3(0, 0, 0)) == nullptr);
  const NodeRef b = nodeDistinct(1);
  REQUIRE_FALSE(table.contains(b.get()));
}

TEST_CASE("nodeHash: inserting a node equal to one already present returns the existing node",
          "[solve][disasm][hash]") {
  NodeHash table;
  const NodeRef a = node3(0, 0, 0);
  const NodeRef b = node3(5, -3, 2);  // the same arrangement, shifted

  REQUIRE(*a == *b);
  REQUIRE(table.insert(a) == nullptr);

  const DisassemblerNode* found = table.insert(b);
  REQUIRE(found != nullptr);
  REQUIRE(found == a.get());  // the original node, not b
  REQUIRE(table.contains(b.get()));
}

TEST_CASE("nodeHash: the shorter way to a position is kept", "[solve][disasm][hash]") {
  NodeRef start = NodeRef::make(3U);
  start->set(0, 0, 0, 0, 0);
  start->set(1, 2, 0, 0, 0);
  start->set(2, 0, 3, 1, 0);

  // the same position, one reached in one step, the other in three
  NodeRef longWay = NodeRef::make(3U, start, 0, 1, 3);
  NodeRef shortWay = NodeRef::make(3U, start, 2, 1, 1);
  for (unsigned int i = 0; i < 3; i++) {
    longWay->set(i, 0, 0, 0);
    shortWay->set(i, 0, 0, 0);
  }

  NodeHash table;
  REQUIRE(table.insert(longWay) == nullptr);
  const DisassemblerNode* kept = table.insert(shortWay);
  REQUIRE(kept == longWay.get());
  CHECK(kept->getWaylength() == 1);
  CHECK(kept->getDirection() == 2U);
}

TEST_CASE("nodeHash: clear empties the table", "[solve][disasm][hash]") {
  NodeHash table;
  const NodeRef a = node3(0, 0, 0);
  REQUIRE(table.insert(a) == nullptr);
  REQUIRE(table.contains(a.get()));
  table.clear();
  REQUIRE_FALSE(table.contains(a.get()));
}

TEST_CASE("nodeHash: growth rehashes without losing or duplicating entries",
          "[solve][disasm][hash]") {
  const int count = 30;
  std::vector<NodeRef> nodes;
  NodeHash table;

  for (int i = 0; i < count; i++) {
    nodes.push_back(nodeDistinct(i));
    REQUIRE(table.insert(nodes.back()) == nullptr);
  }

  for (const NodeRef& n : nodes) {
    REQUIRE(table.contains(n.get()));
  }

  const NodeRef absent = nodeDistinct(count);
  REQUIRE_FALSE(table.contains(absent.get()));

  // a shifted copy of an entry made before the table grew
  const NodeRef dup = nodeDistinct(0, 100, -100, 50);
  REQUIRE(table.contains(dup.get()));
}

TEST_CASE("countingNodeHash: insert reports new vs. already-present nodes",
          "[solve][disasm][hash][counting]") {
  CountingNodeHash table;
  REQUIRE_FALSE(table.insert(node3(0, 0, 0)));  // new
  REQUIRE(table.insert(node3(5, -3, 2)));       // equal to the first
  REQUIRE_FALSE(table.insert(nodeDistinct(1)));  // new
}

TEST_CASE("countingNodeHash: clear empties the table", "[solve][disasm][hash][counting]") {
  CountingNodeHash table;
  REQUIRE_FALSE(table.insert(node3(0, 0, 0)));
  table.clear();
  // an equal arrangement is new again
  REQUIRE_FALSE(table.insert(node3(5, -3, 2)));
}

TEST_CASE("countingNodeHash: initScan/nextScan visits every inserted node exactly once",
          "[solve][disasm][hash][counting]") {
  const int count = 110;
  std::vector<NodeRef> nodes;
  CountingNodeHash table;

  for (int i = 0; i < count; i++) {
    nodes.push_back(nodeDistinct(i));
    REQUIRE_FALSE(table.insert(nodes.back()));
  }

  // a copy of an arrangement there already adds no second entry
  const NodeRef dup = nodeDistinct(0, 9, 9, 9);
  REQUIRE(table.insert(dup));

  table.initScan();

  std::vector<const DisassemblerNode*> order;
  for (const DisassemblerNode* n = table.nextScan(); n != nullptr; n = table.nextScan()) {
    order.push_back(n);
  }

  REQUIRE(order.size() == static_cast<std::size_t>(count));
  // newest first, each once
  for (int i = 0; i < count; i++) {
    CHECK(order[static_cast<std::size_t>(i)] == nodes[static_cast<std::size_t>(count - 1 - i)].get());
  }
  const std::set<const DisassemblerNode*> seen(order.begin(), order.end());
  CHECK(seen.size() == order.size());
  CHECK_FALSE(seen.contains(dup.get()));
}

namespace {

// Cube in Cage taken apart, once: the first assembly that comes apart (the
// puzzle has exactly one). As the original's case, without initHotspot, so
// the positions below are the original's numbers.
const Separation& cubeInCageSeparation() {
  static const Separation sep = [] {
    const Puzzle p = test::loadFixture("CubeInCage.xmpuzzle");
    const Problem& problem = p.getProblem(0);
    REQUIRE(Disassembler::canDisassemble(p));

    std::unique_ptr<Assembler> assm = makeAssembler(p, problem, SolverType::Classic);
    REQUIRE(assm->createMatrix({}) == AssemblerError::None);

    Disassembler disasm(p, problem, {});
    std::optional<Separation> result;
    (void)assm->assemble(
        [&](const Assembly& a) {
          TakeApartResult r = disasm.disassemble(a);
          if (r.separation) {
            result = std::move(r.separation);
            return false;
          }
          return true;
        },
        SearchOptions{}, StopToken{});
    REQUIRE(result.has_value());
    return *result;
  }();
  return sep;
}

}  // namespace

TEST_CASE("CubeInCage separation tree: getMoves() matches the sequence actually taken",
          "[solve][disasm][separation]") {
  const Separation& root = cubeInCageSeparation();

  // root -> left -> left, three levels
  REQUIRE(root.getRemoved() == nullptr);
  REQUIRE(root.getLeft() != nullptr);
  const Separation& mid = *root.getLeft();

  REQUIRE(mid.getRemoved() == nullptr);
  REQUIRE(mid.getLeft() != nullptr);
  const Separation& leaf = *mid.getLeft();

  REQUIRE(leaf.getLeft() == nullptr);
  REQUIRE(leaf.getRemoved() == nullptr);

  CHECK(root.getMoves() > 1);
  CHECK(mid.getMoves() > 1);
  CHECK(leaf.getMoves() > 1);

  const std::string expected = std::to_string(root.getMoves()) + "." +
                               std::to_string(mid.getMoves()) + "." +
                               std::to_string(leaf.getMoves());
  CHECK(root.movesText() == expected);
}

TEST_CASE("CubeInCage separation tree: the sequence API that compare() uses",
          "[solve][disasm][separation]") {
  const Separation& root = cubeInCageSeparation();
  const Separation& mid = *root.getLeft();
  const Separation& leaf = *mid.getLeft();

  REQUIRE(root.getNumSequences() == 3U);

  // a sequence's length is its states, moves + 1
  CHECK(root.getSequenceLength(0) == root.getMoves() + 1);
  CHECK(root.getSequenceLength(1) == mid.getMoves() + 1);
  CHECK(root.getSequenceLength(2) == leaf.getMoves() + 1);

  const unsigned int expectedLengths[3] = {5, 10, 4};
  for (unsigned int i = 0; i < 3; i++) {
    INFO("sequence " << i);
    CHECK(root.getSequenceLength(i) == expectedLengths[i]);
  }

  CHECK(root.compare(root) == 0);
  CHECK(mid.compare(root) == 1);
  CHECK(root.compare(mid) == -1);
}

TEST_CASE("CubeInCage separation tree: getPieceNumber()/getPieceName() account for every piece",
          "[solve][disasm][separation]") {
  const Separation& root = cubeInCageSeparation();
  const Separation& mid = *root.getLeft();
  const Separation& leaf = *mid.getLeft();

  constexpr unsigned int totalPieces = 6;

  REQUIRE(root.getPieceNumber() == totalPieces);

  auto names = [](const Separation& s) {
    std::set<unsigned int> n;
    for (unsigned int i = 0; i < s.getPieceNumber(); i++) {
      const unsigned int name = s.getPieceName(i);
      CHECK(name < totalPieces);
      CHECK(n.insert(name).second);
    }
    return n;
  };

  const std::set<unsigned int> rootNames = names(root);
  CHECK(rootNames.size() == totalPieces);

  // each level down leaves a piece behind; the names are a subset
  const std::set<unsigned int> midNames = names(mid);
  CHECK(mid.getPieceNumber() < root.getPieceNumber());
  for (const unsigned int name : midNames) {
    CHECK(rootNames.contains(name));
  }

  const std::set<unsigned int> leafNames = names(leaf);
  CHECK(leaf.getPieceNumber() < mid.getPieceNumber());
  for (const unsigned int name : leafNames) {
    CHECK(midNames.contains(name));
  }
}

namespace {

// A worklist walk of a tree: every node once, children with fewer pieces.
int walkTree(const Separation& root, bool* tookRemoved) {
  std::set<const Separation*> visited;
  std::vector<const Separation*> pending{&root};
  int nodeCount = 0;

  while (!pending.empty()) {
    const Separation* node = pending.back();
    pending.pop_back();

    REQUIRE(visited.insert(node).second);
    nodeCount++;

    unsigned int childPieces = 0;
    if (node->getLeft() != nullptr) {
      CHECK(node->getLeft()->getPieceNumber() < node->getPieceNumber());
      childPieces += node->getLeft()->getPieceNumber();
      pending.push_back(node->getLeft());
    }
    if (node->getRemoved() != nullptr) {
      CHECK(node->getRemoved()->getPieceNumber() < node->getPieceNumber());
      childPieces += node->getRemoved()->getPieceNumber();
      pending.push_back(node->getRemoved());
      *tookRemoved = true;
    }
    CHECK(childPieces <= node->getPieceNumber());
  }
  return nodeCount;
}

}  // namespace

TEST_CASE("CubeInCage separation tree: navigation terminates and never revisits a node",
          "[solve][disasm][separation]") {
  bool tookRemoved = false;
  CHECK(walkTree(cubeInCageSeparation(), &tookRemoved) == 3);
  CHECK_FALSE(tookRemoved);
}

// DisasmToMoves turns the tree into piece positions. The numbers are those
// stored in the tree's states (the pieces' real places), checked against
// the tree once and then written out. Cube in Cage removes piece 3 at the
// root, 4 in the middle and 5 at the leaf; 0, 1 and 2 stay together.
namespace {

constexpr float kPushSize = 1000.0F;
constexpr unsigned int kMaxPiece = 6;

}  // namespace

TEST_CASE("DisasmToMoves: step 0 places every piece at its assembled position, visible",
          "[solve][disasm][moves]") {
  const Separation& root = cubeInCageSeparation();
  DisasmToMoves mv(&root, static_cast<unsigned int>(kPushSize), kMaxPiece);

  mv.setStep(0);

  for (unsigned int p = 0; p < kMaxPiece; p++) {
    CHECK(mv.getX(p) == Catch::Approx(root.getState(0).getX(p)).margin(1e-4));
    CHECK(mv.getY(p) == Catch::Approx(root.getState(0).getY(p)).margin(1e-4));
    CHECK(mv.getZ(p) == Catch::Approx(root.getState(0).getZ(p)).margin(1e-4));
  }

  const float expected[kMaxPiece][3] = {
      {1, 6, 3}, {6, 3, 1}, {3, 1, 0}, {2, 2, 3}, {2, 6, 0}, {1, 5, 6},
  };

  for (unsigned int p = 0; p < kMaxPiece; p++) {
    INFO("piece " << p);
    CHECK(mv.getX(p) == Catch::Approx(expected[p][0]).margin(1e-4));
    CHECK(mv.getY(p) == Catch::Approx(expected[p][1]).margin(1e-4));
    CHECK(mv.getZ(p) == Catch::Approx(expected[p][2]).margin(1e-4));
    CHECK(mv.getA(p) == Catch::Approx(1.0F).margin(1e-4));
  }
}

TEST_CASE("DisasmToMoves: maxPiece may exceed the tree's own piece count to cover name gaps",
          "[solve][disasm][moves]") {
  const Separation& root = cubeInCageSeparation();
  DisasmToMoves mv(&root, static_cast<unsigned int>(kPushSize), 8);

  mv.setStep(0);
  for (const unsigned int p : {6U, 7U}) {
    INFO("piece " << p);
    CHECK(mv.getX(p) == Catch::Approx(0.0F).margin(1e-4));
    CHECK(mv.getY(p) == Catch::Approx(0.0F).margin(1e-4));
    CHECK(mv.getZ(p) == Catch::Approx(0.0F).margin(1e-4));
    CHECK(mv.getA(p) == Catch::Approx(0.0F).margin(1e-4));
    CHECK_FALSE(mv.moving(p));
  }
}

TEST_CASE("DisasmToMoves: a fractional step interpolates the two keyframes",
          "[solve][disasm][moves]") {
  const Separation& root = cubeInCageSeparation();
  DisasmToMoves mv(&root, static_cast<unsigned int>(kPushSize), kMaxPiece);

  SECTION("within one node's moves") {
    // piece 2 goes from (3, 1, 0) to (2, 1, 0)
    mv.setStep(0.5F);
    CHECK(mv.getX(2) == Catch::Approx(2.5F).margin(1e-4));
    CHECK(mv.getY(2) == Catch::Approx(1.0F).margin(1e-4));
    CHECK(mv.getZ(2) == Catch::Approx(0.0F).margin(1e-4));
    CHECK(mv.getA(2) == Catch::Approx(1.0F).margin(1e-4));
  }

  SECTION("across the step where a piece is taken off and fades") {
    // piece 3 at (3, 2, 3) in state 3; at step 4 pushed out along y to
    // (3, 1002, 3)
    mv.setStep(3.5F);
    CHECK(mv.getX(3) == Catch::Approx(3.0F).margin(1e-4));
    CHECK(mv.getY(3) == Catch::Approx(502.0F).margin(1e-4));
    CHECK(mv.getZ(3) == Catch::Approx(3.0F).margin(1e-4));
    CHECK(mv.getA(3) == Catch::Approx(0.5F).margin(1e-4));
    CHECK(mv.moving(3));
  }
}

TEST_CASE("DisasmToMoves: moving() is true exactly for the pieces that move in the next step",
          "[solve][disasm][moves]") {
  const Separation& root = cubeInCageSeparation();
  DisasmToMoves mv(&root, static_cast<unsigned int>(kPushSize), kMaxPiece);

  const unsigned int total = root.sumMoves();
  REQUIRE(total == 16U);

  for (unsigned int k = 0; k < total; k++) {
    mv.setStep(static_cast<float>(k));
    bool reportedMoving[kMaxPiece];
    float x0[kMaxPiece];
    float y0[kMaxPiece];
    float z0[kMaxPiece];
    for (unsigned int p = 0; p < kMaxPiece; p++) {
      reportedMoving[p] = mv.moving(p);
      x0[p] = mv.getX(p);
      y0[p] = mv.getY(p);
      z0[p] = mv.getZ(p);
    }

    mv.setStep(static_cast<float>(k + 1));
    for (unsigned int p = 0; p < kMaxPiece; p++) {
      const bool changed = std::fabs(mv.getX(p) - x0[p]) > 1e-4F ||
                           std::fabs(mv.getY(p) - y0[p]) > 1e-4F ||
                           std::fabs(mv.getZ(p) - z0[p]) > 1e-4F;
      INFO("step " << k << ", piece " << p);
      CHECK(reportedMoving[p] == changed);
    }
  }
}

TEST_CASE("DisasmToMoves: setStep is an exact affine blend of its two keyframes",
          "[solve][disasm][moves]") {
  const Separation& root = cubeInCageSeparation();
  DisasmToMoves mv(&root, static_cast<unsigned int>(kPushSize), kMaxPiece);

  const unsigned int total = root.sumMoves();

  using PieceKeyframes = std::array<std::array<float, 4>, kMaxPiece>;
  std::vector<PieceKeyframes> keyframes(total + 1);
  for (unsigned int k = 0; k <= total; k++) {
    mv.setStep(static_cast<float>(k));
    for (unsigned int p = 0; p < kMaxPiece; p++) {
      keyframes[k][p] = {mv.getX(p), mv.getY(p), mv.getZ(p), mv.getA(p)};
    }
  }

  // every keyframe's alpha is 0 or 1
  for (unsigned int k = 0; k <= total; k++) {
    for (unsigned int p = 0; p < kMaxPiece; p++) {
      const float a = keyframes[k][p][3];
      CHECK((a == Catch::Approx(0.0F).margin(1e-4) || a == Catch::Approx(1.0F).margin(1e-4)));
    }
  }

  for (unsigned int k = 0; k < total; k++) {
    const PieceKeyframes& a = keyframes[k];
    const PieceKeyframes& b = keyframes[k + 1];
    for (const float frac : {0.25F, 0.5F, 0.75F}) {
      mv.setStep(static_cast<float>(k) + frac);
      for (unsigned int p = 0; p < kMaxPiece; p++) {
        INFO("step " << k << " + " << frac << ", piece " << p);
        CHECK(mv.getX(p) == Catch::Approx((1 - frac) * a[p][0] + frac * b[p][0]).margin(1e-3));
        CHECK(mv.getY(p) == Catch::Approx((1 - frac) * a[p][1] + frac * b[p][1]).margin(1e-3));
        CHECK(mv.getZ(p) == Catch::Approx((1 - frac) * a[p][2] + frac * b[p][2]).margin(1e-3));
        CHECK(mv.getA(p) == Catch::Approx((1 - frac) * a[p][3] + frac * b[p][3]).margin(1e-4));
      }
    }
  }
}

TEST_CASE("DisasmToMoves: piece 2 really goes back and forth", "[solve][disasm][moves]") {
  const Separation& root = cubeInCageSeparation();
  DisasmToMoves mv(&root, static_cast<unsigned int>(kPushSize), kMaxPiece);

  mv.setStep(0);
  CHECK(mv.getX(2) == Catch::Approx(3.0F).margin(1e-4));
  mv.setStep(1);
  CHECK(mv.getX(2) == Catch::Approx(2.0F).margin(1e-4));
  mv.setStep(5);
  CHECK(mv.getX(2) == Catch::Approx(3.0F).margin(1e-4));
  mv.setStep(14);
  CHECK(mv.getX(2) == Catch::Approx(2.0F).margin(1e-4));
}

TEST_CASE("DisasmToMoves: the final step matches the fully separated state",
          "[solve][disasm][moves]") {
  const Separation& root = cubeInCageSeparation();
  const Separation& mid = *root.getLeft();
  const Separation& leaf = *mid.getLeft();
  DisasmToMoves mv(&root, static_cast<unsigned int>(kPushSize), kMaxPiece);

  const unsigned int total = root.sumMoves();
  REQUIRE(total == 16U);

  mv.setStep(static_cast<float>(total));

  const State& leafFinal = leaf.getState(leaf.getMoves());
  for (unsigned int p = 0; p < 3; p++) {
    INFO("piece " << p);
    REQUIRE(leaf.getPieceName(p) == p);
    CHECK(mv.getX(p) == Catch::Approx(leafFinal.getX(p)).margin(1e-4));
    CHECK(mv.getY(p) == Catch::Approx(leafFinal.getY(p)).margin(1e-4));
    CHECK(mv.getZ(p) == Catch::Approx(leafFinal.getZ(p)).margin(1e-4));
  }

  const float expected[kMaxPiece][3] = {
      {1, 6, 3}, {6, 2, 1}, {2, 1, 0}, {3, 1002, 3}, {-999, 7, -2}, {2, 6, -994},
  };

  for (const float extra : {0.0F, 4.0F}) {
    mv.setStep(static_cast<float>(total) + extra);
    for (unsigned int p = 0; p < kMaxPiece; p++) {
      INFO("piece " << p << ", " << extra << " steps after the end");
      CHECK(mv.getX(p) == Catch::Approx(expected[p][0]).margin(1e-4));
      CHECK(mv.getY(p) == Catch::Approx(expected[p][1]).margin(1e-4));
      CHECK(mv.getZ(p) == Catch::Approx(expected[p][2]).margin(1e-4));
      CHECK(mv.getA(p) == Catch::Approx(0.0F).margin(1e-4));
      CHECK_FALSE(mv.moving(p));
    }
  }
}

TEST_CASE("DisasmToMoves: the size sets how far a piece taken off is pushed",
          "[solve][disasm][moves]") {
  const Separation& root = cubeInCageSeparation();

  DisasmToMoves small(&root, 1000, kMaxPiece);
  DisasmToMoves big(&root, 3000, kMaxPiece);

  const auto total = static_cast<float>(root.sumMoves());
  small.setStep(total);
  big.setStep(total);

  CHECK(big.getY(3) - small.getY(3) == Catch::Approx(2000.0F).margin(1e-3));
  CHECK(big.getX(3) == Catch::Approx(small.getX(3)).margin(1e-4));
  CHECK(big.getZ(3) == Catch::Approx(small.getZ(3)).margin(1e-4));

  CHECK(big.getX(4) - small.getX(4) == Catch::Approx(-2000.0F).margin(1e-3));
  CHECK(big.getY(4) == Catch::Approx(small.getY(4)).margin(1e-4));
  CHECK(big.getZ(4) == Catch::Approx(small.getZ(4)).margin(1e-4));

  CHECK(big.getZ(5) - small.getZ(5) == Catch::Approx(-2000.0F).margin(1e-3));
  CHECK(big.getX(5) == Catch::Approx(small.getX(5)).margin(1e-4));
  CHECK(big.getY(5) == Catch::Approx(small.getY(5)).margin(1e-4));

  for (const unsigned int p : {0U, 1U, 2U}) {
    INFO("piece " << p);
    CHECK(big.getX(p) == Catch::Approx(small.getX(p)).margin(1e-4));
    CHECK(big.getY(p) == Catch::Approx(small.getY(p)).margin(1e-4));
    CHECK(big.getZ(p) == Catch::Approx(small.getZ(p)).margin(1e-4));
  }
}

namespace {

// Cube in Cage and Prisgon kept loaded: a movement cache reads the puzzle.
const Puzzle& cubeInCage() {
  static const Puzzle p = test::loadFixture("CubeInCage.xmpuzzle");
  return p;
}

const Puzzle& prisgon() {
  static const Puzzle p = test::loadFixture("Prisgon.xmpuzzle");
  return p;
}

// Prisgon problem 1's saved tree branches: no solve needed.
const Separation& prisgonBranchingTree() {
  const Problem& problem = prisgon().getProblem(1);
  REQUIRE(problem.getNumberOfSavedSolutions() > 0);
  const Separation* tree = problem.getSavedSolution(0).getDisassembly();
  REQUIRE(tree != nullptr);
  return *tree;
}

// A query the cache can answer: two pieces of a saved solution (they do not
// overlap there; the prism cache checks that).
struct MoQuery {
  int dx, dy, dz;
  unsigned char t1, t2;
  unsigned int p1, p2;
};

MoQuery queryFromSolution(const Problem& problem, unsigned int p1, unsigned int p2) {
  REQUIRE(problem.getNumberOfSavedSolutions() > 0);
  const Assembly& a = problem.getSavedSolution(0).getAssembly();
  REQUIRE(a.isPlaced(p1));
  REQUIRE(a.isPlaced(p2));
  return MoQuery{a.getX(p2) - a.getX(p1),
                 a.getY(p2) - a.getY(p1),
                 a.getZ(p2) - a.getZ(p1),
                 a.getTransformation(p1),
                 a.getTransformation(p2),
                 p1,
                 p2};
}

}  // namespace

TEST_CASE("movement cache: the cube grid's, a repeated query gives the same value",
          "[solve][disasm][movementcache]") {
  const Puzzle& p = cubeInCage();
  const Problem& problem = p.getProblem(0);
  REQUIRE(p.getGridType().getType() == GridKind::Bricks);

  std::unique_ptr<MovementCache> cache = makeMovementCache(p, problem);
  REQUIRE(cache != nullptr);
  REQUIRE(dynamic_cast<MovementCacheBricks*>(cache.get()) != nullptr);
  REQUIRE(cache->numDirections() == 3);

  int dx = 0;
  int dy = 0;
  int dz = 0;
  cache->getDirection(0, &dx, &dy, &dz);
  CHECK((dx == 1 && dy == 0 && dz == 0));
  cache->getDirection(1, &dx, &dy, &dz);
  CHECK((dx == 0 && dy == 1 && dz == 0));
  cache->getDirection(2, &dx, &dy, &dz);
  CHECK((dx == 0 && dy == 0 && dz == 1));

  // piece 1 against piece 0 of the saved solution: free along y (32000),
  // blocked at once along x and z
  const MoQuery q = queryFromSolution(problem, 0, 1);

  unsigned int first[3];
  cache->getMoValue(q.dx, q.dy, q.dz, q.t1, q.t2, q.p1, q.p2, first);
  CHECK(first[0] == 0U);
  CHECK(first[1] == 32000U);
  CHECK(first[2] == 0U);

  unsigned int second[3];
  cache->getMoValue(q.dx, q.dy, q.dz, q.t1, q.t2, q.p1, q.p2, second);
  CHECK(second[0] == first[0]);
  CHECK(second[1] == first[1]);
  CHECK(second[2] == first[2]);
}

TEST_CASE("movement cache: cube grid queries do not depend on the order they are asked in",
          "[solve][disasm][movementcache]") {
  const Puzzle& p = cubeInCage();
  const Problem& problem = p.getProblem(0);

  struct Expected {
    unsigned int p1, p2;
    unsigned int move[3];
  };
  const Expected cases[] = {
      {0, 1, {0, 32000, 0}},
      {2, 3, {32000, 0, 1}},
      {4, 5, {0, 0, 0}},
  };

  std::unique_ptr<MovementCache> forwardCache = makeMovementCache(p, problem);
  unsigned int forward[3][3];
  for (unsigned int k = 0; k < 3; k++) {
    const MoQuery q = queryFromSolution(problem, cases[k].p1, cases[k].p2);
    forwardCache->getMoValue(q.dx, q.dy, q.dz, q.t1, q.t2, q.p1, q.p2, forward[k]);
  }

  std::unique_ptr<MovementCache> backwardCache = makeMovementCache(p, problem);
  unsigned int backward[3][3];
  for (unsigned int k = 3; k-- > 0;) {
    const MoQuery q = queryFromSolution(problem, cases[k].p1, cases[k].p2);
    backwardCache->getMoValue(q.dx, q.dy, q.dz, q.t1, q.t2, q.p1, q.p2, backward[k]);
  }

  for (unsigned int k = 0; k < 3; k++) {
    INFO("pair (" << cases[k].p1 << "," << cases[k].p2 << ")");
    for (unsigned int d = 0; d < 3; d++) {
      CHECK(forward[k][d] == cases[k].move[d]);
      CHECK(backward[k][d] == cases[k].move[d]);
    }
  }
}

TEST_CASE("movement cache: the prism grid's, a repeated query gives the same value",
          "[solve][disasm][movementcache]") {
  const Puzzle& p = prisgon();
  const Problem& problem = p.getProblem(0);
  REQUIRE(p.getGridType().getType() == GridKind::TriangularPrism);

  std::unique_ptr<MovementCache> cache = makeMovementCache(p, problem);
  REQUIRE(cache != nullptr);
  REQUIRE(dynamic_cast<MovementCachePrisms*>(cache.get()) != nullptr);
  REQUIRE(cache->numDirections() == 4);

  const MoQuery q = queryFromSolution(problem, 0, 6);

  unsigned int first[4];
  cache->getMoValue(q.dx, q.dy, q.dz, q.t1, q.t2, q.p1, q.p2, first);
  CHECK(first[0] == 1U);
  CHECK(first[1] == 1U);
  CHECK(first[2] == 2U);
  CHECK(first[3] == 0U);

  unsigned int second[4];
  cache->getMoValue(q.dx, q.dy, q.dz, q.t1, q.t2, q.p1, q.p2, second);
  for (unsigned int d = 0; d < 4; d++) {
    CHECK(second[d] == first[d]);
  }
}

TEST_CASE("movement cache: prism grid queries do not depend on the order they are asked in",
          "[solve][disasm][movementcache]") {
  const Puzzle& p = prisgon();
  const Problem& problem = p.getProblem(0);

  struct Expected {
    unsigned int p1, p2;
    unsigned int move[4];
  };
  const Expected cases[] = {
      {0, 6, {1, 1, 2, 0}},
      {0, 7, {1, 1, 1, 0}},
      {5, 7, {0, 32000, 1, 1}},
  };

  std::unique_ptr<MovementCache> forwardCache = makeMovementCache(p, problem);
  unsigned int forward[3][4];
  for (unsigned int k = 0; k < 3; k++) {
    const MoQuery q = queryFromSolution(problem, cases[k].p1, cases[k].p2);
    forwardCache->getMoValue(q.dx, q.dy, q.dz, q.t1, q.t2, q.p1, q.p2, forward[k]);
  }

  std::unique_ptr<MovementCache> backwardCache = makeMovementCache(p, problem);
  unsigned int backward[3][4];
  for (unsigned int k = 3; k-- > 0;) {
    const MoQuery q = queryFromSolution(problem, cases[k].p1, cases[k].p2);
    backwardCache->getMoValue(q.dx, q.dy, q.dz, q.t1, q.t2, q.p1, q.p2, backward[k]);
  }

  for (unsigned int k = 0; k < 3; k++) {
    INFO("pair (" << cases[k].p1 << "," << cases[k].p2 << ")");
    for (unsigned int d = 0; d < 4; d++) {
      CHECK(forward[k][d] == cases[k].move[d]);
      CHECK(backward[k][d] == cases[k].move[d]);
    }
  }
}

TEST_CASE("movement cache: grids without a take-apart have none",
          "[solve][disasm][movementcache]") {
  for (const GridKind t : {GridKind::Spheres, GridKind::Rhombic, GridKind::TetraOcta}) {
    INFO("grid " << toString(t));
    Puzzle p{GridType{t}};
    const unsigned int prob = p.addProblem();
    CHECK(makeMovementCache(p, p.getProblem(prob)) == nullptr);
    CHECK_FALSE(Disassembler::canDisassemble(p));
  }
}

TEST_CASE("Prisgon problem 1 branching separation tree: every piece accounted for, at a branch",
          "[solve][disasm][separation]") {
  const Separation& root = prisgonBranchingTree();
  const unsigned int totalPieces = root.getPieceNumber();
  REQUIRE(totalPieces == 9U);

  auto names = [totalPieces](const Separation& s) {
    std::set<unsigned int> n;
    for (unsigned int i = 0; i < s.getPieceNumber(); i++) {
      const unsigned int name = s.getPieceName(i);
      CHECK(name < totalPieces);
      CHECK(n.insert(name).second);
    }
    return n;
  };

  CHECK(names(root).size() == totalPieces);

  // root --left--> depth1 --left--> branch, which has both children
  REQUIRE(root.getLeft() != nullptr);
  const Separation& depth1 = *root.getLeft();
  REQUIRE(depth1.getLeft() != nullptr);
  const Separation& branch = *depth1.getLeft();
  REQUIRE(branch.getLeft() != nullptr);
  REQUIRE(branch.getRemoved() != nullptr);

  const std::set<unsigned int> branchNames = names(branch);
  CHECK(branch.getPieceNumber() < depth1.getPieceNumber());

  for (const Separation* child : {branch.getLeft(), branch.getRemoved()}) {
    const std::set<unsigned int> childNames = names(*child);
    CHECK(child->getPieceNumber() < branch.getPieceNumber());
    for (const unsigned int name : childNames) {
      CHECK(branchNames.contains(name));
    }
  }
}

TEST_CASE("Prisgon problem 1 branching separation tree: the walk takes both children",
          "[solve][disasm][separation]") {
  bool tookRemoved = false;
  CHECK(walkTree(prisgonBranchingTree(), &tookRemoved) == 8);
  CHECK(tookRemoved);
}

// A tree built by hand: no example puzzle has a removed branch of more than
// one move.
namespace {

std::unique_ptr<Separation> makeSeparation(std::unique_ptr<Separation> removed,
                                           std::unique_ptr<Separation> left,
                                           const std::vector<unsigned int>& pieces,
                                           unsigned int moves) {
  auto node = std::make_unique<Separation>(ClonePtr<Separation>(std::move(removed)),
                                           ClonePtr<Separation>(std::move(left)), pieces);
  // moves + 1 states
  for (unsigned int i = 0; i <= moves; i++) {
    node->addstate(State(static_cast<unsigned int>(pieces.size())));
  }
  return node;
}

}  // namespace

// The fork notation: one segment per removal of a single piece. A node that
// splits the puzzle into two groups has none of its own; its moves go to
// the first removal of the removed branch, which comes before the left one
// (the order the animation plays them).
TEST_CASE("separation tree: movesText carries a split's moves into the removed child",
          "[solve][disasm][separation]") {
  SECTION("both children report, the removed child's numbers first") {
    auto left = makeSeparation(nullptr, nullptr, {1}, 2);
    auto removed = makeSeparation(nullptr, nullptr, {2}, 3);
    auto root = makeSeparation(std::move(removed), std::move(left), {0, 1, 2}, 2);

    REQUIRE(root->getLeft() != nullptr);
    REQUIRE(root->getRemoved() != nullptr);
    REQUIRE(root->getMoves() == 2U);
    REQUIRE(root->getLeft()->getMoves() == 2U);
    REQUIRE(root->getRemoved()->getMoves() == 3U);

    CHECK(root->movesText() == "5.2");
    CHECK(root->sumMoves() == 7U);
    CHECK(root->getNumSequences() == 3U);
  }

  SECTION("a left child of one move gets its own segment") {
    auto left = makeSeparation(nullptr, nullptr, {1}, 1);
    auto removed = makeSeparation(nullptr, nullptr, {2}, 3);
    auto root = makeSeparation(std::move(removed), std::move(left), {0, 1, 2}, 2);

    CHECK(root->movesText() == "5.1");
  }

  SECTION("a removed child of one move, as in the example puzzles") {
    auto left = makeSeparation(nullptr, nullptr, {1}, 2);
    auto removed = makeSeparation(nullptr, nullptr, {2}, 1);
    auto root = makeSeparation(std::move(removed), std::move(left), {0, 1, 2}, 2);

    CHECK(root->movesText() == "3.2");
  }
}

// movesText() writes into the original's 256-byte buffer and stops before
// its end.
TEST_CASE("separation tree: movesText stops at its internal buffer instead of overrunning it",
          "[solve][disasm][separation]") {
  auto tree = makeSeparation(nullptr, nullptr, {199}, 2);
  for (unsigned int i = 199; i-- > 0;) {
    tree = makeSeparation(nullptr, std::move(tree), {i}, 2);
  }

  const std::string got = tree->movesText();

  CHECK(got.size() == 255);
  CHECK(got.find_first_not_of("0123456789.") == std::string::npos);
  CHECK(tree->sumMoves() == 400U);
}

// The original's solve thread (the GUI's path) resets the shapes' hotspots
// after the matrix is made; burrTxt and burrTxt2 reset them before. An
// assembly's positions are those of the hotspots the matrix was made with,
// and the take-apart reads the shapes as they are: with a hotspot moved in
// between, the pieces are taken apart where they were not placed. Cube in
// Cage with one piece's hotspot elsewhere loses its solution that way (the
// original's solve thread does too). The solve session (stage 5) must reset
// the hotspots before the matrix, or not at all.
TEST_CASE("take-apart: hotspots reset before the matrix, not after it", "[solve][disasm]") {
  auto solutions = [](bool resetFirst) {
    Puzzle p = test::loadFixture("CubeInCage.xmpuzzle");
    p.getShape(2).setHotspot(1, 1, 0);
    if (resetFirst) {
      for (unsigned int i = 0; i < p.getNumberOfShapes(); i++) {
        p.getShape(i).initHotspot();
      }
    }
    const Problem& problem = p.getProblem(0);
    std::unique_ptr<Assembler> assm = makeAssembler(p, problem, SolverType::Classic);
    REQUIRE(assm->createMatrix({}) == AssemblerError::None);
    for (unsigned int i = 0; i < p.getNumberOfShapes(); i++) {
      p.getShape(i).initHotspot();
    }
    Disassembler d(p, problem, {});
    unsigned int found = 0;
    REQUIRE(assm->assemble(
                [&](const Assembly& a) {
                  found += d.disassemble(a).separation ? 1 : 0;
                  return true;
                },
                SearchOptions{}, StopToken{}) == RunEnd::Finished);
    return found;
  };
  CHECK(solutions(true) == 1);
  CHECK(solutions(false) == 0);
}
