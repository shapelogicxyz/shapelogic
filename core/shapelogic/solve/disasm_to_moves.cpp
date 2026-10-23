// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/disasmtomoves.cpp.
#include "shapelogic/solve/disasm_to_moves.h"

#include "shapelogic/base/assert.h"
#include "shapelogic/solve/disassembler_node.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace bt {

namespace {

// A piece's orientation in a state, or "not given" for a state without
// orientations (a file without <dt>: every classic solution read back).
// The original answered 0 there, which turned every piece of such a
// solution to orientation 0 in its view and its glTF export instead of
// leaving it as the assembly placed it (a deliberate difference: decision
// 0013, item 7).
unsigned int orientOf(const State& s, unsigned int i) {
  return s.hasOrientations() ? s.getOrient(i) : static_cast<unsigned int>(-1);
}

}  // namespace

DisasmToMoves::DisasmToMoves(const Separation* tree, unsigned int size, unsigned int maxPiece)
    : size_(size),
      moves_(std::size_t{maxPiece} * 4, 0.0F),
      orients_(maxPiece, 0),
      rotAngle_(maxPiece, 0.0F),
      rotAxisX_(maxPiece, 0.0F),
      rotAxisY_(maxPiece, 0.0F),
      rotAxisZ_(maxPiece, 0.0F),
      rotPivotX_(maxPiece, 0.0F),
      rotPivotY_(maxPiece, 0.0F),
      rotPivotZ_(maxPiece, 0.0F),
      mv_(maxPiece, false),
      maxPieceName_(maxPiece) {
  if (tree != nullptr) {
    tree_ = *tree;
  }
}

void DisasmToMoves::setStep(float step, bool fadeOut, bool centerActive) {
  const auto s = static_cast<int>(step);
  const float frac = step - static_cast<float>(s);

  // the next whole step, to interpolate to
  std::vector<float> moves2(std::size_t{maxPieceName_} * 4, 0.0F);
  std::vector<unsigned int> orients2(maxPieceName_, 0);
  std::fill(moves_.begin(), moves_.end(), 0.0F);

  for (unsigned int i = 0; i < maxPieceName_; i++) {
    orients_[i] = 0;
    rotAngle_[i] = 0;
    rotAxisX_[i] = rotAxisY_[i] = rotAxisZ_[i] = 0;
    rotPivotX_[i] = rotPivotY_[i] = rotPivotZ_[i] = 0;
  }

  if (!tree_) {
    return;
  }

  doRecursive(&*tree_, s, moves_.data(), orients_.data(), centerActive, 0, 0, 0);
  doRecursive(&*tree_, s + 1, moves2.data(), orients2.data(), centerActive, 0, 0, 0);

  for (unsigned int i = 0; i < maxPieceName_; i++) {
    const std::size_t b = 4 * std::size_t{i};
    const bool rotating = orients_[i] != orients2[i];
    mv_[i] = moves_[b + 0] != moves2[b + 0] || moves_[b + 1] != moves2[b + 1] ||
             moves_[b + 2] != moves2[b + 2] || rotating;

    if (rotating) {
      // Keep the start position and shape and turn it about the recorded
      // pivot; at frac 1 the next whole step has the end state.
      float angle = frac * 90.0F;
      unsigned int axis = 0;
      unsigned int sense = 0;
      int pvx = 0;
      int pvy = 0;
      int pvz = 0;
      const bool havePivot = findRotationArrival(s + 1, i, &pvx, &pvy, &pvz, &axis, &sense);

      if (havePivot) {
        if (sense != 0) {
          angle = -angle;
        }
        rotAngle_[i] = angle;
        rotAxisX_[i] = (axis == 0) ? 1.0F : 0.0F;
        rotAxisY_[i] = (axis == 1) ? 1.0F : 0.0F;
        rotAxisZ_[i] = (axis == 2) ? 1.0F : 0.0F;
        rotPivotX_[i] = static_cast<float>(pvx) * 0.5F + 0.5F;
        rotPivotY_[i] = static_cast<float>(pvy) * 0.5F + 0.5F;
        rotPivotZ_[i] = static_cast<float>(pvz) * 0.5F + 0.5F;
      } else if (frac >= 0.5F) {
        // no pivot known: jump half way
        for (std::size_t k = 0; k < 4; k++) {
          moves_[b + k] = moves2[b + k];
        }
        orients_[i] = orients2[i];
      }
    } else if (routeAt(s, i, frac, &moves_[b + 0])) {
      moves_[b + 3] = (1 - frac) * moves_[b + 3] + frac * moves2[b + 3];
    } else {
      for (std::size_t k = 0; k < 4; k++) {
        moves_[b + k] = (1 - frac) * moves_[b + k] + frac * moves2[b + k];
      }
    }
  }

  if (!fadeOut) {
    for (unsigned int i = 0; i < maxPieceName_; i++) {
      if (moves_[4 * std::size_t{i} + 3] > 0) {
        moves_[4 * std::size_t{i} + 3] = 1;
      }
    }
  }
}

