// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/stl.cpp (stlExporter_c::write), src/lib/stl_0.cpp
// (stlExporter_0_c: getMesh, getCubeMesh, getMinkowskiMesh and the
// parameters), src/lib/stl_2.cpp (stlExporter_2_c) and
// gridType_c::getStlExporter (src/lib/gridtype.cpp).
#include "shapelogic/mesh/stl.h"

#include "shapelogic/base/assert.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/mesh/cube_mesh.h"
#include "shapelogic/mesh/mink_mesh.h"
#include "shapelogic/mesh/modifiers.h"
#include "shapelogic/mesh/voxel_mesh.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace bt::mesh {

namespace {

constexpr double kEpsilon = 1.0e-5;

// Each recursion of the sphere grid's balls makes four times the
// triangles; at 6 and more the original's mesh asserts anyway (its
// dialog then said it was faulty), and at 10 it needs tens of gigabytes
// before it gets there. Not a limit of the original.
constexpr double kMaxRecursions = 6;

// stl_0: the cube grids, the prisms, the rhombic and tetra-octa grids
constexpr std::array<StlParameter, 6> kStl0 = {{
    {"Unit Size", StlParameterType::PositiveDouble, 10,
     "Basic unit size of the voxel, used for the x direction and as the default for the y and z "
     "direction"},
    {"Unit Size Y", StlParameterType::PositiveDouble, 0,
     "Unit size in the y direction, 0 means same as the basic unit size. Bevel and offset stretch "
     "along with a changed y unit"},
    {"Unit Size Z", StlParameterType::PositiveDouble, 0,
     "Unit size in the z direction, 0 means same as the basic unit size. Bevel and offset stretch "
     "along with a changed z unit"},
    {"Bevel", StlParameterType::PositiveDouble, 0.3, "Size of the bevel at the edges"},
    {"Offset", StlParameterType::PositiveDouble, 0.01,
     "By how much should faces be inset into the voxel"},
    {"Interior chamfers", StlParameterType::Switch, 1,
     "Chamfer the concave (interior) edges as well as the convex ones"},
}};

// stl_2: the sphere grid
constexpr std::array<StlParameter, 8> kStl2 = {{
    {"Sphere radius", StlParameterType::PositiveDouble, 10,
     "Base Radius of the spheres, defines how far apart they are"},
    {"Connection radius", StlParameterType::PositiveDouble, 0.75,
     "Radius of the connection cylinder between the spheres, 1 is the maximum, 0 means no "
     "connection"},
    {"Curvature radius", StlParameterType::PositiveDouble, 1.0,
     "Define the radius of the curve that creates the transition between sphere surface and "
     "connection cylinder 1 is maximum"},
    {"Offset", StlParameterType::PositiveDouble, 0,
     "The radius of the outer surface of the sphere is decreased by this value to make the "
     "spheres a bit smaller"},
    {"Recursions", StlParameterType::PositiveInteger, 2.0,
     "How many levels of recursion, a value of 3 or 4 is enough, be careful with bigger values "
     "as it will take ages to generate"},
    {"Inner radius", StlParameterType::PositiveDouble, 0,
     "The radius of the void inside the sphere, must be smaller than the sphere radius"},
    {"Hole diameter", StlParameterType::PositiveDouble, 0,
     "The radius of the holes inside the sphere wall that connect inside and outside"},
    {"Square Hole", StlParameterType::Switch, 0,
     "When tagged the holes will be square, otherwise round"},
}};

Result<Polyhedron> refuse(const char* why) { return fail(Errc::InvalidArgument, why); }

// stlExporter_0_c::getMesh
Result<Polyhedron> stl0Mesh(const Voxel& v, std::span<const double> p) {
  const double cubeScale = p[0];
  const double cubeScaleY = p[1];
  const double cubeScaleZ = p[2];
  const double bevel = p[3];
  const double shrink = p[4];
  const bool interiorChamfers = p[5] != 0;
  if (v.countState(Voxel::kVariable) != 0) {
    return refuse("Shapes with variable voxels cannot be exported");
  }
  if (cubeScale < kEpsilon) {
    return refuse("Unit size too small");
  }
  if (cubeScaleY < 0 || (cubeScaleY > 0 && cubeScaleY < kEpsilon)) {
    return refuse("Unit size y too small");
  }
  if (cubeScaleZ < 0 || (cubeScaleZ > 0 && cubeScaleZ < kEpsilon)) {
    return refuse("Unit size z too small");
  }
  if (shrink < 0) {
    return refuse("Offset cannot be negative");
  }
  if (bevel < 0) {
    return refuse("Bevel cannot be negative");
  }
  // the unit size in y and z direction defaults to the base unit size
  const double scaleY = (cubeScaleY > kEpsilon) ? cubeScaleY : cubeScale;
  const double scaleZ = (cubeScaleZ > kEpsilon) ? cubeScaleZ : cubeScale;

  const GridKind kind = v.getGridType().getType();
  Result<Polyhedron> poly = fail(Errc::InvalidArgument, "No mesher for this grid");
  if (kind == GridKind::Bricks || kind == GridKind::Sliding || kind == GridKind::Stacking) {
    poly = cubePolyhedron(v, shrink / cubeScale, bevel / cubeScale, interiorChamfers);
  } else if (minkHandles(v)) {
    poly = minkPolyhedron(v, shrink / cubeScale, bevel / cubeScale, interiorChamfers);
  }
  if (poly) {
    scalePolyhedron(*poly, static_cast<float>(cubeScale), static_cast<float>(scaleY),
                    static_cast<float>(scaleZ));
  }
  return poly;
}

// stlExporter_2_c::getMesh
Result<Polyhedron> stl2Mesh(const Voxel& v, std::span<const double> p) {
  const double sphereRad = p[0];
  const double connectionRad = p[1];
  const double round = p[2];
  const double offset = p[3];
  const double recursion = p[4];
  const double innerRad = p[5];
  const double holeDiam = p[6];
  const bool squareHole = p[7] != 0;
  if (v.countState(Voxel::kVariable) != 0) {
    return refuse("Shapes with variable voxels cannot be exported");
  }
  if (sphereRad < kEpsilon) {
    return refuse("Sphere size too small");
  }
  if (offset < 0) {
    return refuse("Offset cannot be negative");
  }
  if (round < 0) {
    return refuse("Curvature radius cannot be negative");
  }
  if (offset > sphereRad) {
    return refuse("Offset must be smaller than sphere radius");
  }
  if (round > 1) {
    return refuse("The curvature radius is relative and must be between 0 and 1");
  }
  if (connectionRad < 0 || connectionRad > 1) {
    return refuse("The connection radius is relative and must be between 0 and 1");
  }
  if (innerRad < 0 || innerRad >= sphereRad) {
    return refuse(
        "The inner radius cannot be negative and must be less than the sphere radius");
  }
  if (holeDiam < 0 || holeDiam > 0.333334 * sphereRad) {
    return refuse(
        "The hole diameter cannot be negative or greater than 1/3 of the sphere radius");
  }
  if (recursion > kMaxRecursions) {
    return refuse("More than 6 recursions make a mesh too large to build");
  }
  return sphereMesh(v, static_cast<float>(sphereRad), static_cast<float>(connectionRad),
                    static_cast<float>(round), static_cast<float>(offset),
                    static_cast<int>(recursion), static_cast<float>(innerRad),
                    static_cast<float>((squareHole ? -1 : 1) * holeDiam), false);
}

void put(std::vector<std::uint8_t>& out, const void* p, std::size_t n) {
  const auto* b = static_cast<const std::uint8_t*>(p);
  out.insert(out.end(), b, b + n);
}

// a line of three numbers after `prefix`, as the original's
// "%9.4e %9.4e %9.4e\n"
void putNumbers(std::vector<std::uint8_t>& out, const char* prefix, Vec3 p) {
  char buf[128];
  const int n = std::snprintf(buf, sizeof(buf), "%s%9.4e %9.4e %9.4e\n", prefix,
                              static_cast<double>(p.x), static_cast<double>(p.y),
                              static_cast<double>(p.z));
  BT_ASSERT(n > 0 && static_cast<std::size_t>(n) < sizeof(buf));
  put(out, buf, static_cast<std::size_t>(n));
}

void putText(std::vector<std::uint8_t>& out, std::string_view s) { put(out, s.data(), s.size()); }

}  // namespace

