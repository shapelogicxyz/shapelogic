// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/gui/placementbrowser.cpp, mainWindow_c::cb_BtnAssemblerStep
// (src/gui/mainwindow.cpp) and src/gui/movementbrowser.cpp (addNode,
// cb_NodeAnalyze, cb_AddMovement, selectSpecificMovement, cb_Prune, the
// direction labels of AddMovementDialog).
#include "bt/app/explorers.h"

#include "bt/base/assert.h"
#include "bt/base/int_cast.h"
#include "bt/base/stop.h"
#include "bt/grid/grid_type.h"
#include "bt/grid/voxel.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"
#include "bt/model/solution.h"
#include "bt/solve/disassembler.h"
#include "bt/solve/disassembler_node.h"
#include "bt/solve/movement_analysator.h"
#include "bt/solve/movement_cache.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <utility>

namespace bt::app {

namespace {

// A copy of the shapes, the colours and one problem's definition, with
// the hotspots where a solve puts them: what an explorer works on, so it
// is independent of later edits.
std::unique_ptr<Puzzle> explorerCopy(const Puzzle& puzzle, unsigned int problemIndex) {
  auto lite = std::make_unique<Puzzle>(puzzle.getGridType());
  for (unsigned int i = 0; i < puzzle.getNumberOfShapes(); i++) {
    std::unique_ptr<Voxel> v = puzzle.getShape(i).clone();
    if (!v->hasInitialHotspot()) {
      v->initHotspot();
    }
    (void)lite->addShape(std::move(v));
  }
  for (unsigned int c = 0; c < puzzle.colorNumber(); c++) {
    const Rgb rgb = puzzle.getColor(c);
    (void)lite->addColor(rgb.r, rgb.g, rgb.b);
  }
  (void)lite->addProblem(puzzle.getProblem(problemIndex));
  return lite;
}

}  // namespace

// ---- the assembly explorer -----------------------------------------------------------

AssemblyExplorer::AssemblyExplorer(AssemblyExplorer&&) noexcept = default;
AssemblyExplorer& AssemblyExplorer::operator=(AssemblyExplorer&&) noexcept = default;
AssemblyExplorer::~AssemblyExplorer() = default;

Result<AssemblyExplorer> AssemblyExplorer::make(const Puzzle& puzzle, unsigned int problemIndex,
                                                SolverType solver, const MatrixOptions& matrix,
                                                bool reduce) {
  const GridType& grid = puzzle.getGridType();
  if (grid.getType() == GridKind::Sliding || grid.getType() == GridKind::Stacking) {
    return fail(Errc::Unsupported, "sliding and stacking puzzles cannot be solved yet");
  }
  if ((grid.getCapabilities() & kCapAssemble) == 0) {
    return fail(Errc::Unsupported, "this space grid doesn't have an assembler");
  }
  if (problemIndex >= puzzle.getNumberOfProblems()) {
    return fail(Errc::NotFound, "there is no such problem");
  }
  const Problem& pr = puzzle.getProblem(problemIndex);
  if (!pr.resultValid()) {
    return fail(Errc::InvalidArgument, "a result shape must be defined");
  }
  if (pr.getNumberOfPieces() == 0) {
    return fail(Errc::InvalidArgument, "the problem has no pieces");
  }

  AssemblyExplorer e;
  e.puzzle_ = explorerCopy(puzzle, problemIndex);
  e.assembler_ = makeAssembler(*e.puzzle_, e.puzzle_->getProblem(0), solver);
  const AssemblerError err = e.assembler_->createMatrix(matrix);
  if (err != AssemblerError::None) {
    return fail(Errc::InvalidArgument, std::string(errorMessage(err)));
  }
  if (reduce) {
    (void)e.assembler_->reduce(StopToken());
  }
  return e;
}

bool AssemblyExplorer::placementsSupported() const {
  return assembler_->piecePlacementSupported();
}

unsigned int AssemblyExplorer::pieceCount() const {
  return puzzle_->getProblem(0).getNumberOfPieces();
}

std::vector<PiecePlacement> AssemblyExplorer::placements(unsigned int piece) const {
  std::vector<PiecePlacement> out;
  if (!assembler_->piecePlacementSupported() || piece >= pieceCount()) {
    return out;
  }
  const unsigned int count = assembler_->piecePlacementCount(piece);
  unsigned int node = 0;
  for (unsigned int i = 0; i < count; i++) {
    PiecePlacement p;
    node = assembler_->piecePlacement(node, i == 0 ? 0 : 1, piece, &p.transformation, &p.x, &p.y,
                                      &p.z);
    out.push_back(p);
  }
  return out;
}

void AssemblyExplorer::step(unsigned long steps) { assembler_->debugStep(steps); }

Assembly AssemblyExplorer::currentAssembly() const { return assembler_->currentAssembly(); }

float AssemblyExplorer::finished() const { return assembler_->finished(); }

// ---- the movement explorer -----------------------------------------------------------

struct MovementExplorer::Impl {
  std::unique_ptr<Puzzle> puzzle;
  std::unique_ptr<MovementAnalysator> analysator;
  std::unique_ptr<MovementCache> cache;
  // a deque: references to nodes stay valid as nodes are added
  std::deque<Node> nodes;
  std::vector<NodeRef> refs;  // the take-apart position of each node
  std::vector<bool> alive;

