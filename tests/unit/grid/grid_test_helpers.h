// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Helpers for the grid tests. Ported from test/test_helpers.h of the
/// original (the voxel part; the puzzle comparison comes with stage 2).

#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/voxel.h"

#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace bt::test {

/// Every grid kind, for cases that loop over all of them.
inline constexpr GridKind kAllGrids[] = {
    GridKind::Bricks,    GridKind::TriangularPrism, GridKind::Spheres,  GridKind::Rhombic,
    GridKind::TetraOcta, GridKind::Sliding,         GridKind::Stacking,
};

/// The five grids that have their own geometry (sliding and stacking use cubes).
inline constexpr GridKind kGeometryGrids[] = {
    GridKind::Bricks, GridKind::TriangularPrism, GridKind::Spheres,
    GridKind::Rhombic, GridKind::TetraOcta,
};

/// An empty voxel space of the given grid.
inline std::unique_ptr<Voxel> makeVoxel(const GridType& gt, unsigned int x, unsigned int y,
                                        unsigned int z) {
  return gt.makeVoxel(x, y, z, Voxel::kEmpty);
}

/// Coordinates inside a size x size x size box that the grid accepts.
inline std::vector<std::tuple<int, int, int>> validCoordinates(const Voxel& v, int size) {
  std::vector<std::tuple<int, int, int>> out;
  for (int z = 0; z < size; z++) {
    for (int y = 0; y < size; y++) {
      for (int x = 0; x < size; x++) {
        if (v.validCoordinate(x, y, z)) {
          out.emplace_back(x, y, z);
        }
      }
    }
  }
  return out;
}

/// A shape that uses only grid-legal coordinates, or nullptr when the box
/// does not offer enough valid cells.
inline std::unique_ptr<Voxel> legalShape(const GridType& gt, int size, unsigned int cells) {
  const auto usize = static_cast<unsigned int>(size);
  std::unique_ptr<Voxel> v = makeVoxel(gt, usize, usize, usize);
  const auto coords = validCoordinates(*v, size);
  if (coords.size() < cells) {
    return nullptr;
  }
  for (unsigned int i = 0; i < cells; i++) {
    auto [x, y, z] = coords[i];
    v->setState(static_cast<unsigned int>(x), static_cast<unsigned int>(y),
                static_cast<unsigned int>(z), Voxel::kFilled);
  }
  return v;
}

/// A box in which every grid offers enough valid coordinates (the
/// tetra-octa grid is the sparsest and sets the size).
inline constexpr int kLegalShapeBox = 6;

/// A voxel space from ASCII art: one vector per z layer, one string per y
/// row, one character per x. '#' is filled, '+' variable, anything else
/// empty. Ragged art is padded to the longest row.
inline std::unique_ptr<Voxel> fromLayers(const GridType& gt,
                                         std::initializer_list<std::vector<std::string>> layers) {
  const auto sz = static_cast<unsigned int>(layers.size());
  unsigned int sy = 0;
  unsigned int sx = 0;
  for (const auto& layer : layers) {
    sy = std::max(sy, static_cast<unsigned int>(layer.size()));
    for (const auto& row : layer) {
      sx = std::max(sx, static_cast<unsigned int>(row.size()));
    }
  }
  // a 0-sized space would have a wrapped-around bounding box
  if (sx == 0 || sy == 0 || sz == 0) {
    throw std::invalid_argument("fromLayers: the layer art is empty");
  }

  std::unique_ptr<Voxel> v = makeVoxel(gt, sx, sy, sz);
  unsigned int z = 0;
  for (const auto& layer : layers) {
    for (unsigned int y = 0; y < layer.size(); y++) {
      const std::string& row = layer[y];
      for (unsigned int x = 0; x < row.size(); x++) {
        if (row[x] == '#') {
          v->setState(x, y, z, Voxel::kFilled);
        } else if (row[x] == '+') {
          v->setState(x, y, z, Voxel::kVariable);
        }
      }
    }
    z++;
  }
  return v;
}

}  // namespace bt::test
