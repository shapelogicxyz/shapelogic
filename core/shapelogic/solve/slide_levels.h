// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Sorted sets of arrangement keys for the full slide search, kept in a file
/// so that a search is limited by the disk, not by memory.
///
/// A set is written once, in rising order, as blocks of up to kBlockKeys
/// keys. A block holds how many keys it has and the gaps between them as
/// varints, so keys close together, as the arrangements of one search are,
/// take a few bytes each. The first key and the offset of each block make
/// the index: in memory while the set is looked in, at the end of the
/// file otherwise, so that a set read only now and then costs no memory in
/// between. Every thread reads a file through one shared descriptor
/// (pread), so a search with many sets open does not run out of
/// descriptors; sorted parts of one set, made by several threads, are
/// joined without decoding them (appendSet).
///
/// In a file the blocks stand in chunks of about FileOptions::chunkBytes,
/// each compressed on its own with zstd (decision 0040): the gaps of a
/// level still repeat themselves, and a third of the file goes. A block's
/// offset counts the bytes before it as they were before compressing, and
/// a table of the chunks says where each begins in the file, so a block is
/// still read by itself, at the cost of decompressing its chunk. A set in
/// memory keeps its blocks as they are.
///
/// A file of the versions before the chunks (up to 1.9: the blocks as they
/// are, then the index) is read too, as a file whose chunks were all left as
/// they are, so a search saved by one of those is carried on; appendOlder
/// writes such a file again as this version writes its own (decision 0041).
///
/// Ported from src/lib/slidelevels.h; the blocks of any size, the last key
/// in the footer, the shared descriptor, seeking cursors, joining and the
/// compressed chunks are new.

#include "shapelogic/base/compress.h"
#include "shapelogic/base/system.h"
#include "shapelogic/io/gzip_file.h"
#include "shapelogic/solve/sliding_search.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

// <windows.h> stays in base/system.cpp (readFileAt): included here it
// reached the tests, whose earlier headers break its RPC part
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace sl::sliding::levels {

/// Keys a block holds at most: few enough that one key looked up decodes
/// little, many enough that the index is small beside the keys.
inline constexpr unsigned int kBlockKeys = 128;
/// The longest varint of a gap between two keys: 7 bits a byte (19 bytes
/// for a 128-bit key).
template <class Key>
inline constexpr unsigned int kMaxVarint = (8 * sizeof(Key) + 6) / 7;
/// The most bytes a block takes: its key count, then a gap for each key but
/// the first.
template <class Key>
inline constexpr std::size_t kMaxBlockBytes =
    kMaxVarint<unsigned int> + std::size_t{kBlockKeys - 1} * kMaxVarint<Key>;
/// The most bytes of blocks a chunk of a file may say it holds: many times
/// what any is written with (kChunkBytes and a block more). A damaged
/// index is not given room for more.
inline constexpr std::uint64_t kMaxChunkRawBytes = std::uint64_t{1} << 26;
/// Ends a file whose index follows its chunks of blocks.
inline constexpr char kSetMagic[8] = {'B', 'T', 'S', 'L', 'K', 'E', 'Y', '3'};
/// Ends a file of the versions up to 1.9: its blocks as they are, then the
/// index (the first keys, the offsets, three numbers, the last key).
inline constexpr char kOlderSetMagic[8] = {'B', 'T', 'S', 'L', 'K', 'E', 'Y', '2'};
/// Bytes of blocks a chunk holds before it is compressed and written: large
/// enough that zstd finds what repeats, and a set read from end to end is
/// read so.
inline constexpr std::size_t kChunkBytes = std::size_t{1} << 18;
/// The same for a set whose keys are looked up a few at a time (the levels
/// of a search with nested slides): a key looked up decompresses its whole
/// chunk, so the chunks are small, for a few points less off the file.
inline constexpr std::size_t kLookupChunkBytes = std::size_t{1} << 14;
/// The zstd level of a set that is read once and deleted (a run of a level
/// being made); a set that is kept (a level of the search) has the search's
/// level, kSlideCompression unless an A/B switch says otherwise.
inline constexpr int kRunCompression = 1;

/// Why a set's file could not be opened (KeySet::fault).
enum class SetFault : std::uint8_t {
  None,
  Unread,   ///< the system would not open or read it; KeySet::faultError says why
  Damaged,  ///< read, and not a whole set
  Newer,    ///< a set of a later version, whose files this one does not know
};

/// The tests' full disk: counts the bytes the sets it is given to write,
/// and fails the write that takes them past `failAt`, and every write
/// after it, or with `once` only that one (a disk that fills while a level
/// is made, with room again once the level's files are gone).
struct WriteGate {
  std::uint64_t failAt = 0;
  bool once = false;
  std::atomic<std::uint64_t> written{0};
  std::atomic<bool> failed{false};

  /// Whether `n` more bytes may be written.
  bool allow(std::uint64_t n) {
    const std::uint64_t now = written.fetch_add(n, std::memory_order_relaxed) + n;
    if (failAt == 0 || now <= failAt) {
      return true;
    }
    // with once, only the first write past the mark fails
    return failed.exchange(true) && once;
  }
};

/// How a set's file is written.
struct FileOptions {
  /// The zstd level of its chunks; 0 writes them as they are.
  int compression = kSlideCompression;
  /// Bytes of blocks to a chunk.
  std::size_t chunkBytes = kChunkBytes;
  /// The tests' full disk; null for none.
  WriteGate* gate = nullptr;
};

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

/// Reads a varint into v from the bytes before `end`; returns the byte
/// after it, or null when it does not end before `end` or within the
/// bytes a Key's varint has (kMaxVarint). The bytes may be a damaged
/// file's: nothing is read past `end`, and no shift reaches the key's
/// width (cleanup item 16.3).
template <class Key>
inline const std::uint8_t* getVarint(const std::uint8_t* p, const std::uint8_t* end, Key& v) {
  v = 0;
  if (static_cast<std::size_t>(end - p) > kMaxVarint<Key>) {
    end = p + kMaxVarint<Key>;
  }
  unsigned int shift = 0;
  while (p != end) {
    const std::uint8_t byte = *p++;
    v |= static_cast<Key>(byte & 0x7fU) << shift;
    if ((byte & 0x80U) == 0) {
      return p;
    }
    shift += 7;
  }
  return nullptr;
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
    return readFileAt(fd_, offset, buf, len);
  }

 private:
  int fd_ = -1;
};

