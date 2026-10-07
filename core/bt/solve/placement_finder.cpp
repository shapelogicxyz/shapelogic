// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from placementFinder_c in src/lib/assembler.cpp.
#include "bt/solve/placement_finder.h"

#include "bt/base/int_cast.h"
#include "bt/grid/voxel.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"

#include <algorithm>

namespace bt {

PlacementFinder::PlacementFinder(const Puzzle& puzzle, const Problem& problem,
                                 const Voxel& result, bool strictColors)
    : res_(result), colorTableWidth_(puzzle.colorNumber() + 1) {
  if (colorTableWidth_ > 1) {
    colorAllowed_.resize(colorTableWidth_ * colorTableWidth_);
    for (unsigned int p = 0; p < colorTableWidth_; p++) {
      for (unsigned int r = 0; r < colorTableWidth_; r++) {
        colorAllowed_[p * colorTableWidth_ + r] =
            problem.placementAllowed(puzzle, p, r, strictColors) ? 1 : 0;
      }
    }
  }

  // all non-empty voxels of the result: the only voxels a filled piece voxel
  // may end up on, so the only candidate positions for a piece's anchor
  unsigned int idx = 0;
  for (unsigned int z = 0; z < result.getZ(); z++) {
    for (unsigned int y = 0; y < result.getY(); y++) {
      for (unsigned int x = 0; x < result.getX(); x++, idx++) {
        if (result.getState(idx) != Voxel::kEmpty) {
          resultVoxels_.push_back(ResultVoxel{asInt(x), asInt(y), asInt(z), idx,
                                              static_cast<unsigned char>(result.getColor(idx))});
        }
      }
    }
  }
}

void PlacementFinder::find(const Voxel& rotation, std::vector<long>& voxelOffsets,
                           std::vector<int>& placements) const {
  voxelOffsets.clear();
  placements.clear();

  // the placement window: the translations the brute-force scan tried, the
  // piece's bounding box staying within the result's
  const int wx1 = asInt(res_.boundX1()) - asInt(rotation.boundX1());
  const int wx2 = asInt(res_.boundX2()) - asInt(rotation.boundX2());
  const int wy1 = asInt(res_.boundY1()) - asInt(rotation.boundY1());
  const int wy2 = asInt(res_.boundY2()) - asInt(rotation.boundY2());
  const int wz1 = asInt(res_.boundZ1()) - asInt(rotation.boundZ1());
  const int wz2 = asInt(res_.boundZ2()) - asInt(rotation.boundZ2());

  if ((wx2 < wx1) || (wy2 < wy1) || (wz2 < wz1)) {
    return;
  }

  const long sx = res_.getX();
  const long sy = res_.getY();

  // the filled voxels of the piece as result index offsets relative to the
  // translation, in the order the matrix nodes are made
  std::vector<unsigned char> voxelColor;
  std::vector<long> checkOffsets;  // colour-only checks for cells that are not filled
  std::vector<unsigned char> checkColor;

  // The anchor is the first filled voxel of the piece. Its coordinates can
  // not be recovered from its index offset (the piece's space may be bigger
  // than the result's), so they are recorded directly.
  int ax = 0;
  int ay = 0;
  int az = 0;

  for (unsigned int pz = rotation.boundZ1(); pz <= rotation.boundZ2(); pz++) {
    for (unsigned int py = rotation.boundY1(); py <= rotation.boundY2(); py++) {
      for (unsigned int px = rotation.boundX1(); px <= rotation.boundX2(); px++) {
        if (rotation.getState(px, py, pz) == Voxel::kFilled) {
          if (voxelOffsets.empty()) {
            ax = asInt(px);
            ay = asInt(py);
            az = asInt(pz);
          }
          voxelOffsets.push_back(static_cast<long>(px) + sx * (static_cast<long>(py) + sy * pz));
          voxelColor.push_back(static_cast<unsigned char>(rotation.getColor(px, py, pz)));
        } else if ((colorTableWidth_ > 1) && (rotation.getColor(px, py, pz) != 0)) {
          // a coloured cell that is not filled cannot occupy a result voxel,
          // but it still takes part in the colour check
          checkOffsets.push_back(static_cast<long>(px) + sx * (static_cast<long>(py) + sy * pz));
          checkColor.push_back(static_cast<unsigned char>(rotation.getColor(px, py, pz)));
        }
      }
    }
  }

  if (voxelOffsets.empty()) {
    return;
  }

  const long anchorOff = voxelOffsets[0];

  const std::size_t voxels = voxelOffsets.size();
  const std::size_t checks = checkOffsets.size();

  // whether the piece fits at the translation with result base index `base`;
  // the caller has tested the state of the anchor voxel
  auto fits = [&](long base, unsigned char anchorResColor) -> bool {
    if (colorTableWidth_ > 1 &&
        !colorAllowed_[voxelColor[0] * colorTableWidth_ + anchorResColor]) {
      return false;
    }

    for (std::size_t v = 1; v < voxels; v++) {
      const auto ri = static_cast<unsigned int>(base + voxelOffsets[v]);
      if (res_.getState(ri) == Voxel::kEmpty) {
        return false;
      }
      if (colorTableWidth_ > 1 &&
          !colorAllowed_[voxelColor[v] * colorTableWidth_ + res_.getColor(ri)]) {
        return false;
      }
    }

    for (std::size_t c = 0; c < checks; c++) {
      if (!colorAllowed_[checkColor[c] * colorTableWidth_ +
                         res_.getColor(static_cast<unsigned int>(base + checkOffsets[c]))]) {
        return false;
      }
    }

    return true;
  };

  const unsigned long windowVol = static_cast<unsigned long>(wx2 - wx1 + 1) *
                                  static_cast<unsigned long>(wy2 - wy1 + 1) *
                                  static_cast<unsigned long>(wz2 - wz1 + 1);

  if (resultVoxels_.size() < windowVol) {
    // the result is sparse within its bounding box: only try translations
    // that put the anchor onto one of its non-empty voxels
    for (const ResultVoxel& rv : resultVoxels_) {
      const int x = rv.x - ax;
      const int y = rv.y - ay;
      const int z = rv.z - az;

      if ((x < wx1) || (x > wx2) || (y < wy1) || (y > wy2) || (z < wz1) || (z > wz2)) {
        continue;
      }

      if (!rotation.onGrid(x, y, z)) {
        continue;
      }

      if (fits(static_cast<long>(rv.index) - anchorOff, rv.color)) {
        placements.push_back(x);
        placements.push_back(y);
        placements.push_back(z);
      }
    }

    // the result voxels go x fastest, the brute-force scan went x slowest:
    // restore that order so the matrix is built identically
    struct Triple {
      int x, y, z;
    };
    std::vector<Triple> t(placements.size() / 3);
    for (std::size_t i = 0; i < t.size(); i++) {
      t[i] = Triple{placements[3 * i], placements[3 * i + 1], placements[3 * i + 2]};
    }
    std::sort(t.begin(), t.end(), [](const Triple& a, const Triple& b) {
      if (a.x != b.x) {
        return a.x < b.x;
      }
      if (a.y != b.y) {
        return a.y < b.y;
      }
      return a.z < b.z;
    });
    for (std::size_t i = 0; i < t.size(); i++) {
      placements[3 * i] = t[i].x;
      placements[3 * i + 1] = t[i].y;
      placements[3 * i + 2] = t[i].z;
    }

  } else {
    // the result is mostly filled: walking the translation window directly
    // is cheaper than the voxel list and needs no sorting; the anchor read
    // rejects most translations with a single access
    for (int x = wx1; x <= wx2; x++) {
      for (int y = wy1; y <= wy2; y++) {
        for (int z = wz1; z <= wz2; z++) {
          const long base = x + sx * (y + sy * static_cast<long>(z));
          const long ri = base + anchorOff;

          if (res_.getState(static_cast<unsigned int>(ri)) == Voxel::kEmpty) {
            continue;
          }

          if (!rotation.onGrid(x, y, z)) {
            continue;
          }

          if (fits(base, static_cast<unsigned char>(res_.getColor(static_cast<unsigned int>(ri))))) {
            placements.push_back(x);
            placements.push_back(y);
            placements.push_back(z);
          }
        }
      }
    }
  }
}

}  // namespace bt
