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

#include "shapelogic/app/background_job.h"
#include "shapelogic/base/error.h"
#include "shapelogic/base/stop.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/solve/assembler.h"
#include "shapelogic/solve/prepare_progress.h"
#include "shapelogic/solve/solver_type.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sl {
class MovementAnalysator;
class MovementCache;
class Puzzle;
class NodeRef;
}  // namespace sl

namespace sl::app {

/// A placement of a piece: its orientation and position.
struct PiecePlacement {
  unsigned char transformation = 0;
  int x = 0;
  int y = 0;
  int z = 0;
};

/// The assembler of a problem, made for exploring: its placements, and its
/// search one step at a time.
///
/// It is made in two steps (cleanup item 18.1): copy(), which is quick and
/// reads the puzzle, and build(), which makes the matrix and can take
/// minutes on a big problem but reads only the explorer's own copy, so it
/// runs without the lock a solve of the problem writes under, and on a
/// thread of its own (AssemblyExplorerBuild).
class AssemblyExplorer {
 public:
  /// The first step: a copy of `puzzle` with problem `problemIndex` (the
  /// hotspots reset) and the assembler the problem's solve would make, its
  /// matrix not yet built: a sliding puzzle's as its solve makes it (start
  /// locks set up, Classic's assembler, every start kept, colors matched
  /// strictly, whatever `solver` and `matrix` say). Refused for grids
  /// without an assembler (stacking puzzles among them: Unsupported), a
  /// problem that is not there (NotFound) and one without a result or
  /// pieces (InvalidArgument). The caller holds whatever guards the puzzle
  /// for the call; nothing of the puzzle is read after it.
  static Result<AssemblyExplorer> copy(const Puzzle& puzzle, unsigned int problemIndex,
                                       SolverType solver, const MatrixOptions& matrix);

  /// The second step, once: the matrix made, and reduced when asked
  /// (Prepare in the original). Polls `stop`. InvalidArgument with the
  /// assembler's message when the matrix cannot be made (the units, a piece
  /// that fits nowhere), InvalidState when it was stopped: the explorer is
  /// then good for nothing but being let go. Only pieceCount() and
  /// prepareProgress() may be asked before it has returned without an
  /// error.
  Result<void> build(bool reduce, const StopToken& stop);

  AssemblyExplorer(AssemblyExplorer&&) noexcept;
  AssemblyExplorer& operator=(AssemblyExplorer&&) noexcept;
  ~AssemblyExplorer();

  /// build() has made the matrix.
  [[nodiscard]] bool built() const { return built_; }
  /// How far build() is, and whether it is at the reduce. Any thread, while
  /// build() runs too.
  [[nodiscard]] PrepareProgress prepareProgress() const;
  [[nodiscard]] bool reducing() const;

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
  AssemblyExplorer();
  std::unique_ptr<Puzzle> puzzle_;
  std::unique_ptr<Assembler> assembler_;
  /// The matrix's options, as the solve's setup gave them (copy()).
  MatrixOptions matrix_;
  bool built_ = false;
  /// build() is at its reduce; a pointer so that the explorer still moves.
  std::unique_ptr<std::atomic<bool>> reducing_;
};

/// The build of an assembly explorer on a thread of its own, with a stop
/// and its progress (cleanup item 18.1: built by the caller, a big
/// problem's matrix held the engine's one request thread for minutes).
/// Whoever opened the explorer keeps it and asks now and then whether it
/// has ended. It works on the explorer's own copy of the puzzle: the
/// document may change or go meanwhile.
class AssemblyExplorerBuild final : public BackgroundJob {
 public:
  /// Starts at once: `explorer` is one copy() gave.
  AssemblyExplorerBuild(AssemblyExplorer explorer, bool reduce);
  /// Stops and waits.
  ~AssemblyExplorerBuild() override;

  /// How far the matrix is, and whether the reduce runs. Any thread.
  [[nodiscard]] PrepareProgress progress() const;
  [[nodiscard]] bool reducing() const;
  /// Once it has ended (active() is false), once: the explorer, or why
  /// there is none (build()'s errors; Internal with what an exception
  /// said).
  [[nodiscard]] Result<AssemblyExplorer> take();

 private:
  void run() override;

  AssemblyExplorer explorer_;
  bool reduce_;
  /// What build() answered; under mutex_.
  Result<void> end_;
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
  /// their ids; positions met before at that node are not added twice. A
  /// move that takes pieces out of the puzzle gives no position: it is not
  /// added.
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

}  // namespace sl::app
