// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/disassembly.cpp (the data part; XML is in bt/io).

#include "shapelogic/model/disassembly.h"

#include "shapelogic/model/movement_values.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
    // copied by its size, not as a C string:
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
    std::copy_n(s.data(), n, txt + at);
    txt[static_cast<std::size_t>(at) + n] = '\0';
  }
  return static_cast<int>(s.size());
}

std::string levelSegment(bool withRots, unsigned int slides, unsigned int rots) {
  return withRots ? std::to_string(slides) + "R" + std::to_string(rots)
                  : std::to_string(slides + rots);
}

// The level text's buffer: the original's, with its cuts.
constexpr int kTextBuffer = 256;

// The level text of a tree, for both of its forms (the full tree and its
// summary). `Tree` tells the tree's shape by Node values:
//   unsigned int slides(Node), rots(Node): a branch's own steps;
//   bool removed(Node, Node& out), left(Node, Node& out): its side that is
//     a branch of its own (not one piece), when it has one.
//
// Each dotted segment is the step count until a single piece leaves the
// puzzle. A split into two multi-piece groups does not emit a segment; its
// moves are added to the next piece removal in the removed branch (the order
// of the solve animation).
//
// The original's functions called themselves once a branch, each with a
// buffer of kTextBuffer characters in its frame for what the branch inside
// it wrote: a kilobyte of stack a branch in a debug build, and a tree nests
// up to kMaxSeparationDepth, on the stack a solve's or the CLI's worker
// thread gets (cleanup item 18.5). The same steps here, with the branches
// under way in a list and their buffers beside it: what is written, and
// where a long text is cut, is what those functions wrote.
template <class Node, class Tree>
std::string levelText(const Tree& tree, Node root, bool withRots) {
  // A branch under way. It writes at txt (len characters of room): its own
  // buffer for the outermost branch, the buffer of the branch around it, or
  // that branch's own txt (the removed side of a split, which carries the
  // split's steps).
  struct Branch {
    Node node{};
    char* txt = nullptr;
    int len = 0;
    unsigned int slides = 0;  // the steps carried into it, then with its own
    unsigned int rots = 0;
    int stage = 0;
    bool both = false;
    bool hasCont = false;
    Node cont{};
    int len2 = 0;
    char* inner = nullptr;
  };
  // stable where they are: a branch writes into the buffer of one below it
  std::deque<std::array<char, kTextBuffer>> buffers;
  buffers.emplace_back();
  std::vector<Branch> open;
  // most trees nest a few branches: no growing for them
  open.reserve(16);
  open.push_back(Branch{root, buffers.front().data(), kTextBuffer, 0, 0});
  // what the branch that just ended wrote, as its length
  int ended = 0;

  while (!open.empty()) {
    Branch& b = open.back();
    switch (b.stage) {
      case 0: {
        b.slides += tree.slides(b.node);
        b.rots += tree.rots(b.node);
        Node removed{};
        Node left{};
        const bool hasRemoved = tree.removed(b.node, removed);
        const bool hasLeft = tree.left(b.node, left);
        b.both = hasRemoved && hasLeft;
        b.hasCont = hasRemoved || hasLeft;
        b.cont = b.both || !hasRemoved ? left : removed;
        if (b.both) {
          // both sides are groups: carry into removed, then left
          b.stage = 1;
          const Branch inside{removed, b.txt, b.len, b.slides, b.rots};
          open.push_back(inside);
        } else {
          // at least one side is a single piece: one piece-removal segment
          b.len2 = appendText(b.txt, b.len, 0, levelSegment(withRots, b.slides, b.rots));
          b.stage = 2;
        }
        break;
      }
      case 1:
        b.len2 = ended;
        b.stage = 2;
        break;
      case 2:
        if (b.len2 < 0 || b.len2 + 5 > b.len || !b.hasCont) {
          ended = b.len2;
          open.pop_back();
        } else {
          buffers.emplace_back();
          b.inner = buffers.back().data();
          b.stage = 3;
          const Branch inside{b.cont, b.inner, kTextBuffer, 0, 0};
          open.push_back(inside);
        }
        break;
      default:
        // what the branch inside wrote, after a dot (of a split: only when
        // the removed side wrote something)
        if (ended > 0) {
          if (!b.both || b.len2 > 0) {
            appendText(b.txt, b.len, b.len2, ".");
            b.len2++;
          }
          appendText(b.txt, b.len, b.len2, b.inner);
          b.len2 += ended;
        }
        ended = b.len2;
        buffers.pop_back();
        open.pop_back();
        break;
    }
  }
  return std::string(buffers.front().data());
}

