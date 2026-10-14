// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The instructions for taking a puzzle apart (and so for putting it
/// together): a tree of separations, each a list of states that ends with
/// the puzzle in two parts. Only the data; the searches that build these
/// come in stage 4.
/// Ported from src/lib/disassembly.{h,cpp} (XML reading and writing lives in
/// bt/io).

#include "shapelogic/base/assert.h"
#include "shapelogic/base/clone_ptr.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace bt {

/// What Separation and SeparationInfo have in common: the move counts and
/// the level text of a disassembly.
class Disassembly {
 public:
  virtual ~Disassembly() = default;

  /// The number of linear (slide) moves to take the puzzle apart completely.
  [[nodiscard]] virtual unsigned int sumMoves() const = 0;

  /// The number of rotation moves.
  [[nodiscard]] virtual unsigned int sumRotations() const { return 0; }

  /// Slides plus rotations.
  [[nodiscard]] unsigned int sumSteps() const { return sumMoves() + sumRotations(); }

  /// The level text: dot-separated move counts, one per piece removal
  /// ("3.1.1"), with "R<n>" rotation counts when there are rotations.
  [[nodiscard]] virtual std::string movesText() const = 0;

  /// Compares levels: >0 when this disassembly has the higher level (all
  /// sequences equal up to one where this one is longer), <0 when lower, 0
  /// when equal.
  [[nodiscard]] int compare(const Disassembly& s2) const;

  /// The number of states of the x-th move sequence (pre-order), 0 past the
  /// end.
  [[nodiscard]] virtual unsigned int getSequenceLength(unsigned int x) const = 0;

  /// The number of move sequences.
  [[nodiscard]] virtual unsigned int getNumSequences() const = 0;

 protected:
  Disassembly() = default;
  Disassembly(const Disassembly&) = default;
  Disassembly(Disassembly&&) = default;
  Disassembly& operator=(const Disassembly&) = default;
  Disassembly& operator=(Disassembly&&) = default;
};

/// The positions of the pieces of one separation at one step, relative to
/// the assembled puzzle; optionally their orientations and, for a state
/// reached by a rotation, that rotation.
class State {
 public:
  /// The rotPiece value of a state not reached by a rotation.
  static constexpr unsigned int kNoRotation = 0xFFFFFFFF;

  /// A state for `pn` pieces, all at 0 with orientation 0.
  explicit State(unsigned int pn);

  /// A state without orientations (what files without <dt> give).
  static State withoutOrientations(unsigned int pn);

  /// Sets the position of a piece.
  void set(unsigned int piece, int x, int y, int z);

  /// Sets the position and the orientation of a piece.
  void set(unsigned int piece, int x, int y, int z, unsigned int orient);

  [[nodiscard]] int getX(unsigned int i) const {
    BT_ASSERT(i < dx_.size());
    return dx_[i];
  }
  [[nodiscard]] int getY(unsigned int i) const {
    BT_ASSERT(i < dy_.size());
    return dy_[i];
  }
  [[nodiscard]] int getZ(unsigned int i) const {
    BT_ASSERT(i < dz_.size());
    return dz_[i];
  }

  /// The orientation of a piece; 0 when the state has none (older files).
  [[nodiscard]] unsigned int getOrient(unsigned int i) const {
    BT_ASSERT(i < dx_.size());
    return dt_.empty() ? 0 : static_cast<unsigned int>(dt_[i]);
  }

  /// True when the state stores orientations.
  [[nodiscard]] bool hasOrientations() const { return !dt_.empty(); }

  /// Whole coordinate vectors (the file writer).
  [[nodiscard]] const std::vector<int>& xs() const { return dx_; }
  [[nodiscard]] const std::vector<int>& ys() const { return dy_; }
  [[nodiscard]] const std::vector<int>& zs() const { return dz_; }
  [[nodiscard]] const std::vector<int>& orientations() const { return dt_; }

  /// Replaces the coordinate vectors (the file reader); each must be empty
  /// or have one value per piece.
  void setXs(std::vector<int> v) { dx_ = std::move(v); }
  void setYs(std::vector<int> v) { dy_ = std::move(v); }
  void setZs(std::vector<int> v) { dz_ = std::move(v); }
  void setOrientations(std::vector<int> v) { dt_ = std::move(v); }

  /// Records that this state was reached by rotating `piece` about the pivot
  /// (doubled cell coordinates: a cell's centre is at 2x) around `axis`.
  void setRotationArrival(unsigned int piece, int px, int py, int pz, unsigned int axis,
                          unsigned int sense);
  void clearRotationArrival();
  [[nodiscard]] bool isRotationArrival() const { return rotPiece_ != kNoRotation; }
  [[nodiscard]] unsigned int getRotPiece() const { return rotPiece_; }
  [[nodiscard]] int getRotPivotX() const { return rotPivotX_; }
  [[nodiscard]] int getRotPivotY() const { return rotPivotY_; }
  [[nodiscard]] int getRotPivotZ() const { return rotPivotZ_; }
  [[nodiscard]] unsigned int getRotAxis() const { return rotAxis_; }
  [[nodiscard]] unsigned int getRotSense() const { return rotSense_; }

  /// True when the piece has left the puzzle (moved more than 10000 units).
  [[nodiscard]] bool pieceRemoved(unsigned int i) const;

