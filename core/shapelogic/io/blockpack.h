// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Compressing large files on every core, in the spirit of pigz: the data
/// is cut into chunks that are packed and unpacked independently and in
/// parallel, a few at a time, so memory stays bounded however large the
/// file. Used for a stacking search's saved files in a .slsolve export
/// (bt/io/paused_solve.h).
///
/// A file of sorted 16-byte keys (a search level) packs far smaller as the
/// gaps between keys: each key is written as its difference from the one
/// before, as a variable-length number, and that is deflated. Anything else
/// is just deflated.
///
/// Packed form, after a "BTZ1" signature and the file's size (u64): chunks,
/// each [kind u8][raw bytes u64][encoded bytes u64][packed bytes u64][data].
/// Kind 0 is deflated bytes, kind 1 deflated key gaps. Little-endian. Byte
/// for byte the original's.
///
/// Ported from src/lib/blockpack.{h,cpp}; its threads are a ThreadPool's
/// (decision 0004).

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>

namespace bt {
class ThreadPool;
}  // namespace bt

namespace bt::io::blockpack {

/// Packs `file` into `out`. With `keys`, the file is read as sorted 16-byte
/// keys (a chunk that is not falls back to plain deflate). The chunks are
/// packed on up to `threads` threads of `pool` (null: a pool of the
/// call's own; threads 0: every hardware thread, or all of the pool's).
/// chunkBytes 0 for 64 MB; others, a multiple of 16, are for the tests.
/// Returns the bytes written, or 0 on an error.
std::uint64_t pack(const std::filesystem::path& file, std::ostream& out, bool keys,
                   unsigned int threads = 0, std::size_t chunkBytes = 0,
                   ThreadPool* pool = nullptr);

/// Unpacks `bytes` bytes of packed data from `in` into `file`. False on
/// damaged data or a write error.
bool unpack(std::istream& in, std::uint64_t bytes, const std::filesystem::path& file,
            unsigned int threads = 0, ThreadPool* pool = nullptr);

}  // namespace bt::io::blockpack
