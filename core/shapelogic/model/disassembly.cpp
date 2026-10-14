// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/disassembly.cpp (the data part; XML is in bt/io).

#include "shapelogic/model/disassembly.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace bt {

namespace {

/// Writes `s` at txt+at like snprintf would: cut to the room left, always
/// terminated, returning the full length. The level-text functions below are
/// a literal port of the original's fixed 256-byte buffers, so that long
/// level texts are cut off where the original cuts them.
int appendText(char* txt, int len, int at, std::string_view s) {
  const auto room = static_cast<std::size_t>(len - at);
  if (room > 0) {
    const std::size_t n = std::min(s.size(), room - 1);
    std::copy_n(s.data(), n, txt + at);
    txt[static_cast<std::size_t>(at) + n] = '\0';
  }
  return static_cast<int>(s.size());
}

std::string levelSegment(bool withRots, unsigned int slides, unsigned int rots) {
  return withRots ? std::to_string(slides) + "R" + std::to_string(rots)
                  : std::to_string(slides + rots);
}

constexpr int kTextBuffer = 256;

}  // namespace

// ---- State ------------------------------------------------------------------

State::State(unsigned int pn) : dx_(pn, 0), dy_(pn, 0), dz_(pn, 0), dt_(pn, 0) {}

State State::withoutOrientations(unsigned int pn) {
  State s;
  s.dx_.assign(pn, 0);
  s.dy_.assign(pn, 0);
  s.dz_.assign(pn, 0);
  return s;
}

void State::set(unsigned int piece, int x, int y, int z) {
  BT_ASSERT(piece < dx_.size());
  dx_[piece] = x;
  dy_[piece] = y;
  dz_[piece] = z;
}

void State::set(unsigned int piece, int x, int y, int z, unsigned int orient) {
  BT_ASSERT(piece < dx_.size());
  dx_[piece] = x;
  dy_[piece] = y;
  dz_[piece] = z;
  if (dt_.empty()) {
    dt_.assign(dx_.size(), 0);
  }
  dt_[piece] = static_cast<int>(orient);
}

void State::setRotationArrival(unsigned int piece, int px, int py, int pz, unsigned int axis,
                               unsigned int sense) {
  rotPiece_ = piece;
  rotPivotX_ = px;
  rotPivotY_ = py;
  rotPivotZ_ = pz;
  rotAxis_ = axis;
  rotSense_ = sense;
}

void State::clearRotationArrival() {
  rotPiece_ = kNoRotation;
  rotPivotX_ = rotPivotY_ = rotPivotZ_ = 0;
  rotAxis_ = rotSense_ = 0;
}

bool State::pieceRemoved(unsigned int i) const {
  BT_ASSERT(i < dx_.size());
  return std::abs(dx_[i]) > 10000 || std::abs(dy_[i]) > 10000 || std::abs(dz_[i]) > 10000;
}

// ---- Disassembly --------------------------------------------------------------

int Disassembly::compare(const Disassembly& s2) const {
  const unsigned int numSeq = std::max(getNumSequences(), s2.getNumSequences());

  for (unsigned int i = 0; i < numSeq; i++) {
    if (getSequenceLength(i) < s2.getSequenceLength(i)) {
      return -1;
    }
    if (getSequenceLength(i) > s2.getSequenceLength(i)) {
      return 1;
    }
  }

  return 0;
}

// ---- Separation ---------------------------------------------------------------

Separation::Separation(ClonePtr<Separation> removed, ClonePtr<Separation> left,
                       std::vector<unsigned int> pcs)
    : pieces_(std::move(pcs)),
      removed_(std::move(removed)),
      left_(std::move(left)),
      numSequences_((left_ ? left_->numSequences_ : 0) +
                    (removed_ ? removed_->numSequences_ : 0) + 1) {}

unsigned int Separation::sumMoves() const {
  BT_ASSERT(!states_.empty());
  unsigned int erg = getSlides();
  if (removed_) {
    erg += removed_->sumMoves();
  }
  if (left_) {
    erg += left_->sumMoves();
  }
  return erg;
}

namespace {

bool stateTransitionIsRotation(const State& a, const State& b, unsigned int pn) {
  if (!a.hasOrientations() || !b.hasOrientations()) {
    return false;
  }
  for (unsigned int i = 0; i < pn; i++) {
    if (a.pieceRemoved(i) || b.pieceRemoved(i)) {
      continue;
    }
    if (a.getOrient(i) != b.getOrient(i)) {
      return true;
    }
  }
  return false;
}

}  // namespace

unsigned int Separation::getRotations() const {
  unsigned int rots = 0;
  for (std::size_t i = 0; i + 1 < states_.size(); i++) {
    if (stateTransitionIsRotation(states_[i], states_[i + 1], getPieceNumber())) {
      rots++;
    }
  }
  return rots;
}

