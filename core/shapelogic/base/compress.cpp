// SPDX-License-Identifier: GPL-3.0-or-later
#include "shapelogic/base/compress.h"

#include <algorithm>

#include <zstd.h>

namespace sl {

namespace {

struct CCtxFree {
  void operator()(ZSTD_CCtx* c) const { ZSTD_freeCCtx(c); }
};
struct DCtxFree {
  void operator()(ZSTD_DCtx* d) const { ZSTD_freeDCtx(d); }
};

}  // namespace

struct BlockCompressor::State {
  std::unique_ptr<ZSTD_CCtx, CCtxFree> ctx{ZSTD_createCCtx()};
};

BlockCompressor::BlockCompressor(int level) : state_(std::make_unique<State>()) {
  const int held = std::clamp(level, kMinCompression, kMaxCompression);
  if (state_->ctx) {
    // a checksum of each block's bytes, checked when it is decompressed:
    // four bytes a block, and a damaged file is told, not read as keys
    const std::size_t a =
        ZSTD_CCtx_setParameter(state_->ctx.get(), ZSTD_c_compressionLevel, held);
    const std::size_t b = ZSTD_CCtx_setParameter(state_->ctx.get(), ZSTD_c_checksumFlag, 1);
    if (ZSTD_isError(a) != 0 || ZSTD_isError(b) != 0) {
      state_->ctx.reset();
    }
  }
}

BlockCompressor::~BlockCompressor() = default;

bool BlockCompressor::compress(const void* src, std::size_t len, std::vector<std::uint8_t>& out) {
  if (!state_->ctx) {
    return false;
  }
  out.resize(ZSTD_compressBound(len));
  const std::size_t n = ZSTD_compress2(state_->ctx.get(), out.data(), out.size(), src, len);
  if (ZSTD_isError(n) != 0) {
    out.clear();
    return false;
  }
  out.resize(n);
  return true;
}

struct BlockDecompressor::State {
  std::unique_ptr<ZSTD_DCtx, DCtxFree> ctx{ZSTD_createDCtx()};
};

BlockDecompressor::BlockDecompressor() : state_(std::make_unique<State>()) {}

BlockDecompressor::~BlockDecompressor() = default;

bool BlockDecompressor::decompress(const void* src, std::size_t len, void* dst,
                                   std::size_t rawLen) {
  if (!state_->ctx) {
    return false;
  }
  const std::size_t n = ZSTD_decompressDCtx(state_->ctx.get(), dst, rawLen, src, len);
  return ZSTD_isError(n) == 0 && n == rawLen;
}

}  // namespace sl
