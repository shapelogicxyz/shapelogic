// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Sorted sets of arrangement keys for the full slide search, kept in a file
/// so that a search is limited by the disk, not by memory.
///
/// A set is written once, in rising order, as blocks of kBlockKeys keys. A
/// block holds the gaps between its keys as varints, so keys close
/// together, as the arrangements of one search are, take a few bytes each.
/// The first key and the file offset of each block make the index: in
/// memory while the set is looked in, at the end of the file otherwise, so
/// that a set read only now and then costs no memory in between.
///
/// Ported from src/lib/slidelevels.h.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace bt::sliding::levels {

/// Keys a block holds: few enough that one key looked up decodes little,
/// many enough that the index is small beside the keys.
inline constexpr unsigned int kBlockKeys = 128;
/// The longest varint of a 128-bit gap.
inline constexpr unsigned int kMaxVarint = 19;
/// Ends a file whose index follows its blocks.
inline constexpr char kSetMagic[8] = {'B', 'T', 'S', 'L', 'K', 'E', 'Y', '1'};

/// Writes v as a varint (7 bits a byte, low first); returns its length.
template <class Key>
inline unsigned int putVarint(std::uint8_t* out, Key v) {
  unsigned int n = 0;
  while (v >= 0x80) {
    out[n++] = static_cast<std::uint8_t>(static_cast<unsigned int>(v & 0x7f) | 0x80U);
    v >>= 7;
  }
  out[n++] = static_cast<std::uint8_t>(v);
  return n;
}

/// Reads a varint into v; returns the byte after it.
template <class Key>
inline const std::uint8_t* getVarint(const std::uint8_t* p, Key& v) {
  v = 0;
  unsigned int shift = 0;
  while ((*p & 0x80) != 0) {
    v |= static_cast<Key>(*p++ & 0x7f) << shift;
    shift += 7;
  }
  v |= static_cast<Key>(*p++) << shift;
  return p;
}

/// Reads len bytes at offset; false when they are not all there.
inline bool readAt(std::FILE* f, std::uint64_t offset, void* buf, std::size_t len) {
#ifdef _WIN32
  if (_fseeki64(f, static_cast<long long>(offset), SEEK_SET) != 0) {
    return false;
  }
#else
  if (fseeko(f, static_cast<off_t>(offset), SEEK_SET) != 0) {
    return false;
  }
#endif
  return std::fread(buf, 1, len, f) == len;
}

/// Closes a FILE.
struct FileCloser {
  void operator()(std::FILE* f) const {
    if (f != nullptr) {
      std::fclose(f);
    }
  }
};
using FilePtr = std::unique_ptr<std::FILE, FileCloser>;

/// One sorted set of unique keys, in a file or, with no file name, in memory.
template <class Key>
class KeySet {
 public:
  KeySet() = default;
  KeySet(const KeySet&) = delete;
  KeySet& operator=(const KeySet&) = delete;
  ~KeySet() { remove(); }

  /// Starts writing; an empty path keeps the set in memory.
  bool create(const std::filesystem::path& p) {
    path_ = p;
    count_ = 0;
    firsts_.clear();
    offsets_.assign(1, 0);
    mem_.clear();
    buf_.clear();
    if (!path_.empty()) {
      out_.reset(std::fopen(path_.string().c_str(), "wb"));
      if (!out_) {
        return false;
      }
    }
    return true;
  }

  /// Appends a key greater than the last one added.
  bool add(Key k) {
    if (count_ % kBlockKeys == 0) {
      if (!flushBlock()) {
        return false;
      }
      firsts_.push_back(k);
    } else {
      std::uint8_t v[kMaxVarint];
      const unsigned int n = putVarint(v, static_cast<Key>(k - last_));
      buf_.insert(buf_.end(), v, v + n);
    }
    last_ = k;
    count_++;
    return true;
  }

  /// Appends a sorted run of keys.
  template <class It>
  bool addAll(It b, It e) {
    for (; b != e; ++b) {
      if (!add(*b)) {
        return false;
      }
    }
    return true;
  }

