// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Blocks of bytes compressed with zstd, each block on its own, for files
/// read in pieces: the slide search's level files (decision 0040). The
/// library is seen only by compress.cpp.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace bt {

/// The levels a BlockCompressor takes: 1 (fastest) to 19 (smallest).
inline constexpr int kMinCompression = 1;
inline constexpr int kMaxCompression = 19;

/// Compresses blocks at one level, each with a checksum of its bytes. It
/// keeps its working memory between blocks, so one is made per writer; not
/// for several threads at once.
class BlockCompressor {
 public:
  /// `level` is held to kMinCompression..kMaxCompression.
  explicit BlockCompressor(int level);
  ~BlockCompressor();
  BlockCompressor(const BlockCompressor&) = delete;
  BlockCompressor& operator=(const BlockCompressor&) = delete;

  /// Compresses the `len` bytes at `src` into `out` (resized to what they
  /// came to); false when they could not be.
  bool compress(const void* src, std::size_t len, std::vector<std::uint8_t>& out);

 private:
  struct State;
  std::unique_ptr<State> state_;
};

/// Decompresses blocks a BlockCompressor made. One per reader; not for
/// several threads at once.
class BlockDecompressor {
 public:
  BlockDecompressor();
  ~BlockDecompressor();
  BlockDecompressor(const BlockDecompressor&) = delete;
  BlockDecompressor& operator=(const BlockDecompressor&) = delete;

  /// Decompresses the `len` bytes at `src` into the `rawLen` bytes at
  /// `dst`; false when they are damaged (the checksum tells) or come to
  /// another length.
  bool decompress(const void* src, std::size_t len, void* dst, std::size_t rawLen);

 private:
  struct State;
  std::unique_ptr<State> state_;
};

}  // namespace bt
