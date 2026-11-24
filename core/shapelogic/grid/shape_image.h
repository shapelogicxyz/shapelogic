// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A picture on the top faces of a shape, for sliding puzzles (decision
/// 0038): which of the puzzle's images, which part of it, and how it lies on
/// the shape's cells. It moves, turns and flips with the cells.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace bt {

/// How a picture was fitted to the shape's rectangle when it was applied:
/// cut to the rectangle's proportions, squashed to fill it, or sized and
/// placed by hand. Kept so the dialog opens with the same choice;
/// `ShapeImage::src` says what is shown.
enum class ImageFit : std::uint8_t { Crop, Stretch, Manual };

/// The file's and the API's name of a fit.
[[nodiscard]] inline const char* imageFitName(ImageFit f) {
  switch (f) {
    case ImageFit::Stretch:
      return "stretch";
    case ImageFit::Manual:
      return "manual";
    case ImageFit::Crop:
      break;
  }
  return "crop";
}

/// The picture of a shape. Seen from above (+Z), the part `src` of the
/// picture is laid over the cell rectangle `box`, turned by `turn`:
///
///   q = M(turn) * (u - 1/2, 1/2 - v),  point = box corner + (q + 1/2) * box size
///
/// for (u, v) in [0, 1]^2 across `src`, u to the right and v down the
/// picture. With turn 0 the picture's top is at the high-y side.
struct ShapeImage {
  /// The puzzle's image (Puzzle::findImage), -1 for none.
  int id = -1;
  ImageFit fit = ImageFit::Crop;
  /// x, y, width and height of the part shown, in pixels of the picture
  /// (x to the right and y down from its top left corner). With the Manual
  /// fit it may reach past the picture's edges (decision 0047): the
  /// picture then covers only part of the box, and the shape's color shows
  /// round it; it always overlaps the picture.
  std::array<int, 4> src{0, 0, 1, 1};
  /// x, y, width and height of the rectangle covered, in cells of the
  /// shape's own space (may reach outside it after a nudge).
  std::array<int, 4> box{0, 0, 1, 1};
  /// 0..7: (turn & 3) quarter turns counter-clockwise, after a mirror in x
  /// when turn >= 4.
  unsigned int turn = 0;

  [[nodiscard]] bool present() const { return id >= 0; }
  bool operator==(const ShapeImage&) const = default;

  /// The cells moved by (dx, dy).
  void translate(int dx, int dy) {
    box[0] += dx;
    box[1] += dy;
  }

  /// The cells turned or mirrored in the plane: cell c goes to
  /// a * c + (shx, shy), `a` row major with one entry of +-1 per row.
  void transform(const std::array<int, 4>& a, int shx, int shy) {
    // a cell [c, c + 1] lands on [a c + s, a c + s + 1]: as a point map
    // that is p -> a p + s, plus 1 on the axes the row reverses
    const int dx = a[0] + a[1] < 0 ? 1 : 0;
    const int dy = a[2] + a[3] < 0 ? 1 : 0;
    const int x0 = box[0];
    const int y0 = box[1];
    const int x1 = box[0] + box[2];
    const int y1 = box[1] + box[3];
    const int ax0 = a[0] * x0 + a[1] * y0 + shx + dx;
    const int ay0 = a[2] * x0 + a[3] * y0 + shy + dy;
    const int ax1 = a[0] * x1 + a[1] * y1 + shx + dx;
    const int ay1 = a[2] * x1 + a[3] * y1 + shy + dy;
    box = {ax0 < ax1 ? ax0 : ax1, ay0 < ay1 ? ay0 : ay1, std::abs(ax1 - ax0), std::abs(ay1 - ay0)};
    turn = turnOf(multiply(a, matrix(turn)));
  }

  /// The cells scaled up `by` times (each cell becomes by x by cells).
  void scale(int by) {
    for (int& v : box) {
      v *= by;
    }
  }

  /// The cells scaled down `by` times.
  void scaleDown(int by) {
    box = {box[0] / by, box[1] / by, (box[2] + by - 1) / by, (box[3] + by - 1) / by};
  }

  /// The 2x2 matrix of turn `t`, row major.
  [[nodiscard]] static std::array<int, 4> matrix(unsigned int t) {
    std::array<int, 4> m = t >= 4 ? std::array<int, 4>{-1, 0, 0, 1} : std::array<int, 4>{1, 0, 0, 1};
    for (unsigned int k = 0; k < (t & 3U); k++) {
      m = multiply({0, -1, 1, 0}, m);
    }
    return m;
  }

  /// The turn whose matrix is `m` (one of the eight).
  [[nodiscard]] static unsigned int turnOf(const std::array<int, 4>& m) {
    for (unsigned int t = 0; t < 8; t++) {
      if (matrix(t) == m) {
        return t;
      }
    }
    return 0;
  }

 private:
  static std::array<int, 4> multiply(const std::array<int, 4>& a, const std::array<int, 4>& b) {
    return {a[0] * b[0] + a[1] * b[2], a[0] * b[1] + a[1] * b[3], a[2] * b[0] + a[3] * b[2],
            a[2] * b[1] + a[3] * b[3]};
  }
};

/// The pictures of a shape, the bottom one first (decision 0047): each
/// lies over the same box with the same turn, the later ones on top. Every
/// entry is present; an empty stack is no picture.
using ShapeImages = std::vector<ShapeImage>;

/// The most pictures a shape's stack holds.
inline constexpr std::size_t kMaxShapeImages = 16;

/// Whether the part `src` shown of a picture `width` by `height` pixels
/// overlaps it: has a size, and at least one of its pixels inside it.
[[nodiscard]] inline bool overlapsPicture(const std::array<int, 4>& src, long long width,
                                          long long height) {
  const long long x = src[0];
  const long long y = src[1];
  return src[2] > 0 && src[3] > 0 && x < width && y < height && x + src[2] > 0 && y + src[3] > 0;
}

/// The bottom picture of a stack, or none.
[[nodiscard]] inline ShapeImage bottomImage(const ShapeImages& images) {
  return images.empty() ? ShapeImage{} : images.front();
}

}  // namespace bt
