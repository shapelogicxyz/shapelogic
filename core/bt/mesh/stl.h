// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The STL export: each grid's mesher parameters (names, types, defaults
/// and tooltips as the original's export dialog showed them), the STL mesh
/// of a shape for given parameter values, and the STL file, binary or
/// text, of that mesh with its coplanar faces merged.
/// Ported from src/lib/stl.{h,cpp} (stlExporter_c::write), stl_0.{h,cpp}
/// (the cube, prism, rhombic and tetra-octa grids), stl_2.{h,cpp} (the
/// sphere grid) and gridType_c::getStlExporter. The file is made in
/// memory: where it goes is the caller's business.

#include "bt/base/error.h"
#include "bt/grid/grid_type.h"
#include "bt/mesh/polyhedron.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace bt {
class Voxel;
}

namespace bt::mesh {

/// How a parameter is entered (stlExporter_c::parameterTypes).
enum class StlParameterType : std::uint8_t {
  Double,           ///< any number
  PositiveDouble,   ///< a number, 0 or more
  PositiveInteger,  ///< a whole number, 0 or more
  Switch,           ///< on (1) or off (0)
};

/// One parameter of a grid's STL mesher, as the export dialog shows it.
struct StlParameter {
  std::string_view name;
  StlParameterType type;
  double defaultValue;
  std::string_view tooltip;  ///< the original's text, without its padding spaces
};

/// The parameters of the grid's STL mesher, in the original's order (the
/// values passed to stlMesh follow it): the cube grids, the prisms, the
/// rhombic and the tetra-octa grids share stl_0's six, the sphere grid has
/// stl_2's eight.
[[nodiscard]] std::span<const StlParameter> stlParameters(GridKind grid);

/// The STL mesh of a shape (stlExporter_c::getMesh) for the given values,
/// one per parameter of its grid, in mesh units scaled by the unit size.
/// Refused (InvalidArgument) with the original's message for a shape with
/// variable cells or a value out of range, or when the mesher fails.
[[nodiscard]] Result<Polyhedron> stlMesh(const Voxel& v, std::span<const double> values);

/// The STL file of a mesh (stlExporter_c::write after getMesh): its
/// coplanar faces merged, each face written as a fan of triangles. Binary:
/// the 80 header bytes hold `title` (cut at 80, padded with zeros), then
/// the triangle count and the triangles; text: "solid <title>" and the
/// facets in the original's number format. The original's title was the
/// file's name.
[[nodiscard]] std::vector<std::uint8_t> stlFile(const Polyhedron& mesh, std::string_view title,
                                                bool binary);

}  // namespace bt::mesh
