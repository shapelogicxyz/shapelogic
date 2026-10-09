// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The step-by-step explorers of the solver: the placements of a piece in
/// the result, the assembly search one step at a time, and the movements
/// of a solution's take-apart, position by position.
/// Ported from src/gui/placementbrowser.cpp, the assembler step of
/// mainWindow_c::cb_BtnAssemblerStep (src/gui/mainwindow.cpp) and
/// src/gui/movementbrowser.cpp. The original worked on the problem's
/// prepared assembler (after Prepare); these make their own from a copy of
/// the puzzle, so stepping one never moves a solve.

#include "bt/base/error.h"
#include "bt/model/assembly.h"
#include "bt/solve/assembler.h"
#include "bt/solve/solver_type.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bt {
class MovementAnalysator;
class MovementCache;
class Puzzle;
class NodeRef;
}  // namespace bt

namespace bt::app {

/// A placement of a piece: its orientation and position.
struct PiecePlacement {
  unsigned char transformation = 0;
  int x = 0;
  int y = 0;
  int z = 0;
};

/// The assembler of a problem, made for exploring: its placements, and its
/// search one step at a time.
class AssemblyExplorer {
 public:
  /// Prepares the search of problem `problemIndex` of a copy of `puzzle`
  /// (hotspots reset, the matrix made, reduced when asked): Prepare in the
  /// original. Refused for grids without an assembler, sliding and
  /// stacking puzzles, a problem without a result or pieces
  /// (Unsupported / InvalidArgument), and when the matrix cannot be made
  /// (InvalidArgument with the assembler's message).
  static Result<AssemblyExplorer> make(const Puzzle& puzzle, unsigned int problemIndex,
                                       SolverType solver, const MatrixOptions& matrix,
                                       bool reduce);

  AssemblyExplorer(AssemblyExplorer&&) noexcept;
  AssemblyExplorer& operator=(AssemblyExplorer&&) noexcept;
  ~AssemblyExplorer();

  /// Whether the placement browser works for this assembler.
  [[nodiscard]] bool placementsSupported() const;
  /// The number of pieces.
  [[nodiscard]] unsigned int pieceCount() const;
  /// Every placement of a piece, in the assembler's order.
  [[nodiscard]] std::vector<PiecePlacement> placements(unsigned int piece) const;

  /// Advances the search `steps` steps (the "Step" button: 1).
  void step(unsigned long steps);
  /// The pieces placed now.
  [[nodiscard]] Assembly currentAssembly() const;
  /// How far the search is (0 to 1); 1 when it is through.
  [[nodiscard]] float finished() const;

 private:
  AssemblyExplorer() = default;
  std::unique_ptr<Puzzle> puzzle_;
  std::unique_ptr<Assembler> assembler_;
};

/// The movement browser: positions reached from a solution's assembly by
/// moves, as a tree the user grows.
class MovementExplorer {
 public:
  /// A position of the tree.
  struct Node {
    unsigned int id = 0;
    /// The node it came from (the root: itself).
    unsigned int parent = 0;
    /// "x: S1, S2.2": the direction and the pieces moved; empty for the root.
    std::string label;
    /// The problem's piece numbers present, and where each is (index for
    /// index), in grid units.
    std::vector<unsigned int> pieces;
    std::vector<int> x;
    std::vector<int> y;
    std::vector<int> z;
    std::vector<unsigned int> children;
  };

  /// The tree for saved solution `solution` of problem `problemIndex` (a
  /// copy of the puzzle is kept). Rotations: whether the analysis also
  /// tries 90 degree turns. Refused for grids without take-apart
  /// (Unsupported) and a missing solution (NotFound).
  static Result<MovementExplorer> make(const Puzzle& puzzle, unsigned int problemIndex,
                                       unsigned int solution, bool rotations);

  MovementExplorer(MovementExplorer&&) noexcept;
  MovementExplorer& operator=(MovementExplorer&&) noexcept;
  ~MovementExplorer();

  [[nodiscard]] const Node& root() const { return node(0); }
  /// A node; ids come from root(), children and the calls below. The
  /// reference stays valid while the explorer exists (a pruned node too).
  [[nodiscard]] const Node& node(unsigned int id) const;
  /// Whether a node exists (pruned ones do not).
  [[nodiscard]] bool exists(unsigned int id) const;

  /// Adds every position one move away from a node (Analyze) and returns
  /// their ids; positions met before at that node are not added twice.
  Result<std::vector<unsigned int>> analyse(unsigned int id);
  /// Adds the position where the given pieces (the problem's piece
  /// numbers) moved one step in direction `direction` of the grid's
  /// movement directions, negated with `reverse` (Add Movement), valid or
  /// not. Returns its id, or that of the child already there.
  Result<unsigned int> addMove(unsigned int id, const std::vector<unsigned int>& pieces,
                               unsigned int direction, bool reverse);
  /// The first valid move that moves piece `piece` (the problem's piece
  /// number) by (dx, dy, dz), one step in one of the grid's directions,
  /// taking as few others as needed; the id of its node, NotFound when
  /// there is none, InvalidArgument for a piece not in the position.
  Result<unsigned int> findMatching(unsigned int id, unsigned int piece, int dx, int dy, int dz);
  /// Keeps only the path from the root to `id`: every other node goes, the
  /// children of `id` too (Prune Tree).
  void prune(unsigned int id);

  /// The grid's movement directions, as the dialog labels them ("x",
  /// "-y", "xy", ...): direction d forward is label 2d, reversed 2d+1.
  [[nodiscard]] std::vector<std::string> directionLabels() const;
  /// The grid's movement directions as steps in grid coordinates (direction
  /// d forward; reversed is its negation): what a dragged piece's move is
  /// matched against, since a label does not say how far a step goes (a
  /// prism step along x is 2).
  [[nodiscard]] std::vector<std::array<int, 3>> directionVectors() const;

 private:
  MovementExplorer() = default;

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace bt::app