  /// The number of pieces.
  [[nodiscard]] unsigned int getPiecenumber() const {
    return static_cast<unsigned int>(dx_.size());
  }

 private:
  State() = default;

  std::vector<int> dx_;
  std::vector<int> dy_;
  std::vector<int> dz_;
  std::vector<int> dt_;  ///< empty for states without orientations

  unsigned int rotPiece_ = kNoRotation;
  int rotPivotX_ = 0;
  int rotPivotY_ = 0;
  int rotPivotZ_ = 0;
  unsigned int rotAxis_ = 0;
  unsigned int rotSense_ = 0;
};

/// One node of a disassembly tree: the pieces it handles, the states that
/// separate them into two groups, and the trees for the two groups (none
/// for a group of one piece). The first state is the starting position
/// (all zero for the root), the last moves the removed group far away.
class Separation final : public Disassembly {
 public:
  /// A node for the pieces `pcs` (piece numbers of the problem), with the
  /// trees of the removed and the remaining group.
  Separation(ClonePtr<Separation> removed, ClonePtr<Separation> left,
             std::vector<unsigned int> pcs);

  /// The number of moves, one less than the number of states.
  [[nodiscard]] unsigned int getMoves() const {
    return static_cast<unsigned int>(states_.size()) - 1;
  }

  /// Rotation steps among the moves of this node.
  [[nodiscard]] unsigned int getRotations() const;

  /// Linear steps among the moves of this node.
  [[nodiscard]] unsigned int getSlides() const;

  [[nodiscard]] unsigned int getNumStates() const {
    return static_cast<unsigned int>(states_.size());
  }
  [[nodiscard]] const State& getState(unsigned int num) const {
    BT_ASSERT(num < states_.size());
    return states_[num];
  }

  /// The tree of the group that stays, or nullptr for a single piece.
  [[nodiscard]] const Separation* getLeft() const { return left_.get(); }
  /// The tree of the group that is removed, or nullptr for a single piece.
  [[nodiscard]] const Separation* getRemoved() const { return removed_.get(); }

  /// Adds a state at the front (searches build the list backwards).
  void addstate(State st);

  /// Adds a state at the end (the file reader).
  void appendState(State st);

  [[nodiscard]] unsigned int getPieceNumber() const {
    return static_cast<unsigned int>(pieces_.size());
  }
  [[nodiscard]] unsigned int getPieceName(unsigned int num) const {
    BT_ASSERT(num < pieces_.size());
    return pieces_[num];
  }
  [[nodiscard]] const std::vector<unsigned int>& getPieces() const { return pieces_; }

  /// Pieces s1 and s2 changed places in the problem.
  void exchangeShape(unsigned int s1, unsigned int s2);

  /// Pieces [from, from+cnt) were removed from the problem (none of them is
  /// in this tree).
  void removePieces(unsigned int from, unsigned int cnt);

  /// `cnt` pieces were inserted into the problem at `from`.
  void addNonPlacedPieces(unsigned int from, unsigned int cnt);

  [[nodiscard]] unsigned int sumMoves() const override;
  [[nodiscard]] unsigned int sumRotations() const override;
  [[nodiscard]] std::string movesText() const override;
  [[nodiscard]] unsigned int getSequenceLength(unsigned int x) const override;
  [[nodiscard]] unsigned int getNumSequences() const override;

 private:
  int movesTextPieceRemovals(char* txt, int len, bool withRots, unsigned int carrySlides,
                             unsigned int carryRots) const;

  std::vector<unsigned int> pieces_;
  std::deque<State> states_;
  ClonePtr<Separation> removed_;
  ClonePtr<Separation> left_;
  unsigned int numSequences_;
};

/// A compact form of a disassembly tree: only the number of states of each
/// node (and its rotations), in pre-order (node, removed, left), with 0 for
/// a missing branch. What is kept for solutions whose full disassembly is
/// not stored.
class SeparationInfo final : public Disassembly {
 public:
  /// The summary of a full tree.
  explicit SeparationInfo(const Separation& sep);

  /// From the pre-order lists (the file reader, which checks that they form
  /// a complete tree and that the lists have the same length).
  SeparationInfo(std::vector<unsigned int> values, std::vector<unsigned int> rotValues);

  [[nodiscard]] const std::vector<unsigned int>& getValues() const { return values_; }
  [[nodiscard]] const std::vector<unsigned int>& getRotValues() const { return rotValues_; }

  [[nodiscard]] unsigned int sumMoves() const override;
  [[nodiscard]] unsigned int sumRotations() const override;
  [[nodiscard]] std::string movesText() const override;
  [[nodiscard]] unsigned int getSequenceLength(unsigned int x) const override;
  [[nodiscard]] unsigned int getNumSequences() const override;

 private:
  void recursiveConstruction(const Separation& sep);
  int movesTextPieceRemovals(char* txt, int len, unsigned int idx, unsigned int carrySlides,
                             unsigned int carryRots) const;
  [[nodiscard]] unsigned int skipSubtree(unsigned int idx) const;

  std::vector<unsigned int> values_;
  std::vector<unsigned int> rotValues_;  ///< rotation steps per node, 0 for markers
};

}  // namespace bt
