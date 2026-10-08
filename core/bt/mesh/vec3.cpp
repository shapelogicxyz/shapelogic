// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/halfedge/vector3.cpp (Vector3D<float>::angle).
#include "bt/mesh/vec3.h"

namespace bt::mesh {

double Vec3::angle(const Vec3& v) const {
  Vec3 local(*this);
  Vec3 param(v);
  local /= static_cast<float>(module());
  param /= static_cast<float>(v.module());
  double dp = static_cast<double>(local.dot(param));
  // clamped to [-1, 1], for sanity
  if (dp > 1.0) {
    dp = 1.0;
  } else if (dp < -1.0) {
    dp = -1.0;
  }
  return std::acos(dp);
}

}  // namespace bt::mesh
