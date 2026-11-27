// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/assembler_0.cpp and assembler_1.cpp: what their
// prepare(), getPieceInformation() and the reading of a saved position had
// a copy each of.
#include "shapelogic/solve/matrix_build.h"

#include "shapelogic/base/assert.h"
#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/solve/placement_finder.h"
#include "shapelogic/solve/rotation_filter.h"

#include <cstdlib>
#include <utility>

namespace bt {

namespace {

/// Adds `piece` to the cache unless an identical shape is there. Returns it,
/// or null when it was a duplicate (and is dropped).
const Voxel* addToCache(std::vector<std::unique_ptr<Voxel>>& cache,
                        std::unique_ptr<Voxel> piece) {
  for (const auto& c : cache) {
    if (c->identicalInBB(*piece)) {
      return nullptr;
    }
  }
  cache.push_back(std::move(piece));
  return cache.back().get();
}

}  // namespace

std::size_t rowIndexOf(const std::vector<RowPlacement>& rows, unsigned int node) {
  // rows is in row order (the nodes are made row after row), so the last
  // entry at or before the node is found by halving
  std::size_t lo = 0;
  std::size_t hi = rows.size();
  while (lo < hi) {
    const std::size_t mid = lo + (hi - lo) / 2;
    if (rows[mid].row <= node) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }

  BT_ASSERT_MSG(lo != 0, "getPieceInformation: no piece position for the node");

  return lo - 1;
}

// The mirror check needs the mirror structure. Which case is this?
// 1) all pieces have mirror symmetry: check mirrors, no pairs
// 2) a piece has none:
//   2a) each such piece has a mirror partner: check mirrors, with pairs
//   2b) one has no partner: no mirror check
std::unique_ptr<MirrorInfo> mirrorPairs(const Puzzle& puzzle, const Problem& problem,
                                        bool evenWithoutPartner) {
  const Symmetries& sym = puzzle.getGridType().getSymmetries();

  struct Mm {
    unsigned int shape;   // the shape of this piece
    unsigned int mirror;  // the mirror shape of this piece
    unsigned int trans;
  };

  std::vector<Mm> mirror(problem.getNumberOfPieces());

  unsigned int pc = 0;
  for (unsigned int i = 0; i < problem.getNumberOfParts(); i++) {
    for (unsigned int p = 0; p < problem.getPartMaximum(i); p++) {
      mirror[pc].shape = i;
      mirror[pc].mirror = static_cast<unsigned int>(-1);
      mirror[pc].trans = 255;
      pc++;
    }
  }

  bool mirrorCheck = true;

  for (unsigned int i = 0; i < problem.getNumberOfPieces(); i++) {
    // the mirror of this one was found already
    if (mirror[i].mirror < problem.getNumberOfPieces()) {
      continue;
    }

    if (!sym.symmetryContainsMirror(problem.partShape(puzzle, mirror[i].shape).selfSymmetries())) {
      // not self-mirroring: look for a shape that is its mirror
      bool found = false;

      for (unsigned int j = i + 1; j < problem.getNumberOfPieces(); j++) {
        if (mirror[j].mirror < problem.getNumberOfPieces()) {
          continue;
        }

        const unsigned int trans =
            problem.partShape(puzzle, mirror[i].shape)
                .getMirrorTransform(problem.partShape(puzzle, mirror[j].shape));

        if (trans > 0) {
          mirror[i].mirror = j;
          mirror[i].trans = trans;
          mirror[j].mirror = i;
          mirror[j].trans = problem.partShape(puzzle, mirror[j].shape)
                                .getMirrorTransform(problem.partShape(puzzle, mirror[i].shape));

          found = true;
          break;
        }
      }

      // a piece without mirror symmetry and without a partner: no mirror
      // check
      if (!found) {
        mirrorCheck = false;
        break;
      }
    }
  }

  if (!mirrorCheck && !evenWithoutPartner) {
    return nullptr;
  }

  auto mir = std::make_unique<MirrorInfo>();

  for (unsigned int i = 0; i < problem.getNumberOfPieces(); i++) {
    if (mirror[i].trans != 255) {
      mir->addPieces(i, mirror[i].mirror, static_cast<unsigned char>(mirror[i].trans));
    }
  }

  return mir;
}

int placeParts(const Puzzle& puzzle, const Problem& problem, const PlaceParts& how,
               PlacementSink& sink, std::atomic<unsigned int>& part, const StopToken& stop) {
  const Voxel& result = problem.resultShape(puzzle);
  const Symmetries& sym = puzzle.getGridType().getSymmetries();

  std::vector<std::unique_ptr<Voxel>> cache;
  cache.reserve(sym.getNumTransformationsMirror());

  const PlacementFinder finder(puzzle, problem, result, how.strictColors, how.startLocks);
  std::vector<long> voxelOffsets;
  std::vector<int> positions;

  const long rsx = result.getX();
  const long rsy = result.getY();

  for (unsigned int pc = 0; pc < problem.getNumberOfParts(); pc++) {
    part.store(pc, std::memory_order_relaxed);
    const PlacementFinder::StartLock lock{
        static_cast<unsigned char>(problem.getShapeIdOfPart(pc) + 1),
        pc < how.freeStarts.size() ? how.freeStarts[pc] : true};

    sink.beginPart(pc);

    // the orientations found so far, so that no orientation goes in twice
    cache.clear();
    bool placed = false;

    const unsigned int rotEnd = transformEnd(how.partTransforms, sym);
    for (unsigned int rot = 0; rot < rotEnd; rot++) {
      if (!transformAllowed(how.partTransforms, pc, rot)) {
        continue;
      }
      if (stop.stopRequested()) {
        return kPrepareStopped;
      }
      std::unique_ptr<Voxel> rotated = problem.partShape(puzzle, pc).clone();
      if (!rotated->transform(rot)) {
        continue;
      }

      const Voxel* rotation = addToCache(cache, std::move(rotated));

      if (rotation != nullptr) {
        finder.find(*rotation, voxelOffsets, positions, stop, lock);
        // stopped, the finder's list is not whole
        if (stop.stopRequested()) {
          return kPrepareStopped;
        }

        for (std::size_t p = 0; p < positions.size(); p += 3) {
          if ((p & kPreparePollMask) == 0 && p != 0 && stop.stopRequested()) {
            return kPrepareStopped;
          }
          const int x = positions[p];
          const int y = positions[p + 1];
          const int z = positions[p + 2];

          // the voxels the piece fills
          const long base = x + rsx * (y + rsy * static_cast<long>(z));
          sink.addRow(pc, rot, x + rotation->getHx(), y + rotation->getHy(),
                      z + rotation->getHz(), base, voxelOffsets);
          placed = true;
        }

        sink.endOrientation();

        // for the symmetry breaker also every symmetry of the result
        if (pc == how.symBreakerShape) {
          for (unsigned int r = 1; r < sym.getNumTransformations(); r++) {
            if (sym.symmetrieContainsTransformation(how.resultSym, r)) {
              std::unique_ptr<Voxel> vx = problem.partShape(puzzle, pc).clone();

              if (!vx->transform(rot) || !vx->transform(r)) {
                continue;
              }

              (void)addToCache(cache, std::move(vx));
            }
          }
        }
      }
    }

    // a part that must be used and fits nowhere
    if (!placed && (how.everyPartNeeded || problem.getPartMinimum(pc) > 0)) {
      return -asInt(problem.getShapeIdOfPart(pc));
    }
  }

  return kPrepared;
}

std::unique_ptr<RowRotationCheck> makeRowCheck(const SearchOptions& options, const Puzzle& puzzle,
                                               const Problem& problem,
                                               const RotationFilter& filter,
                                               const std::vector<RowPlacement>& rows,
                                               std::size_t nodes,
                                               std::vector<std::uint32_t>& rowOfNode) {
  if (!options.rowRotationCheck || !filter.avoidTransformed) {
    return nullptr;
  }

  std::vector<RowRotationCheck::Row> checkRows;
  checkRows.reserve(rows.size());
  for (const RowPlacement& pp : rows) {
    checkRows.push_back({pp.piece, Placement{pp.transformation, pp.x, pp.y, pp.z}});
  }
  std::unique_ptr<RowRotationCheck> check =
      RowRotationCheck::make(puzzle, problem, filter, std::move(checkRows));

  // a row's nodes are made one after the other, the rows in order
  if (check != nullptr && rowOfNode.size() != nodes) {
    rowOfNode.assign(nodes, 0);
    for (std::size_t i = 0; i < rows.size(); i++) {
      const std::size_t end = i + 1 < rows.size() ? rows[i + 1].row : nodes;
      for (std::size_t node = rows[i].row; node < end; node++) {
        rowOfNode[node] = static_cast<std::uint32_t>(i);
      }
    }
  }
  return check;
}

unsigned int getInt(const char* s, unsigned int* i) {
  char* s2 = nullptr;
  *i = static_cast<unsigned int>(std::strtol(s, &s2, 10));
  return static_cast<unsigned int>(s2 - s);
}

}  // namespace bt