void DisasmToMoves::setRoute(unsigned int step, const std::vector<unsigned int>& pieces,
                             const std::vector<std::array<float, 3>>& offsets) {
  if (offsets.size() < 2 || pieces.empty()) {
    routes_.erase(step);
  } else {
    routes_[step] = Route{pieces, offsets};
  }
}

bool DisasmToMoves::routeAt(int step, unsigned int piece, float frac, float* xyz) const {
  if (step < 0) {
    return false;
  }
  auto it = routes_.find(static_cast<unsigned int>(step));
  if (it == routes_.end()) {
    return false;
  }
  const std::vector<unsigned int>& on = it->second.pieces;
  if (std::find(on.begin(), on.end(), piece) == on.end()) {
    return false;
  }
  const std::vector<std::array<float, 3>>& o = it->second.offsets;
  auto length = [&o](std::size_t k) {
    return std::fabs(o[k][0] - o[k - 1][0]) + std::fabs(o[k][1] - o[k - 1][1]) +
           std::fabs(o[k][2] - o[k - 1][2]);
  };
  // at the same speed along the bends: frac by length travelled
  float total = 0;
  for (std::size_t k = 1; k < o.size(); k++) {
    total += length(k);
  }
  float want = frac * total;
  std::array<float, 3> off = o.back();
  for (std::size_t k = 1; k < o.size(); k++) {
    const float len = length(k);
    if (want <= len && len > 0) {
      const float t = want / len;
      for (std::size_t a = 0; a < 3; a++) {
        off[a] = o[k - 1][a] + t * (o[k][a] - o[k - 1][a]);
      }
      break;
    }
    want -= len;
  }
  for (std::size_t a = 0; a < 3; a++) {
    xyz[a] += off[a];
  }
  return true;
}

float DisasmToMoves::getX(unsigned int piece) const {
  BT_ASSERT(piece < maxPieceName_);
  return moves_[4 * std::size_t{piece} + 0];
}
float DisasmToMoves::getY(unsigned int piece) const {
  BT_ASSERT(piece < maxPieceName_);
  return moves_[4 * std::size_t{piece} + 1];
}
float DisasmToMoves::getZ(unsigned int piece) const {
  BT_ASSERT(piece < maxPieceName_);
  return moves_[4 * std::size_t{piece} + 2];
}
float DisasmToMoves::getA(unsigned int piece) const {
  BT_ASSERT(piece < maxPieceName_);
  return moves_[4 * std::size_t{piece} + 3];
}
bool DisasmToMoves::moving(unsigned int piece) const {
  BT_ASSERT(piece < maxPieceName_);
  return mv_[piece];
}
unsigned int DisasmToMoves::getTrans(unsigned int piece) const {
  BT_ASSERT(piece < maxPieceName_);
  return orients_[piece];
}

bool DisasmToMoves::getRotationAnim(unsigned int piece, float* angleDeg, float* axisX,
                                    float* axisY, float* axisZ, float* pivotX, float* pivotY,
                                    float* pivotZ) const {
  BT_ASSERT(piece < maxPieceName_);
  if (rotAngle_[piece] == 0) {
    return false;
  }
  *angleDeg = rotAngle_[piece];
  *axisX = rotAxisX_[piece];
  *axisY = rotAxisY_[piece];
  *axisZ = rotAxisZ_[piece];
  *pivotX = rotPivotX_[piece];
  *pivotY = rotPivotY_[piece];
  *pivotZ = rotPivotZ_[piece];
  return true;
}

bool DisasmToMoves::findRotationArrival(int step, unsigned int pieceName, int* pvx, int* pvy,
                                        int* pvz, unsigned int* axis, unsigned int* sense) const {
  if (!tree_ || step < 0) {
    return false;
  }
  return findRotationArrivalRec(&*tree_, step, pieceName, pvx, pvy, pvz, axis, sense);
}