unsigned int Separation::getSlides() const { return getMoves() - getRotations(); }

unsigned int Separation::sumRotations() const {
  unsigned int erg = getRotations();
  if (removed_) {
    erg += removed_->sumRotations();
  }
  if (left_) {
    erg += left_->sumRotations();
  }
  return erg;
}

void Separation::addstate(State st) {
  BT_ASSERT(st.getPiecenumber() == pieces_.size());
  states_.push_front(std::move(st));
}

void Separation::appendState(State st) {
  BT_ASSERT(st.getPiecenumber() == pieces_.size());
  states_.push_back(std::move(st));
}

/* Each dotted segment is the step count until a single piece leaves the
 * puzzle. A split into two multi-piece groups does not emit a segment; its
 * moves are added to the next piece removal in the removed branch (the order
 * of the solve animation). */
int Separation::movesTextPieceRemovals(char* txt, int len, bool withRots, unsigned int carrySlides,
                                       unsigned int carryRots) const {
  const unsigned int slides = carrySlides + getSlides();
  const unsigned int rots = carryRots + getRotations();

  // both sides are groups: carry into removed, then left
  if (removed_ && left_) {
    int len2 = removed_->movesTextPieceRemovals(txt, len, withRots, slides, rots);
    if (len2 < 0 || len2 + 5 > len) {
      return len2;
    }

    std::array<char, kTextBuffer> cont{};
    const int clen = left_->movesTextPieceRemovals(cont.data(), kTextBuffer, withRots, 0, 0);
    if (clen > 0) {
      if (len2 > 0) {
        appendText(txt, len, len2, ".");
        len2++;
      }
      appendText(txt, len, len2, cont.data());
      len2 += clen;
    }
    return len2;
  }

  // at least one side is a single piece: one piece-removal segment
  int len2 = appendText(txt, len, 0, levelSegment(withRots, slides, rots));

  if (len2 < 0 || len2 + 5 > len) {
    return len2;
  }

  const Separation* cont = removed_ ? removed_.get() : left_.get();
  if (cont != nullptr) {
    std::array<char, kTextBuffer> buf{};
    const int clen = cont->movesTextPieceRemovals(buf.data(), kTextBuffer, withRots, 0, 0);
    if (clen > 0) {
      appendText(txt, len, len2, ".");
      len2++;
      appendText(txt, len, len2, buf.data());
      len2 += clen;
    }
  }

  return len2;
}

std::string Separation::movesText() const {
  BT_ASSERT(!states_.empty());
  std::array<char, kTextBuffer> buf{};
  movesTextPieceRemovals(buf.data(), kTextBuffer, sumRotations() > 0, 0, 0);
  return std::string(buf.data());
}

void Separation::exchangeShape(unsigned int s1, unsigned int s2) {
  for (unsigned int& p : pieces_) {
    if (p == s1) {
      p = s2;
    } else if (p == s2) {
      p = s1;
    }
  }

  if (removed_) {
    removed_->exchangeShape(s1, s2);
  }
  if (left_) {
    left_->exchangeShape(s1, s2);
  }
}

unsigned int Separation::getSequenceLength(unsigned int x) const {
  if (x == 0) {
    return static_cast<unsigned int>(states_.size());
  }

  x--;

  if (left_) {
    if (x < left_->numSequences_) {
      return left_->getSequenceLength(x);
    }
    x -= left_->numSequences_;
  }

  if (removed_) {
    if (x < removed_->numSequences_) {
      return removed_->getSequenceLength(x);
    }
  }

  return 0;
}

unsigned int Separation::getNumSequences() const { return numSequences_; }

void Separation::removePieces(unsigned int from, unsigned int cnt) {
  // none of the removed pieces is used here (else the solution was deleted),
  // so only the numbers above them move down
  for (unsigned int& p : pieces_) {
    BT_ASSERT(p < from || p >= from + cnt);
    if (p >= from + cnt) {
      p -= cnt;
    }
  }

  if (left_) {
    left_->removePieces(from, cnt);
  }
  if (removed_) {
    removed_->removePieces(from, cnt);
  }
}

void Separation::addNonPlacedPieces(unsigned int from, unsigned int cnt) {
  for (unsigned int& p : pieces_) {
    if (p >= from) {
      p += cnt;
    }
  }

  if (left_) {
    left_->addNonPlacedPieces(from, cnt);
  }
  if (removed_) {
    removed_->addNonPlacedPieces(from, cnt);
  }
}

// ---- SeparationInfo -----------------------------------------------------------

