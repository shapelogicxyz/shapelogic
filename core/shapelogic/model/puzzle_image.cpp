// SPDX-License-Identifier: GPL-3.0-or-later
#include "shapelogic/model/puzzle_image.h"

#include <cstdint>

namespace sl {

namespace {

unsigned int byteAt(std::string_view b, std::size_t i) { return static_cast<unsigned char>(b[i]); }

unsigned int big16(std::string_view b, std::size_t i) { return (byteAt(b, i) << 8) | byteAt(b, i + 1); }

std::uint32_t big32(std::string_view b, std::size_t i) { return (big16(b, i) << 16) | big16(b, i + 2); }

std::optional<ImageInfo> png(std::string_view b) {
  static constexpr std::string_view kSignature = "\x89PNG\r\n\x1a\n";
  // the signature, then the IHDR chunk: length, name, width, height
  if (b.size() < 24 || b.substr(0, 8) != kSignature || b.substr(12, 4) != "IHDR") {
    return std::nullopt;
  }
  return ImageInfo{"image/png", big32(b, 16), big32(b, 20)};
}

std::optional<ImageInfo> jpeg(std::string_view b) {
  if (b.size() < 4 || byteAt(b, 0) != 0xFF || byteAt(b, 1) != 0xD8) {
    return std::nullopt;
  }
  // the segments up to the first frame header (SOF0..SOF15 but DHT, JPG
  // and DAC), which holds the height and the width
  std::size_t i = 2;
  while (i + 4 <= b.size()) {
    if (byteAt(b, i) != 0xFF) {
      return std::nullopt;
    }
    const unsigned int marker = byteAt(b, i + 1);
    if (marker == 0xFF) {  // fill byte
      i++;
      continue;
    }
    if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {  // no length
      i += 2;
      continue;
    }
    if (marker == 0xD9 || marker == 0xDA) {  // the end, or the scan, before a frame
      return std::nullopt;
    }
    const unsigned int length = big16(b, i + 2);
    if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
      if (length < 7 || i + 9 > b.size()) {
        return std::nullopt;
      }
      return ImageInfo{"image/jpeg", big16(b, i + 7), big16(b, i + 5)};
    }
    if (length < 2) {
      return std::nullopt;
    }
    i += 2 + length;
  }
  return std::nullopt;
}

}  // namespace

std::optional<ImageInfo> sniffImage(std::string_view bytes) {
  if (bytes.size() > kMaxImageBytes) {
    return std::nullopt;
  }
  std::optional<ImageInfo> info = png(bytes);
  if (!info) {
    info = jpeg(bytes);
  }
  if (!info || info->width == 0 || info->height == 0 || info->width > kMaxImageSide ||
      info->height > kMaxImageSide) {
    return std::nullopt;
  }
  return info;
}

std::string imageHash(std::string_view bytes) {
  std::uint64_t h = 14695981039346656037ULL;
  for (const char c : bytes) {
    h ^= static_cast<unsigned char>(c);
    h *= 1099511628211ULL;
  }
  static constexpr std::string_view kHex = "0123456789abcdef";
  std::string out(16, '0');
  for (std::size_t k = 0; k < 16; k++) {
    out[15 - k] = kHex[(h >> (4 * k)) & 15U];
  }
  return out;
}

}  // namespace sl
