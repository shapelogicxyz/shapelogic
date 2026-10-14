// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Groups: pieces that need not come apart from each other. When identical
/// pieces belong to different groups, which group a piece is in is not
/// known, and it can decide whether the puzzle comes apart (eight identical
/// pieces, four in group A and four in B, assembled so that always four
/// interlock: it depends on how the groups are spread over the two halves).
///
/// The take-apart stops early only where every piece has one group. Else it
/// tries to take the pieces apart, and when it cannot, asks this class
/// whether the pieces can be given groups so that all of them are in one;
/// the class keeps the groups it gave out and takes them into account for
/// the sets that follow.
/// Ported from src/lib/grouping.{h,cpp}.

#include <vector>

namespace bt {

/// Gives groups to sets of pieces, backing off earlier sets when a later
/// one does not fit.
class Grouping {
 public:
  Grouping() = default;
  Grouping(const Grouping&) = delete;
  Grouping& operator=(const Grouping&) = delete;

  /// `count` pieces of part `pc` may be in group `group` (0 is no group and
  /// adds nothing). The disassembler keys by the part index, as it asks
  /// (the original registered the shape id and asked with the part index:
  /// a deliberate difference, stage 4 audit).
  void addPieces(unsigned int pc, unsigned int group, unsigned int count);

  /// Forgets the sets and gives back what they took of the counts: the
  /// next take-apart starts with the counts addPieces registered. (The
  /// original kept them used up, so every take-apart after the first of a
  /// disassembler found the groups full; a deliberate difference, stage 4
  /// audit.)
  void reSet();

  /// Starts a new set.
  void newSet();

  /// Adds a piece of part `pc` to the current set: false when the sets so
  /// far can no longer all be given a group. After false, stop: later calls
  /// give nothing useful.
  bool addPieceToSet(unsigned int pc);

 private:
  struct Set {
    unsigned int currentGroup = 0;
    std::vector<unsigned int> pieces;
  };

  struct Piece {
    unsigned int piece = 0;
    unsigned int group = 0;
    /// What is left of the group's count in this take-apart.
    int count = 0;
    /// The count addPieces registered.
    int registered = 0;
  };

  [[nodiscard]] unsigned int findPiece(unsigned int pc, unsigned int gp) const;

  std::vector<Set> sets_;
  std::vector<Piece> pieces_;
  unsigned int numGroups_ = 0;
  bool failed_ = false;
};

}  // namespace bt