std::span<const StlParameter> stlParameters(GridKind grid) {
  if (grid == GridKind::Spheres) {
    return kStl2;
  }
  return kStl0;
}

Result<Polyhedron> stlMesh(const Voxel& v, std::span<const double> values) {
  const std::span<const StlParameter> params = stlParameters(v.getGridType().getType());
  if (values.size() != params.size()) {
    return fail(Errc::InvalidArgument, "Expected " + std::to_string(params.size()) +
                                           " STL parameters, got " +
                                           std::to_string(values.size()));
  }
  if (v.getGridType().getType() == GridKind::Spheres) {
    return stl2Mesh(v, values);
  }
  return stl0Mesh(v, values);
}

std::vector<std::uint8_t> stlFile(const Polyhedron& mesh, std::string_view title, bool binary) {
  std::vector<std::uint8_t> out;
  std::uint32_t triangleCount = 0;
  if (binary) {
    // the header: the title, padded with zeros (the original wrote 84
    // bytes of it and then the count over the last four)
    out.assign(84, 0);
    std::memcpy(out.data(), title.data(), std::min<std::size_t>(title.size(), 84));
  } else {
    putText(out, "solid ");
    putText(out, title);
    putText(out, "\n");
  }

  // connected coplanar faces merged and triangulated again with fewer,
  // larger triangles: the meshers keep every vertex of a face's outline
  const Polyhedron poly = mergeCoplanarFaces(mesh);

  for (int i = 0; i < poly.numFaces(); i++) {
    const int f = poly.faceAt(i);
    if (poly.face(f).hole) {
      continue;
    }
    Vec3 normal = poly.faceNormal(f);
    if (std::isnan(normal.x) || std::isnan(normal.y) || std::isnan(normal.z)) {
      normal = Vec3(0, 0, 0);
    }
    int e = poly.face(f).edge;
    const int sentinel = e;
    e = poly.he(e).next;
    const Vec3 start = poly.dstPos(e);
    e = poly.he(e).next;
    do {
      const Vec3 v1 = start;
      const Vec3 v2 = poly.dstPos(e);
      e = poly.he(e).next;
      const Vec3 v3 = poly.dstPos(e);
      if (binary) {
        for (const Vec3& p : {normal, v1, v2, v3}) {
          const float xyz[3] = {p.x, p.y, p.z};
          put(out, xyz, sizeof(xyz));
        }
        const std::uint16_t attribute = 0;
        put(out, &attribute, sizeof(attribute));
        triangleCount++;
      } else {
        putNumbers(out, "  facet normal ", normal);
        putText(out, "    outer loop\n");
        putNumbers(out, "      vertex ", v1);
        putNumbers(out, "      vertex ", v2);
        putNumbers(out, "      vertex ", v3);
        putText(out, "    endloop\n");
        putText(out, "  endfacet\n");
      }
    } while (e != sentinel);
  }

  if (binary) {
    // the triangle count, little endian, after the 80 header bytes
    for (std::size_t b = 0; b < 4; b++) {
      out[80 + b] = static_cast<std::uint8_t>((triangleCount >> (8 * b)) & 0xff);
    }
  } else {
    putText(out, "endsolid\n");
  }
  return out;
}

}  // namespace bt::mesh
