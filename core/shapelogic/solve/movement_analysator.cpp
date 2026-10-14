// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/movementanalysator.cpp. SHAPELOGIC_NO_DISASM_FAST is
// Options::fast.
#include "shapelogic/solve/movement_analysator.h"

#include "shapelogic/base/assert.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/solve/movement_cache.h"
#include "shapelogic/solve/rotation_moves_crowell.h"

#include <algorithm>
#include <bit>
#include <chrono>

namespace bt {

namespace {

std::uint64_t nowUs() {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(
      duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

}  // namespace

MovementAnalysator::MovementAnalysator(const Puzzle& puzzle, const Problem& problem,
                                       bool rotations, SolverType solver, Options options)
    : cache_(makeMovementCache(puzzle, problem)),
      options_(options),
      piecenumber_(problem.getNumberOfPieces()) {
  // only a grid with a take-apart gets here
  BT_ASSERT(cache_);

  matrix_.assign(std::size_t{cache_->numDirections()} * piecenumber_ * piecenumber_, 0);
  movement_.assign(piecenumber_, 0);
  weights_.assign(piecenumber_, 0);
  check_.assign(piecenumber_, 0);
  prevFill_.assign(matrix_.size(), 0);

  bricksGrid_ = puzzle.getGridType().getType() == GridKind::Bricks;

  // the weight of each piece
  std::size_t pc = 0;
  for (unsigned int i = 0; i < problem.getNumberOfParts(); i++) {
    for (unsigned int j = 0; j < problem.getPartMaximum(i); j++) {
      weights_[pc++] = problem.partShape(puzzle, i).getWeight();
    }
  }

  if (bricksGrid_) {
    if (solver == SolverType::Crowell) {
      rotationMoves_ = std::make_unique<RotationMovesCrowell>(puzzle, *cache_);
    } else {
      // Classic uses the complete generator
      rotationMoves_ = std::make_unique<RotationMovesClassic>(puzzle, *cache_, options.rotations);
    }
  }

  setCheckRotations(rotations);
}

MovementAnalysator::~MovementAnalysator() = default;

void MovementAnalysator::setCheckRotations(bool enable) {
  checkRotations_ = enable && bricksGrid_ && rotationMoves_;
}

void MovementAnalysator::setStop(const StopToken& stop) {
  stop_ = stop;
  if (rotationMoves_) {
    rotationMoves_->setStop(stop);
  }
}

void MovementAnalysator::prepare() {
  if (!options_.fast) {
    prepareFill();
    closureFull();
    return;
  }

  const auto n = static_cast<unsigned int>(pieces_->size());
  const unsigned int dirs = cache_->numDirections();

  // What two pieces allow each other depends only on how they lie relative
  // to each other and how each is turned. A breadth-first search hands over
  // positions that differ from the one before in a few pieces, so most
  // pairs lie as they did: keep the last position's values for those and
  // ask the cache for the rest. prevFill_ holds the values before closing,
  // for the pieces in prevIds_.
  const bool reuse = prevIds_ == *pieces_ && prevPlace_.size() == std::size_t{4} * n;

  place_.resize(std::size_t{4} * n);
  for (unsigned int i = 0; i < n; i++) {
    place_[4 * std::size_t{i} + 0] = searchnode_->getX(i);
    place_[4 * std::size_t{i} + 1] = searchnode_->getY(i);
    place_[4 * std::size_t{i} + 2] = searchnode_->getZ(i);
    place_[4 * std::size_t{i} + 3] = static_cast<int>(searchnode_->getTrans(i));
  }

  for (std::size_t j = 0; j < n; j++) {
    for (std::size_t i = 0; i < n; i++) {
      if (i == j) {
        continue;
      }
      const int dx = place_[4 * j + 0] - place_[4 * i + 0];
      const int dy = place_[4 * j + 1] - place_[4 * i + 1];
      const int dz = place_[4 * j + 2] - place_[4 * i + 2];
      if (reuse && dx == prevPlace_[4 * j + 0] - prevPlace_[4 * i + 0] &&
          dy == prevPlace_[4 * j + 1] - prevPlace_[4 * i + 1] &&
          dz == prevPlace_[4 * j + 2] - prevPlace_[4 * i + 2] &&
          place_[4 * i + 3] == prevPlace_[4 * i + 3] &&
          place_[4 * j + 3] == prevPlace_[4 * j + 3]) {
        continue;
      }
      cache_->getMoValue(dx, dy, dz, static_cast<unsigned char>(place_[4 * i + 3]),
                         static_cast<unsigned char>(place_[4 * j + 3]), (*pieces_)[i],
                         (*pieces_)[j], &prevFill_[(i + j * piecenumber_) * dirs]);
    }
  }

  prevPlace_.swap(place_);
  if (!reuse) {
    prevIds_ = *pieces_;
  }

  // the masks checkmovementMasks kept were the last position's
  std::fill(pushValid_.begin(), pushValid_.end(), 0);

  closeFrom(prevFill_);
}

// The second part of Bill Cutler's algorithm on a copy of `fill`, left in
// matrix_: how far a piece can go against another when the pieces between
// may be pushed along. That is the shortest path between every two pieces,
// which one Floyd-Warshall pass gives exactly; closureFull's sweeps until
// nothing changes reach the same values in two or more passes.
void MovementAnalysator::closeFrom(const std::vector<unsigned int>& fill) {
  const std::size_t n = pieces_->size();
  const unsigned int dirs = cache_->numDirections();

  dense_.resize(n * n);

  for (unsigned int d = 0; d < dirs; d++) {
    // dense_[y * n + x]: piece x against piece y, as matrix_ holds it
    for (std::size_t y = 0; y < n; y++) {
      const unsigned int* row = &fill[(y * piecenumber_) * dirs + d];
      for (std::size_t x = 0; x < n; x++) {
        dense_[y * n + x] = row[x * dirs];
      }
    }

    for (std::size_t k = 0; k < n; k++) {
      // row k does not change in round k (its own entry is 0), so the other
      // rows can read it while they are written
      const unsigned int* __restrict via = &dense_[k * n];
      for (std::size_t y = 0; y < n; y++) {
        if (y == k) {
          continue;
        }
        unsigned int* __restrict row = &dense_[y * n];
        const unsigned int first = row[k];
        for (std::size_t x = 0; x < n; x++) {
          row[x] = std::min(row[x], first + via[x]);
        }
      }
    }

    for (std::size_t y = 0; y < n; y++) {
      unsigned int* row = &matrix_[(y * piecenumber_) * dirs + d];
      for (std::size_t x = 0; x < n; x++) {
        row[x * dirs] = dense_[y * n + x];
      }
    }
  }
}

void MovementAnalysator::prepareFill() {
  unsigned int* idx = matrix_.data();

  const std::size_t idxCol = cache_->numDirections();
  const std::size_t idxRow = std::size_t{cache_->numDirections()} * (piecenumber_ - pieces_->size());

  for (unsigned int j = 0; j < pieces_->size(); j++) {
    for (unsigned int i = 0; i < pieces_->size(); i++) {
      if (i != j) {
        cache_->getMoValue(searchnode_->getX(j) - searchnode_->getX(i),
                           searchnode_->getY(j) - searchnode_->getY(i),
                           searchnode_->getZ(j) - searchnode_->getZ(i),
                           static_cast<unsigned char>(searchnode_->getTrans(i)),
                           static_cast<unsigned char>(searchnode_->getTrans(j)), (*pieces_)[i],
                           (*pieces_)[j], idx);
      }

      // the diagonal is 0 from the start and stays so
      idx += idxCol;
    }
    idx += idxRow;
  }
}

// The second part of Bill Cutler's algorithm, as the original did it: sweep
// until nothing changes, and sweep again only when a change made calls for
// another (the first sweep does most of the work, the second little).
void MovementAnalysator::closureFull() {
  const unsigned int dirs = cache_->numDirections();
  const unsigned int rowStep = dirs * piecenumber_;
  const auto size = static_cast<unsigned int>(dirs * pieces_->size());

  bool again = false;

  for (unsigned int d = 0; d < dirs; d++) {
    do {
      again = false;

      unsigned int* pos1 = matrix_.data() + d;  // y * piecenumber

      for (unsigned int y = 0; y < size; y += dirs) {
        unsigned int* pos2 = matrix_.data() + d;  // x

        for (unsigned int x = 0; x < size; x += dirs) {
          unsigned int min = *pos2 + *pos1;

          unsigned int i = dirs;
          unsigned int idx = rowStep;
          for (; i < size; i += dirs, idx += rowStep) {
            const unsigned int l = pos2[idx] + pos1[i];
            min = std::min(l, min);
          }

          if (min < pos1[x]) {
            pos1[x] = min;

            if (!again) {
              const unsigned int* pos3 = matrix_.data() + d;

              for (i = 0; i < y; i += dirs) {
                if (min + pos3[y] < pos3[x]) {
                  again = true;
                  break;
                }
                pos3 += rowStep;
              }

              if (!again) {
                pos3 = matrix_.data() + d + std::size_t{piecenumber_} * x;

                for (i = 0; i < x; i += dirs) {
                  if (pos3[i] + min < pos1[i]) {
                    again = true;
                    break;
                  }
                }
              }
            }
          }
          pos2 += dirs;
        }

        pos1 += rowStep;
      }
    } while (again);
  }
}

// To move piece nextpiece_ `step` units in direction nextdir_, the pieces
// in its way that can move must move too: this finds them (movement_).
// When no move that way is possible, every piece ends up moving: the whole
// puzzle. True when at most maxPieces move (more than half the pieces is
// not a useful move).
bool MovementAnalysator::checkmovement(unsigned int maxPieces, unsigned int step) {
  stats_.checkCalls++;

  if (options_.fast && nextPn_ <= 64) {
    return checkmovementMasks(maxPieces, step);
  }

  // the pieces moving so far; more than maxPieces and the move is useless
  unsigned int movedPieces = 1;

  // nextpiece_ moves `step` units, all others stand
  for (std::size_t i = 0; i < static_cast<std::size_t>(nextPn_); i++) {
    movement_[i] = 0;
    check_[i] = 0;
  }
  movement_[static_cast<std::size_t>(nextpiece_)] = step;
  check_[static_cast<std::size_t>(nextpiece_)] = 1;

  bool finished = false;
  const unsigned int nd = nextdir_ >> 1;
  const unsigned int dirs = cache_->numDirections();
  BT_ASSERT(nd < dirs);
  const std::size_t rowIdx = std::size_t{piecenumber_ - static_cast<unsigned int>(nextPn_)} * dirs;
  const std::size_t rowIdx2 = std::size_t{piecenumber_} * dirs;
  const std::size_t rowIdx3 =
      (std::size_t{piecenumber_} * static_cast<unsigned int>(nextPn_) - 1) * dirs;

  // A piece that moves further than the matrix allows against another
  // takes that one along, and that one is checked in turn. The second half
  // is the same for the other sign of the direction (the transposed
  // matrix).
  if ((nextdir_ & 1) != 0) {
    do {
      finished = true;
      const unsigned int* idx = matrix_.data() + nd;

      for (int i = 0; i < nextPn_; i++) {
        if (check_[static_cast<std::size_t>(i)] != 0) {
          // against every piece that stands so far (one that moves goes as
          // far as this one)
          for (int j = 0; j < nextPn_; j++) {
            if (i != j && movement_[static_cast<std::size_t>(j)] == 0) {
              // idx is matrix_[(j + piecenumber * i) * dirs + nd]
              if (movement_[static_cast<std::size_t>(i)] > *idx) {
                movedPieces++;
                if (movedPieces > maxPieces) {
                  return false;
                }

                movement_[static_cast<std::size_t>(j)] = step;
                check_[static_cast<std::size_t>(j)] = 1;
                finished = false;
              }
            }
            idx += dirs;
          }
          check_[static_cast<std::size_t>(i)] = 0;
          idx += rowIdx;
        } else {
          idx += rowIdx2;
        }
      }
    } while (!finished);

  } else {
    do {
      finished = true;
      const unsigned int* idx = matrix_.data() + nd;

      for (int i = 0; i < nextPn_; i++) {
        if (check_[static_cast<std::size_t>(i)] != 0) {
          for (int j = 0; j < nextPn_; j++) {
            if (i != j && movement_[static_cast<std::size_t>(j)] == 0) {
              // idx is matrix_[(i + piecenumber * j) * dirs + nd]
              if (movement_[static_cast<std::size_t>(i)] > *idx) {
                movedPieces++;
                if (movedPieces > maxPieces) {
                  return false;
                }

                movement_[static_cast<std::size_t>(j)] = step;
                check_[static_cast<std::size_t>(j)] = 1;
                finished = false;
              }
            }
            idx += rowIdx2;
          }
          check_[static_cast<std::size_t>(i)] = 0;
          idx -= rowIdx3;
        } else {
          idx += dirs;
        }
      }

    } while (!finished);
  }

  stats_.checkSuccess++;
  return true;
}

// checkmovement on bit masks, for up to 64 pieces. For one direction and
// step, pushes[i] is the set of pieces piece i takes along. They are the
// same for every piece asked about at this position, so they are worked
// out once and kept; the pieces that must move with nextpiece_ are what its
// mask reaches. The same answer and the same movement_ as the loops.
bool MovementAnalysator::checkmovementMasks(unsigned int maxPieces, unsigned int step) {
  const auto n = static_cast<unsigned int>(nextPn_);
  const unsigned int dirs = cache_->numDirections();
  const unsigned int nd = nextdir_ >> 1;
  BT_ASSERT(nd < dirs);

  // slot 0 is a removal, slots 1 .. kPushSlots - 1 the steps of that length
  const unsigned int slot = step < kPushSlots ? step : (step == 30000 ? 0 : kPushSlots);
  std::uint64_t* pushes = nullptr;
  bool fresh = true;
  if (slot < kPushSlots) {
    const std::size_t entry = std::size_t{nextdir_} * kPushSlots + slot;
    if (pushValid_.size() < std::size_t{2} * dirs * kPushSlots) {
      pushValid_.assign(std::size_t{2} * dirs * kPushSlots, 0);
      pushMasks_.assign(std::size_t{2} * dirs * kPushSlots * 64, 0);
    }
    pushes = &pushMasks_[entry * 64];
    fresh = pushValid_[entry] == 0;
    pushValid_[entry] = 1;
  } else {
    pushTemp_.resize(64);
    pushes = pushTemp_.data();
  }

  if (fresh) {
    for (unsigned int i = 0; i < n; i++) {
      std::uint64_t m = 0;
      // how far piece i can go against piece j, as checkmovement reads it
      const unsigned int* v = (nextdir_ & 1) != 0
                                  ? &matrix_[(std::size_t{piecenumber_} * i) * dirs + nd]
                                  : &matrix_[std::size_t{i} * dirs + nd];
      const std::size_t stride = (nextdir_ & 1) != 0 ? dirs : std::size_t{piecenumber_} * dirs;
      for (unsigned int j = 0; j < n; j++) {
        if (j != i && step > v[j * stride]) {
          m |= std::uint64_t{1} << j;
        }
      }
      pushes[i] = m;
    }
  }

  std::uint64_t moving = std::uint64_t{1} << static_cast<unsigned int>(nextpiece_);
  std::uint64_t todo = moving;
  while (todo != 0) {
    const auto i = static_cast<unsigned int>(std::countr_zero(todo));
    todo &= todo - 1;
    const std::uint64_t more = pushes[i] & ~moving;
    if (more != 0) {
      moving |= more;
      todo |= more;
      if (static_cast<unsigned int>(std::popcount(moving)) > maxPieces) {
        return false;
      }
    }
  }

  for (unsigned int i = 0; i < n; i++) {
    movement_[i] = ((moving >> i) & 1) != 0 ? step : 0;
  }

  stats_.checkSuccess++;
  return true;
}

// The position with the pieces of movement_ moved `amount` in direction
// nextdir_ from searchnode_. When the moving pieces are heavier than the
// standing ones, the others move the other way instead.
NodeRef MovementAnalysator::newNode(unsigned int amount) {
  int moveWeight = 0;
  int stilWeight = 0;
  unsigned int nd = nextdir_;

  for (std::size_t i = 0; i < pieces_->size(); i++) {
    if (movement_[i] != 0) {
      BT_ASSERT(amount == movement_[i]);
      moveWeight = std::max(moveWeight, weights_[(*pieces_)[i]]);
    } else {
      stilWeight = std::max(stilWeight, weights_[(*pieces_)[i]]);
    }
  }

  if (stilWeight < moveWeight) {
    for (std::size_t i = 0; i < pieces_->size(); i++) {
      movement_[i] = movement_[i] != 0 ? 0 : amount;
    }
    nd ^= 1;
  }

  const auto pn = static_cast<unsigned int>(pieces_->size());
  NodeRef n = NodeRef::make(pn, NodeRef::share(searchnode_), static_cast<int>(nd),
                            static_cast<int>(amount));

  for (unsigned int i = 0; i < pn; i++) {
    if (movement_[i] != 0) {
      int mx = 0;
      int my = 0;
      int mz = 0;
      cache_->getDirection(nd >> 1, &mx, &my, &mz);

      if (movement_[i] >= 10000) {
        if ((nd & 1) != 0) {
          mx = -mx;
          my = -my;
          mz = -mz;
        }
        n->setRemove(i, mx, my, mz);
      } else {
        const auto m = static_cast<int>(movement_[i]);
        mx *= m;
        my *= m;
        mz *= m;
        if ((nd & 1) != 0) {
          mx = -mx;
          my = -my;
          mz = -mz;
        }
        n->set(i, mx, my, mz);
      }
    } else {
      n->set(i, 0, 0, 0);
    }
  }

  return n;
}

// The position with the moves of n0 and n1 at once: a piece moves as far as
// it does in either. Null when the two do not go the same way the same
// distance, or when the result would be one of them (or move nothing, or
// everything).
NodeRef MovementAnalysator::newNodeMerge(const DisassemblerNode* n0, const DisassemblerNode* n1) {
  // both along the same axis
  BT_ASSERT((nextdir_ | 1) == (n0->getDirection() | 1));
  BT_ASSERT((nextdir_ | 1) == (n1->getDirection() | 1));

  const bool invert0 = nextdir_ != n0->getDirection();
  const bool invert1 = nextdir_ != n1->getDirection();

  const int amount = n0->getAmount();
  if (amount != n1->getAmount()) {
    return nullptr;
  }

  bool different0 = false;
  bool different1 = false;
  int moved = 0;

  for (unsigned int i = 0; i < static_cast<unsigned int>(nextPn_); i++) {
    // moved in either node (read against the direction it went)
    const bool move0 = ((n0->getX(i) != searchnode_->getX(i)) ||
                        (n0->getY(i) != searchnode_->getY(i)) ||
                        (n0->getZ(i) != searchnode_->getZ(i))) != invert0;
    const bool move1 = ((n1->getX(i) != searchnode_->getX(i)) ||
                        (n1->getY(i) != searchnode_->getY(i)) ||
                        (n1->getZ(i) != searchnode_->getZ(i))) != invert1;

    if (move0 || move1) {
      movement_[i] = static_cast<unsigned int>(amount);
      moved++;
    } else {
      movement_[i] = 0;
    }

    different0 |= (move1 && !move0);
    different1 |= (move0 && !move1);
  }

  if (moved == 0 || moved == nextPn_) {
    return nullptr;
  }

  if (!different0 || !different1) {
    return nullptr;
  }

  return newNode(static_cast<unsigned int>(amount));
}

void MovementAnalysator::flushSearchPhase() {
  if (!searchTimingOpen_) {
    return;
  }
  const std::uint64_t dt = nowUs() - searchPhaseStartUs_;
  if (searchPhaseLinear_) {
    linearSearchUs_.fetch_add(dt, std::memory_order_relaxed);
  } else {
    rotationSearchUs_.fetch_add(dt, std::memory_order_relaxed);
  }
  searchTimingOpen_ = false;
}

void MovementAnalysator::beginSearchPhase(bool linear) {
  flushSearchPhase();
  searchPhaseLinear_ = linear;
  searchPhaseStartUs_ = nowUs();
  searchTimingOpen_ = true;
}

void MovementAnalysator::switchToRotationPhase() {
  if (!searchTimingOpen_ || !searchPhaseLinear_) {
    return;
  }
  flushSearchPhase();
  searchPhaseLinear_ = false;
  searchPhaseStartUs_ = nowUs();
  searchTimingOpen_ = true;
}

void MovementAnalysator::initFind(DisassemblerNode* nd, const std::vector<unsigned int>& pieces) {
  beginSearchPhase(true);

  nextdir_ = 0;
  nextpiece_ = 0;
  nextstep_ = 1;
  nextstate_ = 0;
  nextPn_ = static_cast<int>(pieces.size());

  searchnode_ = nd;
  pieces_ = &pieces;

  // The movement matrices: one per direction, holding how far each piece
  // can move with each other piece fixed (Bill Cutler's booklet "Computer
  // Analysis of All 6 Piece Burrs").
  prepare();

  if (checkRotations_ && rotationMoves_) {
    rotationMoves_->initFind(nd, pieces);
  }
}

// In each direction: first whether a single piece can be removed, then a
// group, then the slides, longer and longer; then the turns.
NodeRef MovementAnalysator::find() {
  NodeRef n;

  const unsigned int dirs2 = 2 * cache_->numDirections();

  while (!n) {
    if (stop_.stopRequested()) {
      return nullptr;
    }

    switch (nextstate_) {
      case 0:
        // a single piece removed
        if (checkmovement(1, 30000)) {
          n = newNode(30000);
        }

        nextpiece_++;
        if (nextpiece_ >= nextPn_) {
          nextpiece_ = 0;
          nextdir_++;
          if (nextdir_ >= dirs2) {
            nextstate_++;
            nextdir_ = 0;
          }
        }
        break;

      case 1:
        // a group of pieces removed
        if (checkmovement(static_cast<unsigned int>(nextPn_ / 2), 30000)) {
          n = newNode(30000);
        }

        nextpiece_++;
        if (nextpiece_ >= nextPn_) {
          nextpiece_ = 0;
          nextdir_++;
          if (nextdir_ >= dirs2) {
            nextstate_++;
            nextdir_ = 0;
            nodes_.clear();
          }
        }
        break;

      case 2:
        // pieces slid
        if (nextstep_ <= maxstep_ &&
            checkmovement(static_cast<unsigned int>(nextPn_ / 2), nextstep_)) {
          n = newNode(nextstep_);
          BT_ASSERT(n);

          // Merge it with the positions found before in this direction with
          // the same step, which may give more (below, state 99). A position
          // found already (maybe through a merger) is dropped.
          if (nodes_.insert(n)) {
            n.reset();
          } else {
            nextstate_ = 99;
            state99node_ = n.get();
            nodes_.initScan();
            state99nextState_ = 2;
          }

          // something moved: try a longer step
          nextstep_++;

        } else {
          // the next piece
          nextstep_ = 1;
          nextpiece_++;
          if (nextpiece_ >= nextPn_) {
            nextpiece_ = 0;
            nextdir_++;
            nodes_.clear();
            if (nextdir_ >= dirs2) {
              nextstate_++;
            }
          }
        }
        break;

      case 99: {
        // The last position found merged with the ones found before: both
        // moves at once, where they go the same way the same distance.
        // Without this, independent groups moving together would only be
        // found one after the other.
        const DisassemblerNode* nd2 = nodes_.nextScan();

        if (nd2 != nullptr) {
          n = newNodeMerge(state99node_, nd2);

          // a position known already is dropped
          if (n && nodes_.insert(n)) {
            n.reset();
          }

        } else {
          nextstate_ = state99nextState_;
        }
        break;
      }

      case 3:
        // 90 degree turns (cube grid, when asked for)
        if (checkRotations_ && rotationMoves_) {
          switchToRotationPhase();
          n = rotationMoves_->find();
          if (!n) {
            nextstate_++;
          }
        } else {
          nextstate_++;
        }
        break;

      default:
        flushSearchPhase();
        return nullptr;
    }
  }

  stats_.nodesReturned++;
  return n;
}

void MovementAnalysator::endFind() {
  nodes_.clear();
  flushSearchPhase();
  nextstate_ = -1;
}

// A cut-down find(): the first move of one step that moves `piece` that
// way. The preparation is done here too.
NodeRef MovementAnalysator::findMatching(DisassemblerNode* nd, const std::vector<unsigned int>& pcs,
                                         unsigned int piece, int dx, int dy, int dz) {
  // the direction of (dx, dy, dz)
  {
    const unsigned int numDirs = cache_->numDirections();
    unsigned int dirIdx = 0;
    for (; dirIdx < numDirs; dirIdx++) {
      int dx0 = 0;
      int dy0 = 0;
      int dz0 = 0;
      cache_->getDirection(dirIdx, &dx0, &dy0, &dz0);
      if (dx0 == dx && dy0 == dy && dz0 == dz) {
        nextdir_ = dirIdx << 1;
        break;
      }
      if (dx0 == -dx && dy0 == -dy && dz0 == -dz) {
        nextdir_ = (dirIdx << 1) | 1;
        break;
      }
    }

    // no such direction (possible on other grids)
    if (dirIdx >= numDirs) {
      return nullptr;
    }
  }

  // the piece's index among pcs
  {
    unsigned int i = 0;
    while (i < pcs.size() && pcs[i] != piece) {
      i++;
    }
    BT_ASSERT(i < pcs.size());
    nextpiece_ = static_cast<int>(i);
  }

  nextstep_ = 1;
  nextstate_ = 2;
  nextPn_ = static_cast<int>(pcs.size());

  searchnode_ = nd;
  pieces_ = &pcs;

  prepare();

  if (checkmovement(static_cast<unsigned int>(nextPn_ / 2), nextstep_)) {
    NodeRef n = newNode(nextstep_);
    BT_ASSERT(n);
    return n;
  }

  return nullptr;
}

void MovementAnalysator::completeFind(DisassemblerNode* searchnode,
                                      const std::vector<unsigned int>& pieces,
                                      std::vector<NodeRef>& result) {
  initFind(searchnode, pieces);

  result.clear();

  maxstep_ = 1;

  while (NodeRef nd = find()) {
    const bool known = std::any_of(result.begin(), result.end(),
                                   [&nd](const NodeRef& r) { return *r == *nd; });
    if (!known) {
      result.push_back(std::move(nd));
    }
  }

  maxstep_ = static_cast<unsigned int>(-1);
}

}  // namespace bt
