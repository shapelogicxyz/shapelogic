// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A 3D vector of floats: the coordinates of a mesh.
/// Ported from src/halfedge/vector3.{h,cpp} (Vector3Df; only what the
/// meshes use).

#include <cmath>
#include <compare>

namespace bt::mesh {

/// A point or direction in mesh space.
struct Vec3 {
  float x = 0;
  float y = 0;
  float z = 0;

  constexpr Vec3() = default;
  constexpr Vec3(float px, float py, float pz) : x(px), y(py), z(pz) {}

  constexpr Vec3 operator+(const Vec3& v) const { return {x + v.x, y + v.y, z + v.z}; }
  constexpr Vec3 operator-(const Vec3& v) const { return {x - v.x, y - v.y, z - v.z}; }
  constexpr Vec3 operator-() const { return {-x, -y, -z}; }
  constexpr Vec3 operator*(float f) const { return {x * f, y * f, z * f}; }
  constexpr Vec3 operator/(float f) const { return {x / f, y / f, z / f}; }
  constexpr Vec3& operator+=(const Vec3& v) {
    x += v.x;
    y += v.y;
    z += v.z;
    return *this;
  }
  constexpr Vec3& operator-=(const Vec3& v) {
    x -= v.x;
    y -= v.y;
    z -= v.z;
    return *this;
  }
  constexpr Vec3& operator*=(float f) {
    x *= f;
    y *= f;
    z *= f;
    return *this;
  }
  constexpr Vec3& operator/=(float f) {
    x /= f;
    y /= f;
    z /= f;
    return *this;
  }

  /// The dot product (operator* of the original).
  [[nodiscard]] constexpr float dot(const Vec3& v) const {
    float sum = 0;
    sum += x * v.x;
    sum += y * v.y;
    sum += z * v.z;
    return sum;
  }
  /// The cross product (operator^ of the original).
  [[nodiscard]] constexpr Vec3 cross(const Vec3& v) const {
    return {(y * v.z) - (v.y * z), (z * v.x) - (v.z * x), (x * v.y) - (v.x * y)};
  }

  /// The length squared (summed in float, as the original did).
  [[nodiscard]] double squaredModule() const {
    float sum = 0;
    sum += x * x;
    sum += y * y;
    sum += z * z;
    return static_cast<double>(sum);
  }
  [[nodiscard]] double module() const { return std::sqrt(squaredModule()); }

  /// Scales to length 1 (dividing by the length rounded to float, as the
  /// original's operator/= did).
  void normalize() { *this /= static_cast<float>(module()); }

  /// The angle to v in radians (Vector3D::angle).
  [[nodiscard]] double angle(const Vec3& v) const;

  /// Exact lexicographic order (the original's operator<, used to find
  /// vertices that are the same point).
  constexpr auto operator<=>(const Vec3&) const = default;
};

constexpr Vec3 operator*(float f, const Vec3& v) { return v * f; }

}  // namespace bt::mesh