bool DisasmToMoves::findRotationArrivalRec(const Separation* t, int step, unsigned int pieceName,
                                           int* pvx, int* pvy, int* pvz, unsigned int* axis,
                                           unsigned int* sense) const {
  // the states of this node are steps 0 .. getMoves() of its subtree
  if (step >= 0 && static_cast<unsigned int>(step) <= t->getMoves()) {
    const State& st = t->getState(static_cast<unsigned int>(step));
    if (st.isRotationArrival()) {
      // A group turning shares one pivot, axis and sense, though the state
      // names one piece: any piece of this node that turns (the caller saw
      // its orientation change) turns so.
      for (unsigned int k = 0; k < t->getPieceNumber(); k++) {
        if (t->getPieceName(k) != pieceName) {
          continue;
        }
        *pvx = st.getRotPivotX();
        *pvy = st.getRotPivotY();
        *pvz = st.getRotPivotZ();
        *axis = st.getRotAxis();
        *sense = st.getRotSense();
        return true;
      }
    }
    return false;
  }

  if (step < static_cast<int>(t->getMoves())) {
    return false;
  }

  const int sub = step - static_cast<int>(t->getMoves());

  // the subtrees come after this node as in doRecursive: removed, then left
  int removedLen = 0;
  if (t->getRemoved() != nullptr) {
    if (findRotationArrivalRec(t->getRemoved(), sub, pieceName, pvx, pvy, pvz, axis, sense)) {
      return true;
    }
    removedLen = static_cast<int>(t->getRemoved()->sumSteps());
  }

  if (t->getLeft() != nullptr) {
    return findRotationArrivalRec(t->getLeft(), sub - removedLen, pieceName, pvx, pvy, pvz, axis,
                                  sense);
  }

  return false;
}

