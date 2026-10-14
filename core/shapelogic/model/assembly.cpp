// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/assembly.cpp.

#include "shapelogic/model/assembly.h"

#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"

#include <algorithm>
#include <utility>

namespace bt {

void MirrorInfo::addPieces(unsigned int p1, unsigned int p2, unsigned char trans) {
  entries_.push_back(Entry{p1, p2, trans});
}

bool MirrorInfo::getPieceInfo(unsigned int p, unsigned int* pOut, unsigned char* trans) const {
  for (const Entry& e : entries_) {
    if (e.pc1 == p) {
      *pOut = e.pc2;
      *trans = e.trans;
      return true;
    }
  }
  return false;
}

void Assembly::sort(const Problem& problem) {
  unsigned int p = 0;

  for (unsigned int i = 0; i < problem.getNumberOfParts(); i++) {
    const unsigned int cnt = problem.getPartMaximum(i);

    // how many pieces of this part are actually placed (placed ones come first)
    unsigned int cnt2 = 0;
    while (cnt2 < cnt && isPlaced(p + cnt2)) {
      cnt2++;
    }

    // bubble sort: only a few identical pieces, and often few of them placed
    if (cnt2 > 1) {
      for (unsigned int a = 0; a < cnt2 - 1; a++) {
        bool swapped = false;

        for (unsigned int b = cnt2 - 1; b > a; b--) {
          if (placements_[p + b] < placements_[p + b - 1]) {
            std::swap(placements_[p + b], placements_[p + b - 1]);
            swapped = true;
          }
        }

        if (!swapped) {
          break;
        }
      }
    }

    p += cnt;
  }
}

bool Assembly::transform(unsigned char trans, const Puzzle& puzzle, const Problem& problem,
                         const MirrorInfo* mir) {
  if (trans == 0) {
    return true;
  }

  // mirroring needs the mirror information of the pieces
  BT_ASSERT(trans < sym().getNumTransformations() || mir != nullptr);

  const Voxel& result = problem.resultShape(puzzle);

  int rx = 0;
  int ry = 0;
  int rz = 0;
  if (!result.getHotspot(trans, &rx, &ry, &rz)) {
    return false;
  }

  /* The positions of the pieces are vectors from the origin. Rotating them is
   * easy, but they have to start from where the origin goes: that is the
   * result's hotspot. The rotated assembly must also stay in place when the
   * result looks the same in the new orientation, which the bounding-box
   * shift below takes care of.
   */
  {
    /* on some grids the origin does not stay in place when transformed (a
     * cell has to change its kind, e.g. triangles turned by 60 degrees); find
     * where the hotspot goes and allow for that */
    int hx = result.getHx();
    int hy = result.getHy();
    int hz = result.getHz();

    result.transformPoint(&hx, &hy, &hz, trans);

    rx -= hx;
    ry -= hy;
    rz -= hz;
  }

  {
    /* a result that is not minimised, or one on a grid other than cubes,
     * jumps around when rotated; this keeps the rotation in place (this is
     * only called when the result looks the same in the new orientation) */
    int cx = 0;
    int cy = 0;
    int cz = 0;
    int dx = 0;
    int dy = 0;
    int dz = 0;

    if (!result.getBoundingBox(trans, &cx, &cy, &cz)) {
      return false;
    }
    if (!result.getBoundingBox(0, &dx, &dy, &dz)) {
      return false;
    }

    rx += dx - cx;
    ry += dy - cy;
    rz += dz - cz;
  }

  unsigned int p = 0;

  for (unsigned int i = 0; i < problem.getNumberOfParts(); i++) {
    for (unsigned int j = 0; j < problem.getPartMaximum(i); j++) {
      // an unplaced piece ends the placed pieces of its part
      if (!isPlaced(p)) {
        p += problem.getPartMaximum(i) - j;
        j = problem.getPartMaximum(i);
        continue;
      }

      Placement& pl = placements_[p];
      result.transformPoint(&pl.xpos, &pl.ypos, &pl.zpos, trans);

      pl.xpos += rx;
      pl.ypos += ry;
      pl.zpos += rz;

      // add the transformations, then find the smallest one with the same look
      pl.transformation = sym().transAdd(pl.transformation, trans);
      if (pl.transformation == kTransformUndefined) {
        return false;
      }

      const Voxel& shape = puzzle.getShape(problem.getShapeIdOfPart(i));
      const unsigned char tr = shape.normalizeTransformation(pl.transformation);

      if (tr != pl.transformation) {
        /* the normalised orientation differs from the calculated one: move the
         * piece so that it stays where it is in the normalised orientation */
        int ax = 0;
        int ay = 0;
        int az = 0;
        int bx = 0;
        int by = 0;
        int bz = 0;
        int cx = 0;
        int cy = 0;
        int cz = 0;
        int dx = 0;
        int dy = 0;
        int dz = 0;
        shape.getHotspot(pl.transformation, &ax, &ay, &az);
        shape.getHotspot(tr, &bx, &by, &bz);

        shape.getBoundingBox(pl.transformation, &cx, &cy, &cz);
        shape.getBoundingBox(tr, &dx, &dy, &dz);

        pl.xpos += bx - ax + cx - dx;
        pl.ypos += by - ay + cy - dy;
        pl.zpos += bz - az + cz - dz;

        pl.transformation = tr;
      }

      p++;
    }
  }

  // finally, swap mirrored pieces with their mirror partners where possible,
  // so they no longer need a mirrored orientation
  if (mir != nullptr) {
    p = 0;

    for (unsigned int i = 0; i < problem.getNumberOfParts(); i++) {
      for (unsigned int j = 0; j < problem.getPartMaximum(i); j++) {
        if (!isPlaced(p)) {
          p += problem.getPartMaximum(i) - j;
          j = problem.getPartMaximum(i);
          continue;
        }

        if (placements_[p].transformation >= sym().getNumTransformations()) {
          unsigned int p2 = 0;
          unsigned char t = 0;

          /* without a mirror partner we stop; this happens with piece ranges,
           * where it is not known which pieces the solution uses */
          if (!mir->getPieceInfo(p, &p2, &t)) {
            break;
          }

          unsigned int p3 = 0;
          unsigned char tInv = 0;

          const bool partnerKnown = mir->getPieceInfo(p2, &p3, &tInv);
          BT_ASSERT_MSG(partnerKnown, "piece info could not be found");
          BT_ASSERT(p3 == p);

          unsigned int i2 = 0;
          {
            unsigned int ss = 0;
            while (ss + problem.getPartMaximum(i2) <= p2) {
              ss += problem.getPartMaximum(i2);
              i2++;
            }
          }

          const Voxel& shape1 = problem.partShape(puzzle, i);
          const Voxel& shape2 = problem.partShape(puzzle, i2);

          /* t and tInv do not have to be inverse transformations: a symmetric
           * piece may end up in one of its symmetric orientations, hence the
           * normalisation */
          if (sym().transAdd(t, tInv) == kTransformUndefined ||
              sym().transAdd(tInv, t) == kTransformUndefined) {
            return false;
          }

          BT_ASSERT(shape1.normalizeTransformation(sym().transAdd(t, tInv)) == 0);
          BT_ASSERT(shape2.normalizeTransformation(sym().transAdd(tInv, t)) == 0);

          /* piece p is replaced by piece p2: find the target orientations of
           * both and move them by their hotspots */
          if (isPlaced(p2)) {
            int p1x = placements_[p].xpos;
            int p1y = placements_[p].ypos;
            int p1z = placements_[p].zpos;
            unsigned char p1t = placements_[p].transformation;

            int p2x = placements_[p2].xpos;
            int p2y = placements_[p2].ypos;
            int p2z = placements_[p2].zpos;
            unsigned char p2t = placements_[p2].transformation;

            // the partner must be mirrored, too
            BT_ASSERT(p2t >= sym().getNumTransformations());

            int hx = 0;
            int hy = 0;
            int hz = 0;

            /* the origin of the bounding boxes of the two pieces: the same for
             * the mirrored piece and the one that replaces it, as both have
             * the same shape in that orientation */
            shape1.getHotspot(p1t, &hx, &hy, &hz);
            p1x -= hx;
            p1y -= hy;
            p1z -= hz;
            shape1.getBoundingBox(p1t, &hx, &hy, &hz);
            p1x += hx;
            p1y += hy;
            p1z += hz;

            shape2.getHotspot(p2t, &hx, &hy, &hz);
            p2x -= hx;
            p2y -= hy;
            p2z -= hz;
            shape2.getBoundingBox(p2t, &hx, &hy, &hz);
            p2x += hx;
            p2y += hy;
            p2z += hz;

            /* A_0 -t-> B_0 and B_0 -tInv-> A_0; the orientation x with B_x
             * identical to A_p1t is B_0 -tInv-> A_0 -p1t-> A_p1t, and the
             * same the other way round */
            if (sym().transAdd(tInv, p1t) == kTransformUndefined ||
                sym().transAdd(t, p2t) == kTransformUndefined) {
              return false;
            }
            p1t = shape2.normalizeTransformation(sym().transAdd(tInv, p1t));
            p2t = shape1.normalizeTransformation(sym().transAdd(t, p2t));

            // back from the bounding-box origin to the hotspot
            shape2.getHotspot(p1t, &hx, &hy, &hz);
            p1x += hx;
            p1y += hy;
            p1z += hz;
            shape2.getBoundingBox(p1t, &hx, &hy, &hz);
            p1x -= hx;
            p1y -= hy;
            p1z -= hz;

            shape1.getHotspot(p2t, &hx, &hy, &hz);
            p2x += hx;
            p2y += hy;
            p2z += hz;
            shape1.getBoundingBox(p2t, &hx, &hy, &hz);
            p2x -= hx;
            p2y -= hy;
            p2z -= hz;

            placements_[p] = Placement{p2t, p2x, p2y, p2z};
            placements_[p2] = Placement{p1t, p1x, p1y, p1z};

          } else {
            // the partner is not placed, so after the swap piece p is not either
            int p1x = placements_[p].xpos;
            int p1y = placements_[p].ypos;
            int p1z = placements_[p].zpos;
            unsigned char p1t = placements_[p].transformation;

            int hx = 0;
            int hy = 0;
            int hz = 0;

            shape1.getHotspot(p1t, &hx, &hy, &hz);
            p1x -= hx;
            p1y -= hy;
            p1z -= hz;
            shape1.getBoundingBox(p1t, &hx, &hy, &hz);
            p1x += hx;
            p1y += hy;
            p1z += hz;

            if (sym().transAdd(tInv, p1t) == kTransformUndefined) {
              return false;
            }
            p1t = shape2.normalizeTransformation(sym().transAdd(tInv, p1t));

            shape2.getHotspot(p1t, &hx, &hy, &hz);
            p1x += hx;
            p1y += hy;
            p1z += hz;
            shape2.getBoundingBox(p1t, &hx, &hy, &hz);
            p1x -= hx;
            p1y -= hy;
            p1z -= hz;

            placements_[p] = Placement{kUnplacedTrans, 0, 0, 0};
            placements_[p2] = Placement{p1t, p1x, p1y, p1z};
          }
        }

        p++;
      }
    }
  }

  sort(problem);

  return true;
}

bool Assembly::compare(const Assembly& b, unsigned int pivot) const {
  BT_ASSERT(placements_.size() == b.placements_.size());

  /* the pivot piece first: it is the one with reduced placements, which may
   * not occur in all positions, so the reduction could otherwise pick an
   * assembly that does not exist */
  if (pivot < placements_.size()) {
    if (placements_[pivot] < b.placements_[pivot]) {
      return true;
    }
    if (!(placements_[pivot] == b.placements_[pivot])) {
      return false;
    }
  }

  for (unsigned int i = 0; i < placements_.size(); i++) {
    if (i != pivot) {
      if (placements_[i] < b.placements_[i]) {
        return true;
      }
      if (!(placements_[i] == b.placements_[i])) {
        return false;
      }
    }
  }

  return false;
}

bool Assembly::containsMirroredPieces() const {
  for (unsigned int i = 0; i < placements_.size(); i++) {
    if (isPlaced(i) && placements_[i].transformation >= sym().getNumTransformations()) {
      return true;
    }
  }
  return false;
}

bool Assembly::validSolution(const Problem& problem) const {
  unsigned int pos = 0;

  for (unsigned int i = 0; i < problem.getNumberOfParts(); i++) {
    unsigned int placed = 0;

    while (placed < problem.getPartMaximum(i) && isPlaced(pos + placed)) {
      placed++;
    }

    if (placed < problem.getPartMinimum(i)) {
      return false;
    }

    pos += problem.getPartMaximum(i);
  }

  return true;
}

bool Assembly::smallerRotationExists(const Puzzle& puzzle, const Problem& problem,
                                     unsigned int pivot, const MirrorInfo* mir, bool complete,
                                     bool strictColors) const {
  /* mirrored transformations are only checked with mirror information;
   * without it, the piece set has a piece without mirror symmetry and
   * without a mirror partner */
  const unsigned int endTrans =
      mir != nullptr ? sym().getNumTransformationsMirror() : sym().getNumTransformations();

  if (complete) {
    for (unsigned int t = 0; t < endTrans; t++) {
      Assembly tmp(*this);

      if (!tmp.transform(static_cast<unsigned char>(t), puzzle, problem, mir)) {
        continue;
      }

      if (t >= sym().getNumTransformations() &&
          (tmp.containsMirroredPieces() || !tmp.validSolution(problem))) {
        continue;
      }

      /* Can the whole rotated arrangement be shifted to lower positions? The
       * shift loop runs in the grid of createSpace, which starts at index 0;
       * the placements are in result coordinates. origin is the result
       * position of index 0, so a loop shift (x,y,z) is a result translation
       * of (x,y,z) - origin. */
      int originX = 0;
      int originY = 0;
      int originZ = 0;
      const std::unique_ptr<Voxel> assm =
          tmp.createSpace(puzzle, problem, &originX, &originY, &originZ);
      const Voxel& res = problem.resultShape(puzzle);

      for (int x = asInt(res.boundX1()) - asInt(assm->boundX1());
           asInt(assm->boundX2()) + x <= asInt(res.boundX2()); x++) {
        for (int y = asInt(res.boundY1()) - asInt(assm->boundY1());
             asInt(assm->boundY2()) + y <= asInt(res.boundY2()); y++) {
          for (int z = asInt(res.boundZ1()) - asInt(assm->boundZ1());
               asInt(assm->boundZ2()) + z <= asInt(res.boundZ2()); z++) {
            if (!assm->onGrid(x, y, z)) {
              continue;
            }

            bool fits = true;

            for (int pz = asInt(assm->boundZ1()); pz <= asInt(assm->boundZ2()); pz++) {
              for (int py = asInt(assm->boundY1()); py <= asInt(assm->boundY2()); py++) {
                for (int px = asInt(assm->boundX1()); px <= asInt(assm->boundX2()); px++) {
                  const auto upx = asUnsigned(px);
                  const auto upy = asUnsigned(py);
                  const auto upz = asUnsigned(pz);
                  if (
                      // a filled cell needs a result cell
                      (assm->getState(upx, upy, upz) == Voxel::kFilled &&
                       res.getState2(x + px, y + py, z + pz) == Voxel::kEmpty) ||

                      // every must-be-filled result cell needs a piece cell
                      (assm->getState(upx, upy, upz) == Voxel::kEmpty &&
                       res.getState2(x + px, y + py, z + pz) == Voxel::kFilled) ||

                      // colour constraints; an empty neutral cell has none
                      ((assm->getState(upx, upy, upz) == Voxel::kFilled ||
                        assm->getColor(upx, upy, upz) != 0) &&
                       !problem.placementAllowed(puzzle, assm->getColor(upx, upy, upz),
                                                 res.getColor2(x + px, y + py, z + pz),
                                                 strictColors))) {
                    fits = false;
                  }
                }
              }
            }

            if (fits) {
              for (Placement& pl : tmp.placements_) {
                pl.xpos += x - originX;
                pl.ypos += y - originY;
                pl.zpos += z - originZ;
              }

              if (tmp.compare(*this, pivot)) {
                return true;
              }

              for (Placement& pl : tmp.placements_) {
                pl.xpos -= x - originX;
                pl.ypos -= y - originY;
                pl.zpos -= z - originZ;
              }
            }
          }
        }
      }
    }
  } else {
    // one scratch copy for every transformation: assigning reuses its
    // placements' memory (this runs for each assembly a search finds)
    const Symmetry s = problem.resultShape(puzzle).selfSymmetries();
    Assembly tmp(*this);
    for (unsigned int t = 0; t < endTrans; t++) {
      if (sym().symmetrieContainsTransformation(s, t)) {
        tmp = *this;
        const bool transformed = tmp.transform(static_cast<unsigned char>(t), puzzle, problem, mir);
        BT_ASSERT(transformed);

        /* an orientation that needs mirrored pieces is invalid; so is one
         * that uses a wrong number of pieces of a shape, which can happen
         * when a mirror shape has a different piece range */
        if (t >= sym().getNumTransformations() &&
            (tmp.containsMirroredPieces() || !tmp.validSolution(problem))) {
          continue;
        }

        if (tmp.compare(*this, pivot)) {
          return true;
        }
      }
    }
  }

  return false;
}

void Assembly::exchangeShape(unsigned int s1, unsigned int s2) {
  BT_ASSERT(s1 < placements_.size());
  BT_ASSERT(s2 < placements_.size());
  std::swap(placements_[s1], placements_[s2]);
}

int Assembly::comparePieces(const Assembly& b) const {
  BT_ASSERT(placements_.size() == b.placements_.size());

  for (unsigned int piece = 0; piece < placements_.size(); piece++) {
    if (placements_[piece].transformation == kUnplacedTrans &&
        b.placements_[piece].transformation != kUnplacedTrans) {
      return -1;
    }
    if (placements_[piece].transformation != kUnplacedTrans &&
        b.placements_[piece].transformation == kUnplacedTrans) {
      return 1;
    }
  }

  return 0;
}

std::unique_ptr<Voxel> Assembly::createSpace(const Puzzle& puzzle, const Problem& problem,
                                             int* originX, int* originY, int* originZ) const {
  std::vector<std::unique_ptr<Voxel>> pieces(placements_.size());

  int minX = 0;
  int minY = 0;
  int minZ = 0;
  int maxX = 1;
  int maxY = 1;
  int maxZ = 1;
  bool any = false;

  // the transformed pieces and the box around all of them
  for (unsigned int i = 0; i < placements_.size(); i++) {
    if (placements_[i].transformation == kUnplacedTrans) {
      continue;
    }

    const unsigned int j = problem.getPartIdToPieceId(i);

    std::unique_ptr<Voxel> pc = problem.partShape(puzzle, j).clone();

    const bool transformed = pc->transform(placements_[i].transformation);
    BT_ASSERT(transformed);

    const int dx = placements_[i].xpos - pc->getHx();
    const int dy = placements_[i].ypos - pc->getHy();
    const int dz = placements_[i].zpos - pc->getHz();

    const int x1 = dx;
    const int y1 = dy;
    const int z1 = dz;
    const int x2 = asInt(pc->getX()) + dx;
    const int y2 = asInt(pc->getY()) + dy;
    const int z2 = asInt(pc->getZ()) + dz;

    if (!any) {
      minX = x1;
      minY = y1;
      minZ = z1;
      maxX = x2;
      maxY = y2;
      maxZ = z2;
      any = true;
    } else {
      minX = std::min(minX, x1);
      minY = std::min(minY, y1);
      minZ = std::min(minZ, z1);
      maxX = std::max(maxX, x2);
      maxY = std::max(maxY, y2);
      maxZ = std::max(maxZ, z2);
    }

    pieces[i] = std::move(pc);
  }

  if (originX != nullptr) {
    *originX = minX;
  }
  if (originY != nullptr) {
    *originY = minY;
  }
  if (originZ != nullptr) {
    *originZ = minZ;
  }

  if (!any) {
    return puzzle.getGridType().makeVoxel(1, 1, 1, 0);
  }

  unsigned int sx = asUnsigned(maxX - minX);
  unsigned int sy = asUnsigned(maxY - minY);
  unsigned int sz = asUnsigned(maxZ - minZ);
  if (sx == 0) {
    sx = 1;
  }
  if (sy == 0) {
    sy = 1;
  }
  if (sz == 0) {
    sz = 1;
  }

  std::unique_ptr<Voxel> res = puzzle.getGridType().makeVoxel(sx, sy, sz, 0);
  res->skipRecalcBoundingBox(true);

  for (unsigned int i = 0; i < placements_.size(); i++) {
    if (placements_[i].transformation == kUnplacedTrans) {
      continue;
    }

    const Voxel& pc = *pieces[i];

    const int dx = placements_[i].xpos - pc.getHx() - minX;
    const int dy = placements_[i].ypos - pc.getHy() - minY;
    const int dz = placements_[i].zpos - pc.getHz() - minZ;

    for (unsigned int x = 0; x < pc.getX(); x++) {
      for (unsigned int y = 0; y < pc.getY(); y++) {
        for (unsigned int z = 0; z < pc.getZ(); z++) {
          if (pc.getState(x, y, z) != Voxel::kEmpty) {
            res->set(x + asUnsigned(dx), y + asUnsigned(dy), z + asUnsigned(dz), pc.get(x, y, z));
          }
        }
      }
    }
  }

  res->skipRecalcBoundingBox(false);
  res->initHotspot();
  return res;
}

void Assembly::removePieces(unsigned int from, unsigned int cnt) {
  if (cnt == 0) {
    return;
  }

  BT_ASSERT(from + cnt <= placements_.size());

  for (unsigned int i = 0; i < cnt; i++) {
    BT_ASSERT(!isPlaced(from + i));
  }

  placements_.erase(placements_.begin() + from, placements_.begin() + from + cnt);
}

void Assembly::addNonPlacedPieces(unsigned int from, unsigned int cnt) {
  if (cnt == 0) {
    return;
  }

  BT_ASSERT(from <= placements_.size());

  placements_.insert(placements_.begin() + from, cnt, Placement{kUnplacedTrans, 0, 0, 0});
}

}  // namespace bt