// The full tree's shape for levelText: a side is a branch when it is there.
struct SeparationShape {
  static unsigned int slides(const Separation* s) { return s->getSlides(); }
  static unsigned int rots(const Separation* s) { return s->getRotations(); }
  static bool removed(const Separation* s, const Separation*& out) {
    out = s->getRemoved();
    return out != nullptr;
  }
  static bool left(const Separation* s, const Separation*& out) {
    out = s->getLeft();
    return out != nullptr;
  }
};

// The index after one child slot of a summary's pre-order list: a 0 marker
// or a whole subtree.
unsigned int skipSubtree(const std::vector<unsigned int>& values, unsigned int idx) {
  unsigned int branches = 1;
  while (branches != 0 && idx < values.size()) {
    if (values[idx] != 0) {
      branches++;
    } else {
      branches--;
    }
    idx++;
  }
  return idx;
}

// The summary's shape for levelText: a branch is an index of the pre-order
// list with a value, its removed side the next index, its left side the
// one after that side's subtree.
struct SummaryShape {
  const std::vector<unsigned int>& values;
  const std::vector<unsigned int>& rotValues;

  [[nodiscard]] unsigned int rots(unsigned int idx) const {
    return idx < rotValues.size() ? rotValues[idx] : 0;
  }
  [[nodiscard]] unsigned int slides(unsigned int idx) const {
    const unsigned int moves = values[idx] - 1;
    const unsigned int nodeRots = rots(idx);
    return moves >= nodeRots ? moves - nodeRots : 0;
  }
  [[nodiscard]] bool branch(unsigned int idx) const {
    return idx < values.size() && values[idx] != 0;
  }
  bool removed(unsigned int idx, unsigned int& out) const {
    out = idx + 1;
    return branch(out);
  }
  bool left(unsigned int idx, unsigned int& out) const {
    out = skipSubtree(values, idx + 1);
    return branch(out);
  }
};

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
  SL_ASSERT(piece < dx_.size());
  dx_[piece] = x;
  dy_[piece] = y;
  dz_[piece] = z;
}

