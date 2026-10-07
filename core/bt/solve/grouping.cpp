// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/grouping.cpp.
#include "bt/solve/grouping.h"

#include "bt/base/assert.h"

namespace bt {

unsigned int Grouping::findPiece(unsigned int pc, unsigned int gp) const {
  unsigned int i = 0;
  for (; i < pieces_.size(); i++) {
    if (pieces_[i].piece == pc && pieces_[i].group == gp) {
      break;
    }
  }
  return i;
}

void Grouping::addPieces(unsigned int pc, unsigned int group, unsigned int count) {
  if (group == 0) {
    return;
  }

  const unsigned int i = findPiece(pc, group);

  if (i < pieces_.size()) {
    pieces_[i].count += static_cast<int>(count);
    pieces_[i].registered += static_cast<int>(count);
  } else {
    pieces_.push_back(Piece{pc, group, static_cast<int>(count), static_cast<int>(count)});
    if (group + 1 > numGroups_) {
      numGroups_ = group + 1;
    }
  }
}

void Grouping::reSet() {
  sets_.clear();
  failed_ = false;
  for (Piece& p : pieces_) {
    p.count = p.registered;
  }
}

void Grouping::newSet() {
  if (!failed_) {
    sets_.push_back(Set{1, {}});
  }
}

bool Grouping::addPieceToSet(unsigned int pc) {
  if (failed_) {
    return false;
  }

  BT_ASSERT(!sets_.empty());

  auto set = static_cast<unsigned int>(sets_.size() - 1);

  // the piece joins the current set, in the set's group if there is room
  sets_[set].pieces.push_back(pc);

  const unsigned int i = findPiece(pc, sets_[set].currentGroup);

  if (i < pieces_.size() && pieces_[i].count > 0) {
    pieces_[i].count--;
    return true;
  }

  // no room: the set leaves its group
  for (unsigned int p = 0; p + 1 < sets_[set].pieces.size(); p++) {
    pieces_[findPiece(sets_[set].pieces[p], sets_[set].currentGroup)].count++;
  }

  // Try the next groups for the last set; when it has tried them all, it
  // starts over at group 0 and the set before it moves on, until a fit is
  // found or the first set has run out.
  do {
    sets_[set].currentGroup++;

    if (sets_[set].currentGroup >= numGroups_) {
      // every group tried for this set
      sets_[set].currentGroup = 0;

      if (set == 0) {
        failed_ = true;
        return false;
      }

      set--;

      // the set before leaves its group
      for (const unsigned int piece : sets_[set].pieces) {
        pieces_[findPiece(piece, sets_[set].currentGroup)].count++;
      }

    } else {
      // the set's pieces into the group tried
      for (unsigned int p = 0; p < sets_[set].pieces.size(); p++) {
        const unsigned int j = findPiece(sets_[set].pieces[p], sets_[set].currentGroup);

        if (j < pieces_.size() && pieces_[j].count > 0) {
          pieces_[j].count--;
        } else {
          // no room: take back what was placed, and try again with this set
          // (set-- here and set++ below; for set 0 this wraps and comes
          // back, as in the original)
          for (unsigned int p2 = 0; p2 < p; p2++) {
            pieces_[findPiece(sets_[set].pieces[p2], sets_[set].currentGroup)].count++;
          }
          set--;
          break;
        }
      }
      set++;
    }

  } while (set < sets_.size());

  return true;
}

}  // namespace bt