  /// Done writing: the set can be read from now on. A file gets its index
  /// written after the blocks.
  bool finish() {
    if (!flushBlock()) {
      return false;
    }
    firsts_.shrink_to_fit();
    offsets_.shrink_to_fit();
    mem_.shrink_to_fit();
    std::vector<std::uint8_t>().swap(buf_);
    if (!out_) {
      return true;
    }
    const std::uint64_t blocks = firsts_.size();
    const std::uint64_t tail[3] = {static_cast<std::uint64_t>(count_), blocks, offsets_.back()};
    bool ok = (blocks == 0 ||
               std::fwrite(firsts_.data(), sizeof(Key), blocks, out_.get()) == blocks) &&
              std::fwrite(offsets_.data(), sizeof(std::uint64_t), blocks + 1, out_.get()) ==
                  blocks + 1 &&
              std::fwrite(tail, sizeof(tail), 1, out_.get()) == 1 &&
              std::fwrite(kSetMagic, sizeof(kSetMagic), 1, out_.get()) == 1;
    ok = std::fflush(out_.get()) == 0 && ok && std::ferror(out_.get()) == 0;
    out_.reset();
    return ok;
  }

  /// Frees the index of a set kept in a file; it is read back when needed.
  void release() {
    if (path_.empty() || out_) {
      return;
    }
    std::vector<Key>().swap(firsts_);
    std::vector<std::uint64_t>().swap(offsets_);
    indexed_ = false;
  }

  /// Reads the index back after release(); false when the file is damaged.
  bool reload() {
    if (indexed_) {
      return true;
    }
    const FilePtr in(std::fopen(path_.string().c_str(), "rb"));
    if (!in) {
      return false;
    }
    std::uint64_t tail[3];
    char magic[sizeof(kSetMagic)];
    std::error_code ec;
    const std::uint64_t size = std::filesystem::file_size(path_, ec);
    const std::uint64_t footer = sizeof(tail) + sizeof(magic);
    if (ec || size < footer || !readAt(in.get(), size - footer, tail, sizeof(tail)) ||
        std::fread(magic, sizeof(magic), 1, in.get()) != 1 ||
        std::memcmp(magic, kSetMagic, sizeof(magic)) != 0) {
      return false;
    }
    const std::uint64_t blocks = tail[1];
    if (tail[2] + blocks * sizeof(Key) + (blocks + 1) * sizeof(std::uint64_t) + footer != size) {
      return false;
    }
    firsts_.resize(blocks);
    offsets_.resize(blocks + 1);
    if ((blocks != 0 && !readAt(in.get(), tail[2], firsts_.data(), blocks * sizeof(Key))) ||
        std::fread(offsets_.data(), sizeof(std::uint64_t), blocks + 1, in.get()) != blocks + 1) {
      return false;
    }
    count_ = tail[0];
    indexed_ = true;
    return true;
  }

  [[nodiscard]] std::size_t size() const { return count_; }
  [[nodiscard]] bool inMemory() const { return path_.empty(); }
  /// Bytes the keys and their index take on disk, or in memory for a set
  /// kept there.
  [[nodiscard]] std::uint64_t bytes() const {
    if (path_.empty()) {
      return dataBytes_;
    }
    return dataBytes_ + blockCount_ * (sizeof(Key) + sizeof(std::uint64_t)) +
           sizeof(std::uint64_t) + 3 * sizeof(std::uint64_t) + sizeof(kSetMagic);
  }
  /// Memory the block index takes while it is in memory.
  [[nodiscard]] std::uint64_t indexBytes() const {
    return firsts_.capacity() * sizeof(Key) + offsets_.capacity() * sizeof(std::uint64_t);
  }

  /// Deletes the file, if there is one.
  void remove() {
    out_.reset();
    if (!path_.empty()) {
      std::error_code ec;
      std::filesystem::remove(path_, ec);
      path_.clear();
    }
  }

  /// Reads blocks of one set: one reader per thread.
  class Reader {
   public:
    explicit Reader(const KeySet& s) : set_(s) {
      if (!set_.path_.empty()) {
        in_.reset(std::fopen(set_.path_.string().c_str(), "rb"));
      }
    }
    [[nodiscard]] bool ok() const { return set_.path_.empty() || in_ != nullptr; }

    /// The keys of block b, into keys; false on a read error.
    bool block(std::size_t b, std::vector<Key>& keys) {
      const std::uint64_t from = set_.offsets_[b];
      const auto len = static_cast<std::size_t>(set_.offsets_[b + 1] - from);
      const std::uint8_t* p = nullptr;
      if (set_.path_.empty()) {
        p = set_.mem_.data() + from;
      } else {
        raw_.resize(len + 1);
        if (len != 0 && (!in_ || !readAt(in_.get(), from, raw_.data(), len))) {
          return false;
        }
        p = raw_.data();
      }
      const std::size_t n = std::min<std::size_t>(kBlockKeys, set_.count_ - b * kBlockKeys);
      keys.resize(n);
      Key k = set_.firsts_[b];
      keys[0] = k;
      for (std::size_t i = 1; i < n; i++) {
        Key gap = 0;
        p = getVarint(p, gap);
        k += gap;
        keys[i] = k;
      }
      return true;
    }