/// Closes a FILE.
struct FileCloser {
  void operator()(std::FILE* f) const {
    if (f != nullptr) {
      // the FILE is owned by the unique_ptr this closes it for:
      // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
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

  /// Starts writing; an empty path keeps the set in memory (`options` are
  /// a file's).
  bool create(const std::filesystem::path& p, const FileOptions& options = {}) {
    remove();
    path_ = p;
    options_ = options;
    count_ = 0;
    inBlock_ = 0;
    last_ = 0;
    dataBytes_ = 0;
    fileSize_ = 0;
    firsts_.clear();
    offsets_.assign(1, 0);
    chunkRaw_.assign(1, 0);
    chunkAt_.assign(1, 0);
    mem_.clear();
    buf_.clear();
    chunk_.clear();
    comp_.reset();
    indexed_ = true;
    older_ = false;
    fault_ = SetFault::None;
    faultError_ = 0;
    if (!path_.empty()) {
      if (options_.compression > 0) {
        comp_ = std::make_unique<BlockCompressor>(options_.compression);
      }
      // the FILE is owned by the unique_ptr this closes it for:
      // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
      out_.reset(std::fopen(path_.string().c_str(), "wb"));
      // NOLINTNEXTLINE(clang-analyzer-unix.Stream): out_ closes it (FileCloser)
      if (!out_) {
        path_.clear();
        return false;
      }
    }
    return true;
  }

  /// Opens a set written earlier (a saved search's), its index read back;
  /// false when the file could not be read or is not a whole set (fault
  /// says which). The file stays as it is either way.
  bool open(const std::filesystem::path& p) {
    remove();
    path_ = p;
    indexed_ = false;
    older_ = false;
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
      std::uint8_t v[kMaxVarint<Key>];
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
  /// added, without decoding its blocks: a file's chunks go into a file as
  /// they are, whatever they were compressed with.
  bool appendSet(const KeySet& part) {
    if (part.count_ == 0) {
      return true;
    }
    if (!part.indexed_ || !flushBlock()) {
      return false;
    }
    const std::uint64_t base = offsets_.back();
    const std::uint64_t partBytes = part.offsets_.back();
    if (out_ && part.path_.empty()) {
      // blocks from memory: into chunks as if they had been added here
      for (std::size_t b = 0; b + 1 < part.offsets_.size(); b++) {
        chunk_.insert(chunk_.end(),
                      part.mem_.begin() + static_cast<std::ptrdiff_t>(part.offsets_[b]),
                      part.mem_.begin() + static_cast<std::ptrdiff_t>(part.offsets_[b + 1]));
        firsts_.push_back(part.firsts_[b]);
        offsets_.push_back(base + part.offsets_[b + 1]);
        if (chunk_.size() >= options_.chunkBytes && !flushChunk()) {
          return false;
        }
      }
      dataBytes_ = offsets_.back();
      count_ += part.count_;
      last_ = part.last_;
      return true;
    }
    if (out_ && part.older_) {
      return appendOlder(part, {});
    }
    if (out_) {
      // a file's chunks, as they are
      if (!flushChunk()) {
        return false;
      }
      const std::uint64_t fileBytes = part.chunkAt_.back();
      std::vector<std::uint8_t> piece(std::size_t{1} << 20);
      for (std::uint64_t at = 0; at < fileBytes;) {
        const auto len =
            static_cast<std::size_t>(std::min<std::uint64_t>(piece.size(), fileBytes - at));
        if (!part.file_.readAt(at, piece.data(), len) || !put(piece.data(), len)) {
          return false;
        }
        at += len;
      }
      const std::uint64_t fileBase = chunkAt_.back();
      for (std::size_t c = 1; c < part.chunkRaw_.size(); c++) {
        chunkRaw_.push_back(base + part.chunkRaw_[c]);
        chunkAt_.push_back(fileBase + part.chunkAt_[c]);
      }
    } else {
      const std::size_t was = mem_.size();
      mem_.resize(was + static_cast<std::size_t>(partBytes));
      if (part.path_.empty()) {
        std::memcpy(mem_.data() + was, part.mem_.data(), part.mem_.size());
      } else {
        BlockDecompressor dec;
        std::vector<std::uint8_t> scratch;
        for (std::size_t c = 0; c + 1 < part.chunkRaw_.size(); c++) {
          if (!part.readChunk(c, mem_.data() + was + part.chunkRaw_[c], dec, scratch)) {
            return false;
          }
        }
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

  /// Appends a finished set kept in a file of an earlier version (older())
  /// to this set's file, its blocks gathered into chunks and compressed as
  /// this file's are; its keys are all greater than the last one added.
  /// `step`, when set, hears the bytes read each time and ends the work by
  /// answering false.
  bool appendOlder(const KeySet& part, const std::function<bool(std::uint64_t)>& step) {
    if (part.count_ == 0) {
      return true;
    }
    if (!out_ || !part.indexed_ || part.path_.empty() || !flushBlock()) {
      return false;
    }
    const std::uint64_t base = offsets_.back();
    std::vector<std::uint8_t> piece;
    std::size_t b = 0;
    for (std::size_t c = 0; c + 1 < part.chunkRaw_.size(); c++) {
      const std::uint64_t end = part.chunkRaw_[c + 1];
      const auto len = static_cast<std::size_t>(end - part.chunkRaw_[c]);
      piece.resize(len);
      if (!part.file_.readAt(part.chunkAt_[c], piece.data(), len)) {
        return false;
      }
      std::size_t at = 0;
      for (; b + 1 < part.offsets_.size() && part.offsets_[b + 1] <= end; b++) {
        const auto blockLen = static_cast<std::size_t>(part.offsets_[b + 1] - part.offsets_[b]);
        chunk_.insert(chunk_.end(), piece.begin() + static_cast<std::ptrdiff_t>(at),
                      piece.begin() + static_cast<std::ptrdiff_t>(at + blockLen));
        at += blockLen;
        firsts_.push_back(part.firsts_[b]);
        offsets_.push_back(base + part.offsets_[b + 1]);
        if (chunk_.size() >= options_.chunkBytes && !flushChunk()) {
          return false;
        }
      }
      dataBytes_ = offsets_.back();
      if (step && !step(len)) {
        return false;
      }
    }
    dataBytes_ = offsets_.back();
    count_ += part.count_;
    last_ = part.last_;
    return true;
  }

  /// Done writing: the set can be read from now on. A file gets its index
  /// written after the chunks.
  bool finish() {
    if (!flushBlock() || !flushChunk()) {
      return false;
    }
    firsts_.shrink_to_fit();
    offsets_.shrink_to_fit();
    chunkRaw_.shrink_to_fit();
    chunkAt_.shrink_to_fit();
    mem_.shrink_to_fit();
    std::vector<std::uint8_t>().swap(buf_);
    std::vector<std::uint8_t>().swap(chunk_);
    std::vector<std::uint8_t>().swap(packed_);
    comp_.reset();
    if (!out_) {
      return true;
    }
    const std::uint64_t blocks = firsts_.size();
    const std::uint64_t chunks = chunkRaw_.size() - 1;
    const Tail tail = {static_cast<std::uint64_t>(count_), blocks, chunks, offsets_.back(),
                       chunkAt_.back()};
    bool ok = (blocks == 0 || put(firsts_.data(), blocks * sizeof(Key))) &&
              put(offsets_.data(), (blocks + 1) * sizeof(std::uint64_t)) &&
              put(chunkRaw_.data(), (chunks + 1) * sizeof(std::uint64_t)) &&
              put(chunkAt_.data(), (chunks + 1) * sizeof(std::uint64_t)) &&
              put(&tail, sizeof(tail)) && put(&last_, sizeof(Key)) &&
              put(kSetMagic, sizeof(kSetMagic));
    ok = std::fflush(out_.get()) == 0 && ok && std::ferror(out_.get()) == 0;
    // synced plainly before a state.bin may name it: that file's Full sync
    // then covers this one (cleanup item 4.1); and the close is checked,
    // since some file systems report a lost write only then
#ifdef _WIN32
    const int fd = _fileno(out_.get());
#else
    const int fd = fileno(out_.get());
#endif
    ok = ok && io::syncDescriptor(fd, io::Sync::Plain);
    // the FILE is owned by out_, released here to close it once:
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    ok = std::fclose(out_.release()) == 0 && ok;
    fileSize_ = chunkAt_.back() + indexFileBytes(blocks, chunks) + kFooterBytes;
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
    std::vector<std::uint64_t>().swap(chunkRaw_);
    std::vector<std::uint64_t>().swap(chunkAt_);
    file_.close();
    indexed_ = false;
  }

  /// Reads the index back after release(); false when the file could not be
  /// read or is not a whole set (fault says which).
  bool reload() {
    if (indexed_) {
      return true;
    }
    fault_ = SetFault::None;
    faultError_ = 0;
    errno = 0;
    if (!file_.isOpen() && !file_.open(path_)) {
      return faulty(SetFault::Unread);
    }
    char magic[sizeof(kSetMagic)];
    std::error_code ec;
    const std::uint64_t size = std::filesystem::file_size(path_, ec);
    if (ec) {
      faultError_ = ec.value();
      return faulty(SetFault::Unread);
    }
    if (size < sizeof(magic)) {
      return faulty(SetFault::Damaged);
    }
    if (!file_.readAt(size - sizeof(magic), magic, sizeof(magic))) {
      return faulty(SetFault::Unread);
    }
    if (std::memcmp(magic, kOlderSetMagic, sizeof(magic)) == 0) {
      return reloadOlder(size);
    }
    if (std::memcmp(magic, kSetMagic, sizeof(magic)) != 0) {
      // the same name with a later number: a later version's
      const bool later = std::memcmp(magic, kSetMagic, sizeof(magic) - 1) == 0 &&
                         magic[sizeof(magic) - 1] > kSetMagic[sizeof(magic) - 1];
      return faulty(later ? SetFault::Newer : SetFault::Damaged);
    }
    Tail tail{};
    Key last = 0;
    if (size < kFooterBytes) {
      return faulty(SetFault::Damaged);
    }
    if (!file_.readAt(size - kFooterBytes, &tail, sizeof(tail)) ||
        !file_.readAt(size - kFooterBytes + sizeof(tail), &last, sizeof(Key))) {
      return faulty(SetFault::Unread);
    }
    const std::uint64_t blocks = tail.blocks;
    const std::uint64_t chunks = tail.chunks;
    // (the chunks' bytes first: a number past the file could make the sum
    // below come round to the file's size)
    if (tail.fileBytes > size || blocks > size / sizeof(std::uint64_t) ||
        chunks > size / sizeof(std::uint64_t) ||
        tail.fileBytes + indexFileBytes(blocks, chunks) + kFooterBytes != size) {
      return faulty(SetFault::Damaged);
    }
    firsts_.resize(static_cast<std::size_t>(blocks));
    offsets_.resize(static_cast<std::size_t>(blocks + 1));
    chunkRaw_.resize(static_cast<std::size_t>(chunks + 1));
    chunkAt_.resize(static_cast<std::size_t>(chunks + 1));
    std::uint64_t at = tail.fileBytes;
    bool ok = blocks == 0 || file_.readAt(at, firsts_.data(), blocks * sizeof(Key));
    at += blocks * sizeof(Key);
    ok = ok && file_.readAt(at, offsets_.data(), (blocks + 1) * sizeof(std::uint64_t));
    at += (blocks + 1) * sizeof(std::uint64_t);
    ok = ok && file_.readAt(at, chunkRaw_.data(), (chunks + 1) * sizeof(std::uint64_t));
    at += (chunks + 1) * sizeof(std::uint64_t);
    ok = ok && file_.readAt(at, chunkAt_.data(), (chunks + 1) * sizeof(std::uint64_t));
    if (!ok) {
      return faulty(SetFault::Unread);
    }
    // the tables end where the footer says the blocks and the chunks do,
    // and are a set's all the way there
    if (offsets_.back() != tail.rawBytes || chunkRaw_.back() != tail.rawBytes ||
        chunkAt_.back() != tail.fileBytes || !tablesHold(tail.count, last)) {
      return faulty(SetFault::Damaged);
    }
    count_ = static_cast<std::size_t>(tail.count);
    last_ = last;
    dataBytes_ = tail.rawBytes;
    fileSize_ = size;
    older_ = false;
    indexed_ = true;
    return true;
  }

  /// Why the last open or reload failed.
  [[nodiscard]] SetFault fault() const { return fault_; }
  /// The system's error number of a file that could not be read; 0 when it
  /// gave none.
  [[nodiscard]] int faultError() const { return faultError_; }
  /// Whether the set is kept in a file of an earlier version, which is read
  /// as it is (its blocks are not compressed).
  [[nodiscard]] bool older() const { return older_; }

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
  /// Bytes the set takes where it is: its file (what is written of one
  /// still being written), or its blocks in memory.
  [[nodiscard]] std::uint64_t bytes() const {
    if (path_.empty()) {
      return dataBytes_;
    }
    return out_ ? chunkAt_.back() + chunk_.size() : fileSize_;
  }
  /// Bytes its blocks take as they are, before a file compresses them: what
  /// the set would take in memory.
  [[nodiscard]] std::uint64_t rawBytes() const { return dataBytes_; }
  /// Memory the index takes while it is in memory.
  [[nodiscard]] std::uint64_t indexBytes() const {
    return firsts_.capacity() * sizeof(Key) +
           (offsets_.capacity() + chunkRaw_.capacity() + chunkAt_.capacity()) *
               sizeof(std::uint64_t);
  }
  /// Memory the set takes: its keys when kept in memory, and its index.
  [[nodiscard]] std::uint64_t memoryBytes() const {
    return indexBytes() + (path_.empty() ? dataBytes_ : chunk_.capacity()) + buf_.capacity();
  }

  /// Deletes the file, if there is one.
  void remove() {
    out_.reset();
    comp_.reset();
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
    comp_.reset();
    file_.close();
    path_.clear();
    older_ = false;
    std::vector<std::uint8_t>().swap(mem_);
    std::vector<std::uint8_t>().swap(chunk_);
    count_ = 0;
    dataBytes_ = 0;
    fileSize_ = 0;
    firsts_.clear();
    offsets_.assign(1, 0);
    chunkRaw_.assign(1, 0);
    chunkAt_.assign(1, 0);
  }

  /// Reads blocks of one set: one reader per thread. A read of a file
  /// brings in `readAhead` bytes from the block's chunk on, so blocks read
  /// in order cost few reads; the chunk is decompressed and kept, for the
  /// blocks after it.
  class Reader {
   public:
    explicit Reader(const KeySet& s, std::size_t readAhead = std::size_t{1} << 18)
        : set_(s), readAhead_(readAhead) {}
    [[nodiscard]] bool ok() const { return set_.path_.empty() || set_.file_.isOpen(); }

    /// The keys of block b, into keys; false on a read error and on a block
    /// that is not one (a damaged file).
    bool block(std::size_t b, std::vector<Key>& keys) {
      const std::uint64_t from = set_.offsets_[b];
      const auto len = static_cast<std::size_t>(set_.offsets_[b + 1] - from);
      const std::uint8_t* p = nullptr;
      if (set_.path_.empty()) {
        p = set_.mem_.data() + from;
      } else {
        if ((from < bufAt_ || from + len > bufAt_ + buf_.size()) && !chunkOf(from)) {
          buf_.clear();
          bufAt_ = 0;
          return false;
        }
        p = buf_.data() + (from - bufAt_);
      }
      // A block is its key count, then the gaps, and ends where the next
      // begins. Bytes that say otherwise are a damaged file's: a chunk left
      // as it is (zstd could not shrink it, a file written without
      // compression, every file of the versions up to 1.9) has no checksum
      // to tell (cleanup item 16.3). The count and the end are checked
      // once a block, and each varint is held to the block's end.
      const std::uint8_t* const end = p + len;
      unsigned int n = 0;
      p = getVarint(p, end, n);
      if (p == nullptr || n == 0 || n > kBlockKeys) {
        return false;
      }
      keys.resize(n);
      Key k = set_.firsts_[b];
      keys[0] = k;
      for (std::size_t i = 1; i < n; i++) {
        Key gap = 0;
        p = getVarint(p, end, gap);
        if (p == nullptr) {
          return false;
        }
        k += gap;
        keys[i] = k;
      }
      return p == end;
    }

   private:
    // Brings in the chunk that holds the block at `from`: the one after
    // the chunk at hand when the blocks are read in order.
    bool chunkOf(std::uint64_t from) {
      const std::vector<std::uint64_t>& raw = set_.chunkRaw_;
      const std::size_t chunks = raw.size() - 1;
      std::size_t c = chunk_ + 1;
      if (buf_.empty() || c >= chunks || from < raw[c] || from >= raw[c + 1]) {
        c = static_cast<std::size_t>(std::upper_bound(raw.begin(), raw.end(), from) -
                                     raw.begin()) -
            1;
      }
      if (c >= chunks) {
        return false;
      }
      const std::uint64_t at = set_.chunkAt_[c];
      const auto packed = static_cast<std::size_t>(set_.chunkAt_[c + 1] - at);
      const auto rawLen = static_cast<std::size_t>(raw[c + 1] - raw[c]);
      if (at < aheadAt_ || at + packed > aheadAt_ + ahead_.size()) {
        const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(
            std::max<std::size_t>(packed, readAhead_), set_.chunkAt_.back() - at));
        ahead_.resize(want);
        if (want != 0 && !set_.file_.readAt(at, ahead_.data(), want)) {
          ahead_.clear();
          aheadAt_ = 0;
          return false;
        }
        aheadAt_ = at;
      }
      const std::uint8_t* src = ahead_.data() + (at - aheadAt_);
      buf_.resize(rawLen);
      if (packed == rawLen) {
        // a chunk that zstd could not shrink is written as it is
        std::memcpy(buf_.data(), src, rawLen);
      } else {
        if (!dec_) {
          dec_ = std::make_unique<BlockDecompressor>();
        }
        if (!dec_->decompress(src, packed, buf_.data(), rawLen)) {
          return false;
        }
      }
      bufAt_ = raw[c];
      chunk_ = c;
      return true;
    }

    const KeySet& set_;
    std::size_t readAhead_;
    // The chunk at hand, decompressed, and where its blocks begin.
    std::vector<std::uint8_t> buf_;
    std::uint64_t bufAt_ = 0;
    std::size_t chunk_ = 0;
    // Bytes of the file read ahead, and where they begin.
    std::vector<std::uint8_t> ahead_;
    std::uint64_t aheadAt_ = 0;
    std::unique_ptr<BlockDecompressor> dec_;
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
  // The footer's numbers: keys, blocks, chunks, the bytes of the blocks as
  // they are, and the bytes the chunks take in the file.
  struct Tail {
    std::uint64_t count;
    std::uint64_t blocks;
    std::uint64_t chunks;
    std::uint64_t rawBytes;
    std::uint64_t fileBytes;
  };
  static constexpr std::uint64_t kFooterBytes = sizeof(Tail) + sizeof(Key) + sizeof(kSetMagic);
  // Bytes the index of so many blocks and chunks takes in a file.
  static std::uint64_t indexFileBytes(std::uint64_t blocks, std::uint64_t chunks) {
    return blocks * sizeof(Key) + (blocks + 1) * sizeof(std::uint64_t) +
           2 * (chunks + 1) * sizeof(std::uint64_t);
  }

  // Whether the tables read from a file are a set's (cleanup item 16.3; a
  // file's index was trusted, and a reader followed a damaged one out of
  // its buffers): the blocks follow each other from 0, none empty or
  // longer than a block can be, each inside one chunk; the chunks follow
  // each other from 0, in the blocks' bytes and in the file's, none taking
  // more room in the file than its blocks' bytes (it is written as it is
  // when compressing gains nothing) or more of those than any chunk has;
  // the first keys rise up to the last key; and the keys are as many as
  // the blocks can hold. Once a file, in the time its tables take to read:
  // nothing here looks at a key.
  [[nodiscard]] bool tablesHold(std::uint64_t count, Key last) const {
    const std::size_t blocks = firsts_.size();
    const std::size_t chunks = chunkRaw_.size() - 1;
    if (offsets_.size() != blocks + 1 || chunkAt_.size() != chunks + 1 || offsets_.front() != 0 ||
        chunkRaw_.front() != 0 || chunkAt_.front() != 0) {
      return false;
    }
    if (count < blocks || count > std::uint64_t{blocks} * kBlockKeys) {
      return false;
    }
    for (std::size_t c = 0; c < chunks; c++) {
      if (chunkRaw_[c + 1] <= chunkRaw_[c] || chunkAt_[c + 1] <= chunkAt_[c] ||
          chunkRaw_[c + 1] - chunkRaw_[c] > kMaxChunkRawBytes ||
          chunkAt_[c + 1] - chunkAt_[c] > chunkRaw_[c + 1] - chunkRaw_[c]) {
        return false;
      }
    }
    std::size_t c = 0;
    for (std::size_t b = 0; b < blocks; b++) {
      const std::uint64_t from = offsets_[b];
      const std::uint64_t to = offsets_[b + 1];
      if (to <= from || to - from > kMaxBlockBytes<Key>) {
        return false;
      }
      if (b != 0 && !(firsts_[b - 1] < firsts_[b])) {
        return false;
      }
      // the blocks and the chunks both run upward: the chunk is looked for
      // from the last block's on
      while (c < chunks && chunkRaw_[c + 1] <= from) {
        c++;
      }
      if (c == chunks || from < chunkRaw_[c] || to > chunkRaw_[c + 1]) {
        return false;
      }
    }
    return blocks == 0 || !(last < firsts_.back());
  }

  // A file that could not be opened: closed, with why.
  bool faulty(SetFault why) {
    if (why == SetFault::Unread && faultError_ == 0) {
      faultError_ = errno;
    }
    fault_ = why;
    file_.close();
    std::vector<Key>().swap(firsts_);
    std::vector<std::uint64_t>().swap(offsets_);
    std::vector<std::uint64_t>().swap(chunkRaw_);
    std::vector<std::uint64_t>().swap(chunkAt_);
    return false;
  }

  // The index of a file of an earlier version (kOlderSetMagic): the blocks
  // stand as they are from the file's beginning, so they are taken as
  // chunks that were left as they are, of about kChunkBytes each.
  bool reloadOlder(std::uint64_t size) {
    std::uint64_t tail[3] = {0, 0, 0};
    Key last = 0;
    const std::uint64_t footer = sizeof(tail) + sizeof(Key) + sizeof(kOlderSetMagic);
    if (size < footer) {
      return faulty(SetFault::Damaged);
    }
    if (!file_.readAt(size - footer, tail, sizeof(tail)) ||
        !file_.readAt(size - footer + sizeof(tail), &last, sizeof(Key))) {
      return faulty(SetFault::Unread);
    }
    const std::uint64_t blocks = tail[1];
    const std::uint64_t dataBytes = tail[2];
    if (dataBytes > size || blocks > size / sizeof(std::uint64_t) ||
        dataBytes + blocks * sizeof(Key) + (blocks + 1) * sizeof(std::uint64_t) + footer !=
            size) {
      return faulty(SetFault::Damaged);
    }
    firsts_.resize(static_cast<std::size_t>(blocks));
    offsets_.resize(static_cast<std::size_t>(blocks + 1));
    if ((blocks != 0 && !file_.readAt(dataBytes, firsts_.data(), blocks * sizeof(Key))) ||
        !file_.readAt(dataBytes + blocks * sizeof(Key), offsets_.data(),
                      (blocks + 1) * sizeof(std::uint64_t))) {
      return faulty(SetFault::Unread);
    }
    if (offsets_.front() != 0 || offsets_.back() != dataBytes) {
      return faulty(SetFault::Damaged);
    }
    chunkRaw_.assign(1, 0);
    for (std::size_t b = 1; b <= blocks; b++) {
      if (offsets_[b] < offsets_[b - 1]) {
        return faulty(SetFault::Damaged);
      }
      if (b == blocks || offsets_[b] - chunkRaw_.back() >= kChunkBytes) {
        chunkRaw_.push_back(offsets_[b]);
      }
    }
    chunkRaw_.shrink_to_fit();
    chunkAt_ = chunkRaw_;
    if (!tablesHold(tail[0], last)) {
      return faulty(SetFault::Damaged);
    }
    count_ = static_cast<std::size_t>(tail[0]);
    last_ = last;
    dataBytes_ = dataBytes;
    fileSize_ = size;
    older_ = true;
    indexed_ = true;
    return true;
  }

  // Writes bytes to the file.
  bool put(const void* data, std::size_t len) {
    if (options_.gate != nullptr && !options_.gate->allow(len)) {
      return false;
    }
    // no more than 1 GB a call (the index of a level of billions of keys
    // is larger): a system may refuse a write of 2 GB or more
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    while (len > 0) {
      const std::size_t n = std::min<std::size_t>(len, std::size_t{1} << 30);
      if (std::fwrite(bytes, 1, n, out_.get()) != n) {
        return false;
      }
      bytes += n;
      len -= n;
    }
    return true;
  }

  // Chunk c of a file into the bytes at dst (as many as its blocks take).
  bool readChunk(std::size_t c, std::uint8_t* dst, BlockDecompressor& dec,
                 std::vector<std::uint8_t>& scratch) const {
    const auto rawLen = static_cast<std::size_t>(chunkRaw_[c + 1] - chunkRaw_[c]);
    const auto packed = static_cast<std::size_t>(chunkAt_[c + 1] - chunkAt_[c]);
    if (packed == rawLen) {
      return file_.readAt(chunkAt_[c], dst, rawLen);
    }
    scratch.resize(packed);
    return file_.readAt(chunkAt_[c], scratch.data(), packed) &&
           dec.decompress(scratch.data(), packed, dst, rawLen);
  }

  // Writes the block being built, if there is one: its key count, then
  // the gaps. A file's blocks gather in a chunk, written when it is full.
  bool flushBlock() {
    if (inBlock_ == 0) {
      return true;
    }
    std::uint8_t hdr[kMaxVarint<unsigned int>];
    const unsigned int n = putVarint(hdr, inBlock_);
    std::vector<std::uint8_t>& into = out_ ? chunk_ : mem_;
    into.insert(into.end(), hdr, hdr + n);
    into.insert(into.end(), buf_.begin(), buf_.end());
    offsets_.push_back(offsets_.back() + n + buf_.size());
    dataBytes_ = offsets_.back();
    buf_.clear();
    inBlock_ = 0;
    return !out_ || chunk_.size() < options_.chunkBytes || flushChunk();
  }

  // Writes the chunk being gathered, if there is one: compressed, or as it
  // is when that is no smaller (the reader tells by the two lengths).
  bool flushChunk() {
    if (chunk_.empty()) {
      return true;
    }
    const std::uint8_t* data = chunk_.data();
    std::size_t len = chunk_.size();
    if (comp_) {
      if (!comp_->compress(chunk_.data(), chunk_.size(), packed_)) {
        return false;
      }
      if (packed_.size() < len) {
        data = packed_.data();
        len = packed_.size();
      }
    }
    if (!put(data, len)) {
      return false;
    }
    chunkRaw_.push_back(chunkRaw_.back() + chunk_.size());
    chunkAt_.push_back(chunkAt_.back() + len);
    chunk_.clear();
    return true;
  }

  std::filesystem::path path_;
  FileOptions options_;
  FilePtr out_;
  ReadFile file_;
  std::vector<std::uint8_t> mem_;
  // The first key of each block, and where each block starts, counted in
  // the bytes of the blocks as they are: offsets_ has one more entry, the
  // end of the last block.
  std::vector<Key> firsts_;
  std::vector<std::uint64_t> offsets_{0};
  // A file's chunks: where each starts among the blocks' bytes, and where
  // in the file; one more entry each, the end of the last chunk.
  std::vector<std::uint64_t> chunkRaw_{0};
  std::vector<std::uint64_t> chunkAt_{0};
  bool indexed_ = true;
  // A file of an earlier version, read as it is; why the last open failed.
  bool older_ = false;
  SetFault fault_ = SetFault::None;
  int faultError_ = 0;
  std::size_t count_ = 0;
  std::uint64_t dataBytes_ = 0;
  // The size of a finished file.
  std::uint64_t fileSize_ = 0;
  // While writing: the gaps of the block being built, and its keys so far;
  // for a file, the blocks of the chunk being gathered, its compressor and
  // the compressed bytes.
  std::vector<std::uint8_t> buf_;
  unsigned int inBlock_ = 0;
  std::vector<std::uint8_t> chunk_;
  std::vector<std::uint8_t> packed_;
  std::unique_ptr<BlockCompressor> comp_;
  // The greatest key added.
  Key last_ = 0;
};

}  // namespace sl::sliding::levels