  [[nodiscard]] const Problem& problem() const { return puzzle->getProblem(0); }

  // The label of a move from `from` to `to` (addNode): the direction of
  // the first piece that moved, then the pieces moved.
  [[nodiscard]] std::string label(const Node& from, const DisassemblerNode& f,
                                  const DisassemblerNode& to) const {
    std::string t;
    bool firstPiece = true;
    for (unsigned int p = 0; p < from.pieces.size(); p++) {
      if (t.empty()) {
        if (f.getX(p) > to.getX(p)) t += "-x";
        if (f.getX(p) < to.getX(p)) t += "x";
        if (f.getY(p) > to.getY(p)) t += "-y";
        if (f.getY(p) < to.getY(p)) t += "y";
        if (f.getZ(p) > to.getZ(p)) t += "-z";
        if (f.getZ(p) < to.getZ(p)) t += "z";
      }
      if (f.getX(p) != to.getX(p) || f.getY(p) != to.getY(p) || f.getZ(p) != to.getZ(p)) {
        const unsigned int piece = from.pieces[p];
        const unsigned int part = problem().getPartIdToPieceId(piece);
        t += firstPiece ? ": S" : ", S";
        firstPiece = false;
        t += std::to_string(problem().getShapeIdOfPart(part) + 1);
        const unsigned int sub = problem().getPartIndexToPieceId(piece);
        if (sub != 0) {
          t += "." + std::to_string(sub + 1);
        }
      }
    }
    return t;
  }