   private:
    const KeySet& set_;
    FilePtr in_;
    std::vector<std::uint8_t> raw_;
  };

  /// All keys, in order, into keys; false on a read error.
  bool load(std::vector<Key>& keys) {
    keys.clear();
    if (!reload()) {
      return false;
    }
    keys.reserve(count_);
    Reader r(*this);
    std::vector<Key> blk;
    for (std::size_t b = 0; b < firsts_.size(); b++) {
      if (!r.block(b, blk)) {
        return false;
      }
      keys.insert(keys.end(), blk.begin(), blk.end());
    }
    return r.ok();
  }

  /// Every key in rising order, one block at a time.
  class Cursor {
   public:
    explicit Cursor(const KeySet& s) : set_(s), reader_(s) {}
    /// The next key into k; false at the end or on a read error (failed()).
    bool next(Key& k) {
      if (at_ == keys_.size()) {
        if (blk_ >= set_.firsts_.size()) {
          return false;
        }
        if (!reader_.block(blk_++, keys_)) {
          bad_ = true;
          return false;
        }
        at_ = 0;
      }
      k = keys_[at_++];
      return true;
    }
    [[nodiscard]] bool failed() const { return bad_ || !reader_.ok(); }

   private:
    const KeySet& set_;
    Reader reader_;
    std::vector<Key> keys_;
    std::size_t blk_ = 0;
    std::size_t at_ = 0;
    bool bad_ = false;
  };

  /// Sets found[i] for each of the sorted keys cand[0..n) that is in this
  /// set. False on a read error. The index must be in memory.
  bool find(const Key* cand, std::size_t n, char* found, Reader& reader) const {
    std::vector<Key> keys;
    auto loaded = static_cast<std::size_t>(-1);
    std::size_t b = 0;
    const std::size_t blocks = firsts_.size();
    for (std::size_t i = 0; i < n; i++) {
      const Key c = cand[i];
      if (blocks == 0 || c < firsts_[0] || c > last_) {
        continue;
      }
      // The block c would be in: the last whose first key is not above it.
      // Keys rise, so look on from the last one, in doubling steps.
      std::size_t step = 1;
      std::size_t hi = b + 1;
      while (hi < blocks && firsts_[hi] <= c) {
        b = hi;
        hi = b + step;
        step *= 2;
      }
      b = static_cast<std::size_t>(
              std::upper_bound(firsts_.begin() + static_cast<std::ptrdiff_t>(b),
                               firsts_.begin() + static_cast<std::ptrdiff_t>(std::min(hi, blocks)),
                               c) -
              firsts_.begin()) -
          1;
      if (b != loaded) {
        if (!reader.block(b, keys)) {
          return false;
        }
        loaded = b;
      }
      if (std::binary_search(keys.begin(), keys.end(), c)) {
        found[i] = 1;
      }
    }
    return true;
  }

 private:
  // Writes the block being built, if there is one: one has been started
  // (firsts_) that is not yet written (offsets_).
  bool flushBlock() {
    if (firsts_.size() < offsets_.size()) {
      return true;
    }
    if (out_) {
      if (!buf_.empty() && std::fwrite(buf_.data(), 1, buf_.size(), out_.get()) != buf_.size()) {
        return false;
      }
    } else {
      mem_.insert(mem_.end(), buf_.begin(), buf_.end());
    }
    offsets_.push_back(offsets_.back() + buf_.size());
    dataBytes_ = offsets_.back();
    blockCount_ = offsets_.size() - 1;
    buf_.clear();
    return true;
  }

  std::filesystem::path path_;
  FilePtr out_;
  std::vector<std::uint8_t> mem_;
  // The first key of each block, and where each block starts: offsets_ has
  // one more entry, the end of the last block.
  std::vector<Key> firsts_;
  std::vector<std::uint64_t> offsets_{0};
  bool indexed_ = true;
  std::size_t count_ = 0;
  std::uint64_t dataBytes_ = 0;
  std::uint64_t blockCount_ = 0;
  // While writing: the block being built. After, the greatest key.
  std::vector<std::uint8_t> buf_;
  Key last_ = 0;
};

}  // namespace bt::sliding::levels