SeparationInfo::SeparationInfo(std::vector<unsigned int> values,
                               std::vector<unsigned int> rotValues)
    : values_(std::move(values)), rotValues_(std::move(rotValues)) {
  BT_ASSERT(values_.size() == rotValues_.size());
}

/* Pre-order in the order of the animation and movesText: the node, its
 * removed subtree, then its left one. */
void SeparationInfo::recursiveConstruction(const Separation& sep) {
  values_.push_back(sep.getMoves() + 1);
  rotValues_.push_back(sep.getRotations());

  if (sep.getRemoved() != nullptr) {
    recursiveConstruction(*sep.getRemoved());
  } else {
    values_.push_back(0);
    rotValues_.push_back(0);
  }

  if (sep.getLeft() != nullptr) {
    recursiveConstruction(*sep.getLeft());
  } else {
    values_.push_back(0);
    rotValues_.push_back(0);
  }
}

SeparationInfo::SeparationInfo(const Separation& sep) { recursiveConstruction(sep); }

unsigned int SeparationInfo::sumMoves() const {
  unsigned int erg = 0;

  for (std::size_t i = 0; i < values_.size(); i++) {
    if (values_[i] != 0) {
      const unsigned int steps = values_[i] - 1;
      const unsigned int rots = i < rotValues_.size() ? rotValues_[i] : 0;
      if (steps >= rots) {
        erg += steps - rots;
      }
    }
  }

  return erg;
}

unsigned int SeparationInfo::sumRotations() const {
  unsigned int erg = 0;
  for (const unsigned int r : rotValues_) {
    erg += r;
  }
  return erg;
}

std::string SeparationInfo::movesText() const {
  // the original left the buffer unset for an empty tree; here it is ""
  std::array<char, kTextBuffer> buf{};
  movesTextPieceRemovals(buf.data(), kTextBuffer, 0, 0, 0);
  return std::string(buf.data());
}

unsigned int SeparationInfo::skipSubtree(unsigned int idx) const {
  // one child slot: a 0 marker or a whole subtree
  unsigned int branches = 1;
  while (branches != 0 && idx < values_.size()) {
    if (values_[idx] != 0) {
      branches++;
    } else {
      branches--;
    }
    idx++;
  }
  return idx;
}

int SeparationInfo::movesTextPieceRemovals(char* txt, int len, unsigned int idx,
                                           unsigned int carrySlides,
                                           unsigned int carryRots) const {
  if (idx >= values_.size() || values_[idx] == 0) {
    return 0;
  }

  const bool withRots = sumRotations() > 0;
  const unsigned int moves = values_[idx] - 1;
  const unsigned int nodeRots = idx < rotValues_.size() ? rotValues_[idx] : 0;
  const unsigned int nodeSlides = moves >= nodeRots ? moves - nodeRots : 0;

  const unsigned int slides = carrySlides + nodeSlides;
  const unsigned int rots = carryRots + nodeRots;

  const unsigned int remIdx = idx + 1;
  const bool remMulti = remIdx < values_.size() && values_[remIdx] != 0;
  const unsigned int leftIdx = skipSubtree(remIdx);
  const bool leftMulti = leftIdx < values_.size() && values_[leftIdx] != 0;

  if (remMulti && leftMulti) {
    int len2 = movesTextPieceRemovals(txt, len, remIdx, slides, rots);
    if (len2 < 0 || len2 + 5 > len) {
      return len2;
    }

    std::array<char, kTextBuffer> cont{};
    const int clen = movesTextPieceRemovals(cont.data(), kTextBuffer, leftIdx, 0, 0);
    if (clen > 0) {
      if (len2 > 0) {
        appendText(txt, len, len2, ".");
        len2++;
      }
      appendText(txt, len, len2, cont.data());
      len2 += clen;
    }
    return len2;
  }

  int len2 = appendText(txt, len, 0, levelSegment(withRots, slides, rots));

  if (len2 < 0 || len2 + 5 > len) {
    return len2;
  }

  const auto contIdx = remMulti ? remIdx
                       : leftMulti ? leftIdx
                                   : static_cast<unsigned int>(values_.size());
  if (contIdx < values_.size() && values_[contIdx] != 0) {
    std::array<char, kTextBuffer> buf{};
    const int clen = movesTextPieceRemovals(buf.data(), kTextBuffer, contIdx, 0, 0);
    if (clen > 0) {
      appendText(txt, len, len2, ".");
      len2++;
      appendText(txt, len, len2, buf.data());
      len2 += clen;
    }
  }

  return len2;
}

unsigned int SeparationInfo::getSequenceLength(unsigned int x) const {
  return x < values_.size() ? values_[x] : 0;
}

unsigned int SeparationInfo::getNumSequences() const {
  return static_cast<unsigned int>(values_.size());
}

}  // namespace bt
