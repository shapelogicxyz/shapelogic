// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The pictures of a puzzle (decision 0038): the image files, kept as they
/// came, that shapes show on their top faces (ShapeImage).

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace bt {

/// One picture. Never changed once added: shapes refer to it by `id`, and
/// undo steps and copies of the puzzle share its bytes.
struct PuzzleImage {
  unsigned int id = 0;
  /// "image/png" or "image/jpeg".
  std::string type;
  unsigned int width = 0;
  unsigned int height = 0;
  std::shared_ptr<const std::string> bytes;
  /// imageHash() of the bytes, worked out once.
  std::string hash;
};

/// What sniffImage() finds in an image file.
struct ImageInfo {
  std::string type;
  unsigned int width = 0;
  unsigned int height = 0;
};

/// The largest picture a puzzle takes, in bytes and in pixels per side. The
/// app shrinks pictures to 2048 pixels before it adds them.
inline constexpr std::size_t kMaxImageBytes = std::size_t{32} << 20;
inline constexpr unsigned int kMaxImageSide = 8192;

/// The type and pixel size of PNG or JPEG bytes; nothing for anything else
/// (or a picture over the limits).
[[nodiscard]] std::optional<ImageInfo> sniffImage(std::string_view bytes);

/// A hash of the bytes as 16 hex digits (FNV-1a, 64 bits), for the app's
/// cache of decoded pictures.
[[nodiscard]] std::string imageHash(std::string_view bytes);

}  // namespace bt
