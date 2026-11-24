// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The border of a sliding puzzle's tray (decision 0046): whether a board
/// shows one, and how it looks. The border is the tray's body around its
/// floor: a rim round the board's rectangle and the cells that are no
/// floor. The views draw it; nothing here changes what the puzzle is.

#include "shapelogic/grid/shape_image.h"

#include <cstdint>

namespace bt {

/// The outer corners of a tray's border.
enum class BorderCorners : std::uint8_t { Round, Sharp };

/// What a tray's border is made of: its surface.
enum class BorderMaterial : std::uint8_t { Smooth, Textured, Wood };

/// The file's and the API's name of a border's corners.
[[nodiscard]] inline const char* borderCornersName(BorderCorners c) {
  return c == BorderCorners::Sharp ? "sharp" : "round";
}

/// The file's and the API's name of a border's material.
[[nodiscard]] inline const char* borderMaterialName(BorderMaterial m) {
  switch (m) {
    case BorderMaterial::Textured:
      return "textured";
    case BorderMaterial::Wood:
      return "wood";
    case BorderMaterial::Smooth:
      break;
  }
  return "smooth";
}

/// The thickest border, in voxels.
inline constexpr unsigned int kMaxBorderThickness = 10;

/// A board's border. The look is kept while the border is off, so turning
/// it on again shows what was chosen.
struct TrayBorder {
  bool enabled = false;
  BorderCorners corners = BorderCorners::Round;
  BorderMaterial material = BorderMaterial::Smooth;
  /// The border's own color, 0xRRGGBB, or -1 for none: the board's color
  /// then, or the wood's for the wood material.
  int color = -1;
  /// How wide the rim round the board's rectangle is: 0 for thin (a
  /// little over a quarter of a cell, as the Play tab has always drawn
  /// it), else that many voxels, up to kMaxBorderThickness.
  unsigned int thickness = 0;
  /// Pictures over the whole tray, the rim included, shown on the border
  /// only, the bottom one first (decision 0047). Their `src` and `turn`
  /// count (the turn follows the cells as a shape's picture does); `box`
  /// is the board's rectangle when they were applied, and the views lay
  /// them over the tray as it is now.
  ShapeImages images;

  /// Whether anything differs from a board without a border.
  [[nodiscard]] bool isDefault() const { return *this == TrayBorder{}; }
  bool operator==(const TrayBorder&) const = default;
};

}  // namespace bt