// Walks the tree and adds the pieces' positions for `step` into `array`
// (x, y, z, alpha per piece; null only counts the steps), centred on (cx,
// cy, cz). A step below 0 or past the tree's moves is outside it. Returns
// the steps the tree takes.
int DisasmToMoves::doRecursive(const Separation* tree, int step, float* array,
                               unsigned int* orientsOut, bool centerActive, int cx, int cy,
                               int cz) {
  BT_ASSERT(tree != nullptr);

  auto f = [](int v) { return static_cast<float>(v); };

  // A take-apart ends with a group pulled off the puzzle; a slide ends
  // with every piece still on the board, so its last state is a placement,
  // not a separation.
  bool takeApart = step >= 0 && static_cast<unsigned int>(step) >= tree->getMoves();
  if (takeApart) {
    const State& end = tree->getState(tree->getMoves());
    takeApart = false;
    for (unsigned int i = 0; i < tree->getPieceNumber(); i++) {
      if (end.pieceRemoved(i)) {
        takeApart = true;
        break;
      }
    }
    if (!takeApart) {
      step = static_cast<int>(tree->getMoves());
    }
  }

  // Past this node (its last move is the separation, shown as it fits the
  // screen, not as the state has it: the removed group 10000 units away):
  // place both groups apart and walk their subtrees. A group of one piece
  // has no subtree.
  if (takeApart) {
    const State& s = tree->getState(tree->getMoves());

    // a removed piece and a piece that stays
    unsigned int pc = 0;
    while (pc < tree->getPieceNumber() && !s.pieceRemoved(pc)) {
      pc++;
    }

    // the direction the group left in
    int dx = 0;
    int dy = 0;
    int dz = 0;
    const auto sz = static_cast<int>(size_);

    if (s.getX(pc) > 10000) {
      dx = sz;
    }
    if (s.getX(pc) < -10000) {
      dx = -sz;
    }
    if (s.getY(pc) > 10000) {
      dy = sz;
    }
    if (s.getY(pc) < -10000) {
      dy = -sz;
    }
    if (s.getZ(pc) > 10000) {
      dz = sz;
    }
    if (s.getZ(pc) < -10000) {
      dz = -sz;
    }

    int steps = 0;
    int steps2 = 0;

    // the removed group around its new centre
    if (tree->getRemoved() != nullptr) {
      // centred: the removed group stays where it was; else it is placed
      // out of the way
      if (centerActive) {
        const State& before = tree->getState(tree->getMoves() - 1);
        const State& first = tree->getRemoved()->getState(0);
        steps = doRecursive(tree->getRemoved(), step - static_cast<int>(tree->getMoves()), array,
                            orientsOut, centerActive, before.getX(pc) + cx - first.getX(0),
                            before.getY(pc) + cy - first.getY(0),
                            before.getZ(pc) + cz - first.getZ(0));
      } else {
        steps = doRecursive(tree->getRemoved(), step - static_cast<int>(tree->getMoves()), array,
                            orientsOut, centerActive, cx + dx, cy + dy, cz + dz);
      }

    } else {
      const State& s2 = tree->getState(tree->getMoves() - 1);

      // no subtree: the removed pieces go
      if (array != nullptr) {
        for (unsigned int p = 0; p < tree->getPieceNumber(); p++) {
          if (s.pieceRemoved(p)) {
            const std::size_t b = 4 * std::size_t{tree->getPieceName(p)};
            array[b + 0] += f(dx + cx + ((std::abs(s.getX(p)) < 10000) ? s.getX(p) : s2.getX(p)));
            array[b + 1] += f(dy + cy + ((std::abs(s.getY(p)) < 10000) ? s.getY(p) : s2.getY(p)));
            array[b + 2] += f(dz + cz + ((std::abs(s.getZ(p)) < 10000) ? s.getZ(p) : s2.getZ(p)));
            array[b + 3] += 0;
            if (orientsOut != nullptr) {
              orientsOut[tree->getPieceName(p)] = orientOf(s2, p);
            }
          }
        }
      }
    }

    // the group that stays, at the old centre
    if (tree->getLeft() != nullptr) {
      // centred, while the removed group is taken apart the staying one is
      // moved out of the way, against the direction of the removal
      const bool removedActive = centerActive &&
                                 (step - static_cast<int>(tree->getMoves()) < steps) &&
                                 tree->getRemoved() != nullptr;
      if (removedActive) {
        steps2 = doRecursive(tree->getLeft(), step - static_cast<int>(tree->getMoves()) - steps,
                             array, orientsOut, centerActive, cx - dx, cy - dy, cz - dz);
      } else {
        steps2 = doRecursive(tree->getLeft(), step - static_cast<int>(tree->getMoves()) - steps,
                             array, orientsOut, centerActive, cx, cy, cz);
      }

      // while the removed group is shown, the staying pieces are hidden
      if (array != nullptr && removedActive) {
        for (unsigned int p = 0; p < tree->getPieceNumber(); p++) {
          if (!s.pieceRemoved(p)) {
            array[4 * std::size_t{tree->getPieceName(p)} + 3] = 0;
          }
        }
      }

    } else {
      if (array != nullptr) {
        for (unsigned int p = 0; p < tree->getPieceNumber(); p++) {
          if (!s.pieceRemoved(p)) {
            const std::size_t b = 4 * std::size_t{tree->getPieceName(p)};
            array[b + 0] += f(cx + s.getX(p));
            array[b + 1] += f(cy + s.getY(p));
            array[b + 2] += f(cz + s.getZ(p));
            array[b + 3] += 0;
            if (orientsOut != nullptr) {
              orientsOut[tree->getPieceName(p)] = orientOf(s, p);
            }
          }
        }
      }
    }

    return static_cast<int>(tree->getMoves()) + steps + steps2;
  }

  // within this node (or before it: the start state)
  const State& s = tree->getState(static_cast<unsigned int>(std::max(step, 0)));

  if (array != nullptr) {
    for (unsigned int i = 0; i < tree->getPieceNumber(); i++) {
      const std::size_t b = 4 * std::size_t{tree->getPieceName(i)};
      array[b + 0] += f(cx + s.getX(i));
      array[b + 1] += f(cy + s.getY(i));
      array[b + 2] += f(cz + s.getZ(i));
      array[b + 3] += 1;
      if (orientsOut != nullptr) {
        orientsOut[tree->getPieceName(i)] = orientOf(s, i);
      }
    }
  }

  const int steps = tree->getRemoved() != nullptr
                        ? doRecursive(tree->getRemoved(),
                                      step - static_cast<int>(tree->getMoves()), nullptr, nullptr,
                                      centerActive, 0, 0, 0)
                        : 0;
  const int steps2 = tree->getLeft() != nullptr
                         ? doRecursive(tree->getLeft(),
                                       step - static_cast<int>(tree->getMoves()) - steps, nullptr,
                                       nullptr, centerActive, 0, 0, 0)
                         : 0;

  return static_cast<int>(tree->getMoves()) + steps + steps2;
}

FixedPositions::FixedPositions(const DisassemblerNode& nd, const std::vector<unsigned int>& pc,
                               unsigned int pcs)
    : pieces_(pcs), x_(pcs, 0), y_(pcs, 0), z_(pcs, 0), visible_(pcs, false) {
  for (unsigned int p = 0; p < pc.size(); p++) {
    const unsigned int pi = pc[p];
    BT_ASSERT(pi < pieces_);
    x_[pi] = nd.getX(p);
    y_[pi] = nd.getY(p);
    z_[pi] = nd.getZ(p);
    visible_[pi] = true;
  }
}

float FixedPositions::getX(unsigned int piece) const {
  BT_ASSERT(piece < pieces_);
  return static_cast<float>(x_[piece]);
}
float FixedPositions::getY(unsigned int piece) const {
  BT_ASSERT(piece < pieces_);
  return static_cast<float>(y_[piece]);
}
float FixedPositions::getZ(unsigned int piece) const {
  BT_ASSERT(piece < pieces_);
  return static_cast<float>(z_[piece]);
}
float FixedPositions::getA(unsigned int piece) const { return visible_[piece] ? 1.0F : 0.0F; }
bool FixedPositions::moving(unsigned int /*piece*/) const { return false; }

}  // namespace bt
