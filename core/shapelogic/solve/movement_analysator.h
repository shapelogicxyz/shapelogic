// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The moves from one position of a take-apart: Bill Cutler's algorithm
/// (from his booklet "Computer Analysis of All 6 Piece Burrs"; the comments
/// assume it is known). From how far each piece can move against each
/// other, it finds the pieces that can be removed, then the groups, then
/// the slides by any number of steps (with groups that move together), and,
/// on the cube grid when asked for, the 90 degree turns.
/// Ported from src/lib/movementanalysator.{h,cpp}.

#include "shapelogic/base/stop.h"
#include "shapelogic/solve/disassembler_node.h"
#include "shapelogic/solve/node_hash.h"
#include "shapelogic/solve/rotation_moves_classic.h"
#include "shapelogic/solve/solver_type.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace bt {

class MovementCache;
class Problem;
class Puzzle;
class RotationMoves;

/// The analysis of one problem's positions. An analysator belongs to one
/// thread (it has its own movement cache); a take-apart over several
/// threads uses one per thread.
class MovementAnalysator {
 public:
  /// A/B switches (environment variables of the original).
  struct Options {
    /// Ask the cache only about the pairs of pieces that lie differently
    /// from the last position, close the matrix in one pass, and test moves
    /// on bit masks; off fills and closes the whole matrix for every
    /// position, as before (SHAPELOGIC_NO_DISASM_FAST). The moves are the
    /// same either way.
    bool fast = true;
    /// The Classic turn generator's switches (SHAPELOGIC_NO_ROT_FAST,
    /// SHAPELOGIC_NO_ROT_ANCHOR).
    RotationMovesClassic::Options rotations;
  };

  /// An analysator for `problem` of `puzzle` (both must outlive it), on a
  /// grid that has a movement cache. `rotations` asks for 90 degree turns
  /// (cube grid only), with the generator of `solver`.
  MovementAnalysator(const Puzzle& puzzle, const Problem& problem, bool rotations,
                     SolverType solver, Options options);
  ~MovementAnalysator();

  MovementAnalysator(const MovementAnalysator&) = delete;
  MovementAnalysator& operator=(const MovementAnalysator&) = delete;

  /// Turns the 90 degree turns on or off (only where the grid has them).
  void setCheckRotations(bool enable);

  /// The take-apart's stop: find() then returns null as if there were no
  /// more moves, so the caller must look at the token itself.
  void setStop(const StopToken& stop);

  /// Microseconds spent finding turns, and slides and removals.
  [[nodiscard]] std::uint64_t getRotationSearchUs() const {
    return rotationSearchUs_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t getLinearSearchUs() const {
    return linearSearchUs_.load(std::memory_order_relaxed);
  }

  /// Starts the moves from `nd`, a position of the pieces `pieces` (both
  /// must stay until the search of them is over).
  void initFind(DisassemblerNode* nd, const std::vector<unsigned int>& pieces);

  /// The next move, as a new position; null when there are no more.
  NodeRef find();

  /// Gives up what find() holds of the position it was searching: needed
  /// when the search of a position is left before find() returned null and
  /// another thread may take up that position before the next initFind
  /// here.
  void endFind();

  /// Every move from `searchnode` of one step (removals included), each
  /// position once, into `result`.
  void completeFind(DisassemblerNode* searchnode, const std::vector<unsigned int>& pieces,
                    std::vector<NodeRef>& result);

  /// The first move of one step from `nd` that moves `piece` (a number of
  /// the problem, one of `pcs`) by (dx, dy, dz), taking as few others along
  /// as possible; null when there is none.
  NodeRef findMatching(DisassemblerNode* nd, const std::vector<unsigned int>& pcs,
                       unsigned int piece, int dx, int dy, int dz);

  /// Counters of the move search since construction or resetStats(). Nothing
  /// in the search reads them.
  struct Stats {
    unsigned long checkCalls = 0;     ///< checkmovement() calls
    unsigned long checkSuccess = 0;   ///< ... that allowed a move
    unsigned long nodesReturned = 0;  ///< positions handed out by find()
  };
  [[nodiscard]] const Stats& getStats() const { return stats_; }
  void resetStats() { stats_ = Stats(); }

 private:
  void prepare();
  void prepareFill();
  void closureFull();
  void closeFrom(const std::vector<unsigned int>& fill);
  void beginSearchPhase(bool linear);
  void switchToRotationPhase();
  void flushSearchPhase();
  bool checkmovement(unsigned int maxPieces, unsigned int step);
  bool checkmovementMasks(unsigned int maxPieces, unsigned int step);
  NodeRef newNode(unsigned int amount);
  NodeRef newNodeMerge(const DisassemblerNode* n0, const DisassemblerNode* n1);

  std::unique_ptr<MovementCache> cache_;
  Options options_;

  /// One matrix per direction, interleaved ([(x + piecenumber * y) * dirs +
  /// d]); the opposite direction is its transpose, read so in
  /// checkmovement.
  std::vector<unsigned int> matrix_;
  std::vector<unsigned int> movement_;
  std::vector<int> weights_;
  /// checkmovement's scratch (a member: it is on the hot path).
  std::vector<char> check_;
  unsigned int piecenumber_;

  /// The movement values of the last position prepared, before closing, with
  /// where its pieces lay (x, y, z, orientation each) and which pieces they
  /// were: prepare() asks the cache only about pairs that lie differently.
  std::vector<unsigned int> prevFill_;
  std::vector<int> prevPlace_;
  std::vector<int> place_;
  std::vector<unsigned int> prevIds_;
  /// One direction's matrix, pieces in use only, for closeFrom().
  std::vector<unsigned int> dense_;
  /// checkmovementMasks: per direction (with its sign) and step, the pieces
  /// each piece takes along, 64 words a set; pushValid_ says which sets are
  /// worked out for the position at hand.
  static constexpr unsigned int kPushSlots = 16;
  std::vector<std::uint64_t> pushMasks_;
  std::vector<std::uint64_t> pushTemp_;
  std::vector<char> pushValid_;

  /// The positions of the current direction's slides, for the mergers.
  CountingNodeHash nodes_;

  bool checkRotations_ = false;
  bool bricksGrid_ = false;
  std::unique_ptr<RotationMoves> rotationMoves_;
  StopToken stop_;
  std::atomic<std::uint64_t> rotationSearchUs_{0};
  std::atomic<std::uint64_t> linearSearchUs_{0};
  std::uint64_t searchPhaseStartUs_ = 0;
  bool searchTimingOpen_ = false;
  bool searchPhaseLinear_ = true;

  // where find() stands
  int nextpiece_ = 0;
  int nextPn_ = 0;
  int nextstate_ = -1;
  int state99nextState_ = 0;
  unsigned int nextdir_ = 0;
  unsigned int maxstep_ = static_cast<unsigned int>(-1);
  unsigned int nextstep_ = 0;
  const DisassemblerNode* state99node_ = nullptr;
  DisassemblerNode* searchnode_ = nullptr;
  const std::vector<unsigned int>* pieces_ = nullptr;

  Stats stats_;
};

}  // namespace bt