void State::set(unsigned int piece, int x, int y, int z, unsigned int orient) {
  SL_ASSERT(piece < dx_.size());
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
  SL_ASSERT(i < dx_.size());
  return std::abs(dx_[i]) > movement::kRemovedBeyond || std::abs(dy_[i]) > movement::kRemovedBeyond ||
         std::abs(dz_[i]) > movement::kRemovedBeyond;
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

// The branches go one after the other, not each inside the destructor of
// the one around it: a tree nests up to kMaxSeparationDepth, and a solve
// lets its trees go on its own threads. They are kept as one list through
// left_ (a branch's left side is the rest of the list already); a removed
// side goes in front of it, its own left sides first. Nothing is allocated,
// and each branch's left sides are walked once.
Separation::~Separation() {
  std::unique_ptr<Separation> list = left_.release();
  std::unique_ptr<Separation> side = removed_.release();
  for (;;) {
    if (side) {
      Separation* last = side.get();
      while (last->left_) {
        last = last->left_.get();
      }
      last->left_.reset(std::move(list));
      list = std::move(side);
    }
    if (!list) {
      break;
    }
    const std::unique_ptr<Separation> head = std::move(list);
    list = head->left_.release();
    side = head->removed_.release();
    // head goes here, with no branch left for its own destructor
  }
}

// Calls f for every branch of the tree, this one first. The smaller of a
// branch's two is gone through by a call, the larger in the loop, so the
// calls nest at most log2 of the branches deep however deep the tree is
// (kMaxSeparationDepth; a solve sorts by these counts on its threads).
template <class F>
void Separation::eachBranch(const F& f) const {
  for (const Separation* s = this; s != nullptr;) {
    f(*s);
    const Separation* small = s->removed_.get();
    const Separation* large = s->left_.get();
    if (small != nullptr && large != nullptr) {
      if (small->numSequences_ > large->numSequences_) {
        std::swap(small, large);
      }
      small->eachBranch(f);
      s = large;
    } else {
      s = small != nullptr ? small : large;
    }
  }
}

unsigned int Separation::sumMoves() const {
  unsigned int erg = 0;
  eachBranch([&erg](const Separation& s) {
    SL_ASSERT(!s.states_.empty());
    erg += s.getSlides();
  });
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
  unsigned int erg = 0;
  eachBranch([&erg](const Separation& s) { erg += s.getRotations(); });
  return erg;
}

std::size_t Separation::byteEstimate() const {
  std::size_t bytes = 0;
  eachBranch([&bytes](const Separation& s) {
    bytes += sizeof(Separation) + s.pieces_.size() * sizeof(unsigned int);
    for (const State& st : s.states_) {
      // x, y, z and, where the state has them, the orientations
      bytes += sizeof(State) +
               (st.hasOrientations() ? 4 : 3) * std::size_t{st.getPiecenumber()} * sizeof(int);
    }
  });
  return bytes;
}

void Separation::addstate(State st) {
  SL_ASSERT(st.getPiecenumber() == pieces_.size());
  states_.push_front(std::move(st));
}

void Separation::appendState(State st) {
  SL_ASSERT(st.getPiecenumber() == pieces_.size());
  states_.push_back(std::move(st));
}

std::string Separation::movesText() const {
  SL_ASSERT(!states_.empty());
  return levelText(SeparationShape{}, this, sumRotations() > 0);
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
  // down one way through the tree: a loop, however deep it is
  for (const Separation* s = this;;) {
    if (x == 0) {
      return static_cast<unsigned int>(s->states_.size());
    }

    x--;

    if (s->left_) {
      if (x < s->left_->numSequences_) {
        s = s->left_.get();
        continue;
      }
      x -= s->left_->numSequences_;
    }

    if (s->removed_ && x < s->removed_->numSequences_) {
      s = s->removed_.get();
      continue;
    }

    return 0;
  }
}

unsigned int Separation::getNumSequences() const { return numSequences_; }

void Separation::removePieces(unsigned int from, unsigned int cnt) {
  // none of the removed pieces is used here (else the solution was deleted),
  // so only the numbers above them move down
  for (unsigned int& p : pieces_) {
    SL_ASSERT(p < from || p >= from + cnt);
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
  SL_ASSERT(values_.size() == rotValues_.size());
}

/* Pre-order in the order of the animation and movesText: the node, its
 * removed subtree, then its left one. The branches still to write wait in
 * a list (a missing one as null), the removed one on top: no call a branch,
 * as a solve's thread makes the summary of a tree it made. */
SeparationInfo::SeparationInfo(const Separation& sep) {
  std::vector<const Separation*> open{&sep};
  while (!open.empty()) {
    const Separation* s = open.back();
    open.pop_back();
    if (s == nullptr) {
      values_.push_back(0);
      rotValues_.push_back(0);
      continue;
    }
    values_.push_back(s->getMoves() + 1);
    rotValues_.push_back(s->getRotations());
    open.push_back(s->getLeft());
    open.push_back(s->getRemoved());
  }
}

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
  const SummaryShape shape{values_, rotValues_};
  if (!shape.branch(0)) {
    return {};
  }
  // (whether any node turns is asked once, not at every node: a file's
  // summary can have a million of them)
  return levelText(shape, 0U, sumRotations() > 0);
}

unsigned int SeparationInfo::getSequenceLength(unsigned int x) const {
  return x < values_.size() ? values_[x] : 0;
}

unsigned int SeparationInfo::getNumSequences() const {
  return static_cast<unsigned int>(values_.size());
}

}  // namespace bt
