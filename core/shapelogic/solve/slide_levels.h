// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Sorted sets of arrangement keys for the full slide search, kept in a file
/// so that a search is limited by the disk, not by memory.
///
/// A set is written once, in rising order, as blocks of up to kBlockKeys
/// keys. A block holds how many keys it has and the gaps between them as
/// varints, so keys close together, as the arrangements of one search are,
/// take a few bytes each. The first key and the file offset of each block
/// make the index: in memory while the set is looked in, at the end of the
/// file otherwise, so that a set read only now and then costs no memory in
/// between. Every thread reads a file through one shared descriptor
/// (pread), so a search with many sets open does not run out of
/// descriptors; sorted parts of one set, made by several threads, are
/// joined block by block (appendSet).
///
/// Ported from src/lib/slidelevels.h; the blocks of any size, the last key
/// in the footer, the shared descriptor, seeking cursors and joining are new.

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

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace bt::sliding::levels {

/// Keys a block holds at most: few enough that one key looked up decodes
/// little, many enough that the index is small beside the keys.
inline constexpr unsigned int kBlockKeys = 128;
/// The longest varint of a 128-bit gap.
inline constexpr unsigned int kMaxVarint = 19;
/// Ends a file whose index follows its blocks.
inline constexpr char kSetMagic[8] = {'B', 'T', 'S', 'L', 'K', 'E', 'Y', '2'};

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

/// A file open for reading at any offset from any thread at once (pread;
/// on Windows ReadFile with an offset): one per set, shared by its readers.
class ReadFile {
 public:
  ReadFile() = default;
  ~ReadFile() { close(); }
  ReadFile(const ReadFile&) = delete;
  ReadFile& operator=(const ReadFile&) = delete;

  bool open(const std::filesystem::path& p) {
    close();
#ifdef _WIN32
    fd_ = _wopen(p.c_str(), _O_RDONLY | _O_BINARY);
#else
    fd_ = ::open(p.c_str(), O_RDONLY);
#endif
    return fd_ >= 0;
  }
  void close() {
    if (fd_ >= 0) {
#ifdef _WIN32
      _close(fd_);
#else
      ::close(fd_);
#endif
      fd_ = -1;
    }
  }
  [[nodiscard]] bool isOpen() const { return fd_ >= 0; }

  /// Reads len bytes at offset; false when they are not all there.
  bool readAt(std::uint64_t offset, void* buf, std::size_t len) const {
    if (fd_ < 0) {
      return false;
    }
    auto* out = static_cast<std::uint8_t*>(buf);
    while (len > 0) {
#ifdef _WIN32
      OVERLAPPED ov{};
      ov.Offset = static_cast<DWORD>(offset & 0xffffffffULL);
      ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
      DWORD got = 0;
      const DWORD want = static_cast<DWORD>(std::min<std::size_t>(len, 1U << 30));
      // ::ReadFile is Windows' (this class has its name)
      if (!::ReadFile(reinterpret_cast<HANDLE>(_get_osfhandle(fd_)), out, want, &got, &ov) ||
          got == 0) {
        return false;
      }
      const std::size_t n = got;
#else
      const ssize_t r = ::pread(fd_, out, len, static_cast<off_t>(offset));
      if (r <= 0) {
        return false;
      }
      const auto n = static_cast<std::size_t>(r);
#endif
      out += n;
      offset += n;
      len -= n;
    }
    return true;
  }

 private:
  int fd_ = -1;
};

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
    remove();
    path_ = p;
    count_ = 0;
    inBlock_ = 0;
    last_ = 0;
    dataBytes_ = 0;
    firsts_.clear();
    offsets_.assign(1, 0);
    mem_.clear();
    buf_.clear();
    indexed_ = true;
    if (!path_.empty()) {
      out_.reset(std::fopen(path_.string().c_str(), "wb"));
      if (!out_) {
        path_.clear();
        return false;
      }
    }
    return true;
  }

  /// Opens a set written earlier (a saved search's), its index read back;
  /// false when the file is not a whole set.
  bool open(const std::filesystem::path& p) {
    remove();
    path_ = p;
    indexed_ = false;
    if (!reload()) {
      path_.clear();
      return false;
    }
    return true;
  }

  /// Appends a key greater than the last one added.
  bool add(Key k) {
    if (inBlock_ == 0) {
      firsts_.push_back(k);
    } else {
      std::uint8_t v[kMaxVarint];
      const unsigned int n = putVarint(v, static_cast<Key>(k - last_));
      buf_.insert(buf_.end(), v, v + n);
    }
    last_ = k;
    count_++;
    if (++inBlock_ == kBlockKeys) {
      return flushBlock();
    }
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

  /// Appends a finished set whose keys are all greater than the last one
  /// added, block by block without decoding them.
  bool appendSet(const KeySet& part) {
    if (part.count_ == 0) {
      return true;
    }
    if (!part.indexed_ || !flushBlock()) {
      return false;
    }
    const std::uint64_t base = offsets_.back();
    const std::uint64_t partBytes = part.offsets_.back();
    if (out_) {
      if (part.path_.empty()) {
        if (std::fwrite(part.mem_.data(), 1, part.mem_.size(), out_.get()) != part.mem_.size()) {
          return false;
        }
      } else {
        std::vector<std::uint8_t> chunk(std::size_t{1} << 20);
        for (std::uint64_t at = 0; at < partBytes;) {
          const auto len =
              static_cast<std::size_t>(std::min<std::uint64_t>(chunk.size(), partBytes - at));
          if (!part.file_.readAt(at, chunk.data(), len) ||
              std::fwrite(chunk.data(), 1, len, out_.get()) != len) {
            return false;
          }
          at += len;
        }
      }
    } else {
      const std::size_t was = mem_.size();
      mem_.resize(was + static_cast<std::size_t>(partBytes));
      if (part.path_.empty()) {
        std::memcpy(mem_.data() + was, part.mem_.data(), part.mem_.size());
      } else if (!part.file_.readAt(0, mem_.data() + was, static_cast<std::size_t>(partBytes))) {
        return false;
      }
    }
    firsts_.insert(firsts_.end(), part.firsts_.begin(), part.firsts_.end());
    for (std::size_t b = 1; b < part.offsets_.size(); b++) {
      offsets_.push_back(base + part.offsets_[b]);
    }
    dataBytes_ = offsets_.back();
    count_ += part.count_;
    last_ = part.last_;
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
              std::fwrite(&last_, sizeof(Key), 1, out_.get()) == 1 &&
              std::fwrite(kSetMagic, sizeof(kSetMagic), 1, out_.get()) == 1;
    ok = std::fflush(out_.get()) == 0 && ok && std::ferror(out_.get()) == 0;
    out_.reset();
    return ok && file_.open(path_);
  }

  /// Frees the index of a set kept in a file, and closes the file; both
  /// come back when needed (reload).
  void release() {
    if (path_.empty() || out_) {
      return;
    }
    std::vector<Key>().swap(firsts_);
    std::vector<std::uint64_t>().swap(offsets_);
    file_.close();
    indexed_ = false;
  }

  /// Reads the index back after release(); false when the file is damaged.
  bool reload() {
    if (indexed_) {
      return true;
    }
    if (!file_.isOpen() && !file_.open(path_)) {
      return false;
    }
    std::uint64_t tail[3];
    Key last = 0;
    char magic[sizeof(kSetMagic)];
    std::error_code ec;
    const std::uint64_t size = std::filesystem::file_size(path_, ec);
    const std::uint64_t footer = sizeof(tail) + sizeof(Key) + sizeof(magic);
    if (ec || size < footer || !file_.readAt(size - footer, tail, sizeof(tail)) ||
        !file_.readAt(size - footer + sizeof(tail), &last, sizeof(Key)) ||
        !file_.readAt(size - sizeof(magic), magic, sizeof(magic)) ||
        std::memcmp(magic, kSetMagic, sizeof(magic)) != 0) {
      file_.close();
      return false;
    }
    const std::uint64_t blocks = tail[1];
    if (blocks > size / sizeof(std::uint64_t) ||
        tail[2] + blocks * sizeof(Key) + (blocks + 1) * sizeof(std::uint64_t) + footer != size) {
      file_.close();
      return false;
    }
    firsts_.resize(static_cast<std::size_t>(blocks));
    offsets_.resize(static_cast<std::size_t>(blocks + 1));
    if ((blocks != 0 && !file_.readAt(tail[2], firsts_.data(), blocks * sizeof(Key))) ||
        !file_.readAt(tail[2] + blocks * sizeof(Key), offsets_.data(),
                      (blocks + 1) * sizeof(std::uint64_t))) {
      file_.close();
      return false;
    }
    count_ = static_cast<std::size_t>(tail[0]);
    last_ = last;
    dataBytes_ = offsets_.back();
    indexed_ = true;
    return true;
  }

  [[nodiscard]] std::size_t size() const { return count_; }
  [[nodiscard]] bool inMemory() const { return path_.empty(); }
  [[nodiscard]] const std::filesystem::path& path() const { return path_; }
  /// Whether the index is in memory, so the set can be read.
  [[nodiscard]] bool indexed() const { return indexed_; }
  [[nodiscard]] std::size_t blocks() const { return firsts_.size(); }
  /// The first key of block b (the index must be in memory).
  [[nodiscard]] Key firstKey(std::size_t b) const { return firsts_[b]; }
  /// The greatest key; 0 for an empty set.
  [[nodiscard]] Key lastKey() const { return last_; }
  /// Bytes the keys and their index take on disk, or in memory for a set
  /// kept there.
  [[nodiscard]] std::uint64_t bytes() const {
    if (path_.empty()) {
      return dataBytes_;
    }
    const std::uint64_t blocks = offsets_.empty() ? 0 : offsets_.size() - 1;
    return dataBytes_ + blocks * (sizeof(Key) + sizeof(std::uint64_t)) + sizeof(std::uint64_t) +
           3 * sizeof(std::uint64_t) + sizeof(Key) + sizeof(kSetMagic);
  }
  /// Memory the block index takes while it is in memory.
  [[nodiscard]] std::uint64_t indexBytes() const {
    return firsts_.capacity() * sizeof(Key) + offsets_.capacity() * sizeof(std::uint64_t);
  }
  /// Memory the set takes: its keys when kept in memory, and its index.
  [[nodiscard]] std::uint64_t memoryBytes() const {
    return indexBytes() + (path_.empty() ? dataBytes_ + buf_.capacity() : 0);
  }

  /// Deletes the file, if there is one.
  void remove() {
    out_.reset();
    file_.close();
    if (!path_.empty()) {
      std::error_code ec;
      std::filesystem::remove(path_, ec);
      path_.clear();
    }
  }

  /// Forgets the file without deleting it (a saved search keeps it).
  void keepFile() {
    out_.reset();
    file_.close();
    path_.clear();
    std::vector<std::uint8_t>().swap(mem_);
    count_ = 0;
    firsts_.clear();
    offsets_.assign(1, 0);
  }

  /// Reads blocks of one set: one reader per thread. A read brings in
  /// `readAhead` bytes from the block on, so blocks read in order cost few
  /// reads.
  class Reader {
   public:
    explicit Reader(const KeySet& s, std::size_t readAhead = std::size_t{1} << 18)
        : set_(s), readAhead_(readAhead) {}
    [[nodiscard]] bool ok() const { return set_.path_.empty() || set_.file_.isOpen(); }

    /// The keys of block b, into keys; false on a read error.
    bool block(std::size_t b, std::vector<Key>& keys) {
      const std::uint64_t from = set_.offsets_[b];
      const auto len = static_cast<std::size_t>(set_.offsets_[b + 1] - from);
      const std::uint8_t* p = nullptr;
      if (set_.path_.empty()) {
        p = set_.mem_.data() + from;
      } else {
        if (from < bufAt_ || from + len > bufAt_ + buf_.size()) {
          const std::uint64_t end = set_.offsets_.back();
          const auto want = static_cast<std::size_t>(
              std::min<std::uint64_t>(std::max<std::size_t>(len, readAhead_), end - from));
          buf_.resize(want);
          if (want != 0 && !set_.file_.readAt(from, buf_.data(), want)) {
            buf_.clear();
            bufAt_ = 0;
            return false;
          }
          bufAt_ = from;
        }
        p = buf_.data() + (from - bufAt_);
      }
      unsigned int n = 0;
      p = getVarint(p, n);
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
    std::size_t readAhead_;
    std::vector<std::uint8_t> buf_;
    std::uint64_t bufAt_ = 0;
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

  /// Every key in rising order, one block at a time, from the first key
  /// not below `from` (the index must be in memory).
  class Cursor {
   public:
    explicit Cursor(const KeySet& s) : set_(s), reader_(s) {}
    Cursor(const KeySet& s, Key from) : set_(s), reader_(s) { seek(from); }

    /// Goes to the first key not below `from`.
    void seek(Key from) {
      keys_.clear();
      at_ = 0;
      blk_ = set_.firsts_.size();
      if (set_.count_ == 0 || from > set_.last_) {
        return;
      }
      blk_ = 0;
      if (from > set_.firsts_[0]) {
        blk_ = static_cast<std::size_t>(
                   std::upper_bound(set_.firsts_.begin(), set_.firsts_.end(), from) -
                   set_.firsts_.begin()) -
               1;
      }
      if (!reader_.block(blk_++, keys_)) {
        bad_ = true;
        keys_.clear();
        return;
      }
      at_ = static_cast<std::size_t>(std::lower_bound(keys_.begin(), keys_.end(), from) -
                                     keys_.begin());
    }

    /// The next key into k; false at the end or on a read error (failed()).
    bool next(Key& k) {
      if (at_ == keys_.size()) {
        if (blk_ >= set_.firsts_.size()) {
          return false;
        }
        if (!reader_.block(blk_++, keys_)) {
          bad_ = true;
          keys_.clear();
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
  // Writes the block being built, if there is one: its key count, then
  // the gaps.
  bool flushBlock() {
    if (inBlock_ == 0) {
      return true;
    }
    std::uint8_t hdr[kMaxVarint];
    const unsigned int n = putVarint(hdr, inBlock_);
    if (out_) {
      if (std::fwrite(hdr, 1, n, out_.get()) != n ||
          (!buf_.empty() && std::fwrite(buf_.data(), 1, buf_.size(), out_.get()) != buf_.size())) {
        return false;
      }
    } else {
      mem_.insert(mem_.end(), hdr, hdr + n);
      mem_.insert(mem_.end(), buf_.begin(), buf_.end());
    }
    offsets_.push_back(offsets_.back() + n + buf_.size());
    dataBytes_ = offsets_.back();
    buf_.clear();
    inBlock_ = 0;
    return true;
  }

  std::filesystem::path path_;
  FilePtr out_;
  ReadFile file_;
  std::vector<std::uint8_t> mem_;
  // The first key of each block, and where each block starts: offsets_ has
  // one more entry, the end of the last block.
  std::vector<Key> firsts_;
  std::vector<std::uint64_t> offsets_{0};
  bool indexed_ = true;
  std::size_t count_ = 0;
  std::uint64_t dataBytes_ = 0;
  // While writing: the gaps of the block being built, and its keys so far.
  std::vector<std::uint8_t> buf_;
  unsigned int inBlock_ = 0;
  // The greatest key added.
  Key last_ = 0;
};

}  // namespace bt::sliding::levels