  // Adds `mv` as a child of node `at` (addNode); the child already there
  // when one is the same position; none for a removal.
  std::optional<unsigned int> addNode(unsigned int at, NodeRef mv) {
    for (const unsigned int c : nodes[at].children) {
      if (*refs[c] == *mv) {
        return c;
      }
    }
    const Node& from = nodes[at];
    // removals are not added
    for (unsigned int i = 0; i < from.pieces.size(); i++) {
      if (std::abs(mv->getX(i)) > 10000 || std::abs(mv->getY(i)) > 10000 ||
          std::abs(mv->getZ(i)) > 10000) {
        return std::nullopt;
      }
    }
    Node n;
    n.id = static_cast<unsigned int>(nodes.size());
    n.parent = at;
    n.label = label(from, *refs[at], *mv);
    n.pieces = from.pieces;
    for (unsigned int i = 0; i < n.pieces.size(); i++) {
      n.x.push_back(mv->getX(i));
      n.y.push_back(mv->getY(i));
      n.z.push_back(mv->getZ(i));
    }
    nodes[at].children.push_back(n.id);
    nodes.push_back(std::move(n));
    refs.push_back(std::move(mv));
    alive.push_back(true);
    return nodes.back().id;
  }
};

MovementExplorer::MovementExplorer(MovementExplorer&&) noexcept = default;
MovementExplorer& MovementExplorer::operator=(MovementExplorer&&) noexcept = default;
MovementExplorer::~MovementExplorer() = default;

Result<MovementExplorer> MovementExplorer::make(const Puzzle& puzzle, unsigned int problemIndex,
                                                unsigned int solution, bool rotations) {
  if (!Disassembler::canDisassemble(puzzle)) {
    return fail(Errc::Unsupported, "this space grid doesn't have a disassembler");
  }
  if (problemIndex >= puzzle.getNumberOfProblems()) {
    return fail(Errc::NotFound, "there is no such problem");
  }
  const Problem& source = puzzle.getProblem(problemIndex);
  if (solution >= source.getNumberOfSavedSolutions()) {
    return fail(Errc::NotFound, "there is no such solution");
  }

  MovementExplorer e;
  e.impl_ = std::make_unique<Impl>();
  Impl& m = *e.impl_;
  m.puzzle = explorerCopy(puzzle, problemIndex);
  m.analysator = std::make_unique<MovementAnalysator>(*m.puzzle, m.problem(), rotations,
                                                      SolverType::Classic,
                                                      MovementAnalysator::Options{});
  m.cache = makeMovementCache(*m.puzzle, m.problem());

  // the root: the assembly, every placed piece where it is
  const Assembly& assembly = source.getSavedSolution(solution).getAssembly();
  NodeRef root = NodeRef::make(assembly);
  Node n;
  for (unsigned int j = 0; j < assembly.placementCount(); j++) {
    if (assembly.isPlaced(j)) {
      n.pieces.push_back(j);
    }
  }
  for (unsigned int i = 0; i < n.pieces.size(); i++) {
    n.x.push_back(root->getX(i));
    n.y.push_back(root->getY(i));
    n.z.push_back(root->getZ(i));
  }
  m.nodes.push_back(std::move(n));
  m.refs.push_back(std::move(root));
  m.alive.push_back(true);
  return e;
}

const MovementExplorer::Node& MovementExplorer::node(unsigned int id) const {
  BT_ASSERT(id < impl_->nodes.size());
  return impl_->nodes[id];
}

bool MovementExplorer::exists(unsigned int id) const {
  return id < impl_->nodes.size() && impl_->alive[id];
}

Result<std::vector<unsigned int>> MovementExplorer::analyse(unsigned int id) {
  if (!exists(id)) {
    return fail(Errc::NotFound, "there is no such node");
  }
  Impl& m = *impl_;
  std::vector<NodeRef> res;
  m.analysator->completeFind(m.refs[id].get(), m.nodes[id].pieces, res);
  std::vector<unsigned int> added;
  for (NodeRef& r : res) {
    if (const std::optional<unsigned int> c = m.addNode(id, std::move(r))) {
      added.push_back(*c);
    }
  }
  return added;
}

Result<unsigned int> MovementExplorer::addMove(unsigned int id,
                                               const std::vector<unsigned int>& pieces,
                                               unsigned int direction, bool reverse) {
  if (!exists(id)) {
    return fail(Errc::NotFound, "there is no such node");
  }
  Impl& m = *impl_;
  if (direction >= m.cache->numDirections()) {
    return fail(Errc::InvalidArgument, "there is no such direction");
  }
  const Node& from = m.nodes[id];
  std::vector<bool> moved(from.pieces.size(), false);
  for (const unsigned int p : pieces) {
    bool found = false;
    for (unsigned int i = 0; i < from.pieces.size(); i++) {
      if (from.pieces[i] == p) {
        moved[i] = true;
        found = true;
      }
    }
    if (!found) {
      return fail(Errc::InvalidArgument, "that piece is not in this position");
    }
  }
  if (pieces.empty()) {
    return fail(Errc::InvalidArgument, "no piece to move");
  }

  int x = 0;
  int y = 0;
  int z = 0;
  m.cache->getDirection(direction, &x, &y, &z);
  if (reverse) {
    x = -x;
    y = -y;
    z = -z;
  }
  const DisassemblerNode& f = *m.refs[id];
  NodeRef n = NodeRef::make(static_cast<unsigned int>(from.pieces.size()), m.refs[id], 0, 0);
  for (unsigned int i = 0; i < from.pieces.size(); i++) {
    // relative to where the piece was (set(i, tx, ty, tz) of the original)
    const int d = moved[i] ? 1 : 0;
    n->set(i, f.getX(i) + d * x, f.getY(i) + d * y, f.getZ(i) + d * z, f.getTrans(i));
  }
  const std::optional<unsigned int> c = m.addNode(id, std::move(n));
  if (!c) {
    return fail(Errc::InvalidArgument, "the move takes the pieces out of the puzzle");
  }
  return *c;
}

Result<unsigned int> MovementExplorer::findMatching(unsigned int id, unsigned int piece, int dx,
                                                    int dy, int dz) {
  if (!exists(id)) {
    return fail(Errc::NotFound, "there is no such node");
  }
  Impl& m = *impl_;
  const std::vector<unsigned int>& present = m.nodes[id].pieces;
  if (std::find(present.begin(), present.end(), piece) == present.end()) {
    return fail(Errc::InvalidArgument, "that piece is not in this position");
  }
  NodeRef n = m.analysator->findMatching(m.refs[id].get(), present, piece, dx, dy, dz);
  if (!n) {
    return fail(Errc::NotFound, "no move does that");
  }
  const std::optional<unsigned int> c = m.addNode(id, std::move(n));
  if (!c) {
    return fail(Errc::NotFound, "no move does that");
  }
  return *c;
}

void MovementExplorer::prune(unsigned int id) {
  if (!exists(id)) {
    return;
  }
  Impl& m = *impl_;
  // everything below the nodes of the path goes, but the path
  const auto drop = [&m](unsigned int n, auto& self) -> void {
    m.alive[n] = false;
    for (const unsigned int c : m.nodes[n].children) {
      self(c, self);
    }
    m.nodes[n].children.clear();
  };
  unsigned int omit = id;
  for (const unsigned int c : m.nodes[id].children) {
    drop(c, drop);
  }
  m.nodes[id].children.clear();
  while (omit != 0) {
    const unsigned int up = m.nodes[omit].parent;
    std::vector<unsigned int> keep;
    for (const unsigned int c : m.nodes[up].children) {
      if (c == omit) {
        keep.push_back(c);
      } else {
        drop(c, drop);
      }
    }
    m.nodes[up].children = keep;
    omit = up;
  }
}

std::vector<std::string> MovementExplorer::directionLabels() const {
  std::vector<std::string> out;
  for (unsigned int i = 0; i < impl_->cache->numDirections(); i++) {
    int x = 0;
    int y = 0;
    int z = 0;
    impl_->cache->getDirection(i, &x, &y, &z);
    std::string f;
    std::string r;
    if (x > 0) {
      f += "x";
      r += "-x";
    }
    if (x < 0) {
      f += "-x";
      r += "x";
    }
    if (y > 0) {
      f += "y";
      r += "-y";
    }
    if (y < 0) {
      f += "-y";
      r += "y";
    }
    if (z > 0) {
      f += "z";
      r += "-z";
    }
    if (z < 0) {
      f += "-z";
      r += "z";
    }
    out.push_back(f);
    out.push_back(r);
  }
  return out;
}

}  // namespace bt::app
