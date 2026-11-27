// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/rotationmoves_0.cpp and rotationmoves_crowell.cpp:
// what the two generators had a copy each of.
#include "shapelogic/solve/rotation_moves.h"

#include "shapelogic/base/assert.h"
#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/symmetries.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/solve/movement_cache.h"

namespace sl {

namespace {

// Cube orientations of +-90 degrees about x, y and z (tabs_0/rotmatrix.inc).
constexpr unsigned char kRotXP90 = 1;
constexpr unsigned char kRotXM90 = 3;
constexpr unsigned char kRotYP90 = 12;
constexpr unsigned char kRotYM90 = 4;
constexpr unsigned char kRotZP90 = 16;
constexpr unsigned char kRotZM90 = 20;

// The cube orientation of a +-90 degree turn about an axis.
unsigned char rotationTransformId(unsigned int axis, unsigned int sense) {
  static constexpr unsigned char kIds[3][2] = {
      {kRotXP90, kRotXM90}, {kRotYP90, kRotYM90}, {kRotZP90, kRotZM90}};
  SL_ASSERT(axis < 3 && sense < 2);
  return kIds[axis][sense];
}

}  // namespace

void RotationMoves::collectWorldCells(MovementCache& cache, const DisassemblerNode& nd,
                                      const std::vector<unsigned int>& pieces,
                                      unsigned int pieceIdx,
                                      std::vector<RotationRules::Cell>& out) {
  out.clear();

  const unsigned int shapeId = cache.getShapeOfPiece(pieces[pieceIdx]);
  const auto trans = static_cast<unsigned char>(nd.getTrans(pieceIdx));
  const Voxel& sh = cache.getTransformedShape(shapeId, trans);

  const int px = nd.getX(pieceIdx);
  const int py = nd.getY(pieceIdx);
  const int pz = nd.getZ(pieceIdx);
  const int hx = sh.getHx();
  const int hy = sh.getHy();
  const int hz = sh.getHz();

  for (unsigned int z = 0; z < sh.getZ(); z++) {
    for (unsigned int y = 0; y < sh.getY(); y++) {
      for (unsigned int x = 0; x < sh.getX(); x++) {
        if (sh.isFilled(x, y, z)) {
          out.emplace_back(px - hx + asInt(x), py - hy + asInt(y), pz - hz + asInt(z));
        }
      }
    }
  }
}

RotationMoves::TurnBox RotationMoves::turnBox(const std::vector<RotationRules::Cell>& moving,
                                              unsigned int axis) {
  SL_ASSERT(!moving.empty());

  auto planeU = [axis](const RotationRules::Cell& p) { return (axis == 0) ? p.y : p.x; };
  auto planeV = [axis](const RotationRules::Cell& p) { return (axis == 2) ? p.y : p.z; };
  auto along = [axis](const RotationRules::Cell& p) {
    return (axis == 0) ? p.x : ((axis == 1) ? p.y : p.z);
  };

  TurnBox box;
  box.umin = box.umax = planeU(moving[0]);
  box.vmin = box.vmax = planeV(moving[0]);
  box.amin = along(moving[0]);
  for (std::size_t c = 1; c < moving.size(); c++) {
    const int u = planeU(moving[c]);
    const int v = planeV(moving[c]);
    const int a = along(moving[c]);
    box.umin = u < box.umin ? u : box.umin;
    box.umax = u > box.umax ? u : box.umax;
    box.vmin = v < box.vmin ? v : box.vmin;
    box.vmax = v > box.vmax ? v : box.vmax;
    box.amin = a < box.amin ? a : box.amin;
  }
  return box;
}

NodeRef RotationMoves::turnedNode(DisassemblerNode* from, const Symmetries& sym,
                                  unsigned int moveMask, const RotationRules::Pivot& pivot,
                                  unsigned int axis, unsigned int sense) {
  const unsigned int n = from->getPiecenumber();

  unsigned int primaryPiece = 0;
  while (primaryPiece < n && (moveMask & (1U << primaryPiece)) == 0) {
    primaryPiece++;
  }
  SL_ASSERT(primaryPiece < n);

  const unsigned char rotId = rotationTransformId(axis, sense);
  bool changed = false;

  const unsigned int dir = DisassemblerNode::kRotationDirBase + axis * 2 + sense;
  NodeRef node = NodeRef::make(n, NodeRef::share(from), static_cast<int>(dir), 1);
  node->setRotationInfo(primaryPiece, pivot.hx, pivot.hy, pivot.hz);

  for (unsigned int i = 0; i < n; i++) {
    if (from->isPieceRemoved(i) || (moveMask & (1U << i)) == 0) {
      node->set(i, from->getX(i), from->getY(i), from->getZ(i), from->getTrans(i));
      continue;
    }

    const auto oldTrans = static_cast<unsigned char>(from->getTrans(i));
    const unsigned char newTrans = sym.transAdd(oldTrans, rotId);

    const RotationRules::Cell at(from->getX(i), from->getY(i), from->getZ(i));
    RotationRules::Cell to;
    if (!RotationRules::rotateCell(at, pivot, axis, sense, to)) {
      return nullptr;
    }

    if (to.x != at.x || to.y != at.y || to.z != at.z || newTrans != oldTrans) {
      changed = true;
    }

    node->set(i, to.x, to.y, to.z, newTrans);
  }

  if (!changed) {
    return nullptr;
  }

  return node;
}

}  // namespace sl
