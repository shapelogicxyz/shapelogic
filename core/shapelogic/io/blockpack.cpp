// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/blockpack.cpp: the signature and chunk layout, the
// little-endian words (put, get), the key gaps (encodeKeys, decodeKeys),
// packChunk, unpackChunk, pack and unpack. inParallel's threads of its own
// are a ThreadPool's here: a batch of chunks, each taken once.
#include "shapelogic/io/blockpack.h"

#include "shapelogic/base/thread_pool.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <istream>
#include <optional>
#include <ostream>
#include <vector>

namespace bt::io::blockpack {

namespace {

constexpr std::array<char, 4> kSignature = {'B', 'T', 'Z', '1'};
// Raw bytes a chunk covers: a multiple of the 16-byte key, and small enough
// that zlib's 32-bit lengths (on Windows) always hold it.
constexpr std::size_t kChunk = std::size_t{64} << 20;
// A chunk's packed data may claim at most this; more means damage.
constexpr std::uint64_t kMost = 2 * std::uint64_t{kChunk} + (1U << 20);

constexpr unsigned char kKindDeflate = 0;
constexpr unsigned char kKindKeys = 1;

template <class T>
void put(std::ostream& out, T v) {
  std::array<unsigned char, sizeof(T)> b{};
  for (std::size_t i = 0; i < sizeof(T); i++) {
    b[i] = static_cast<unsigned char>(static_cast<std::uint64_t>(v) >> (8 * i));
  }
  out.write(reinterpret_cast<const char*>(b.data()), sizeof(T));
}

template <class T>
bool get(std::istream& in, T& v) {
  std::array<unsigned char, sizeof(T)> b{};
  if (!in.read(reinterpret_cast<char*>(b.data()), sizeof(T))) {
    return false;
  }
  std::uint64_t r = 0;
  for (std::size_t i = 0; i < sizeof(T); i++) {
    r |= static_cast<std::uint64_t>(b[i]) << (8 * i);
  }
  v = static_cast<T>(r);
  return true;
}

// A key as the search stores it: two 64-bit words, high first in memory.
struct Key {
  std::uint64_t hi;
  std::uint64_t lo;
};

Key readKey(const unsigned char* p) {
  Key k{};
  std::memcpy(&k.hi, p, 8);
  std::memcpy(&k.lo, p + 8, 8);
  return k;
}

void writeKey(unsigned char* p, const Key& k) {
  std::memcpy(p, &k.hi, 8);
  std::memcpy(p + 8, &k.lo, 8);
}

bool less(const Key& a, const Key& b) { return a.hi < b.hi || (a.hi == b.hi && a.lo < b.lo); }

// The gaps between sorted keys, as variable-length numbers: 7 bits a byte,
// low first, the top bit set while more follow. False when the keys are
// not in increasing order.
bool encodeKeys(const unsigned char* raw, std::size_t bytes, std::vector<unsigned char>& enc) {
  enc.clear();
  enc.reserve(bytes / 4);
  Key prev{0, 0};
  for (std::size_t i = 0; i < bytes; i += 16) {
    const Key k = readKey(raw + i);
    if (i != 0 && !less(prev, k)) {
      return false;
    }
    // k - prev, in 128 bits
    std::uint64_t lo = k.lo - prev.lo;
    std::uint64_t hi = k.hi - prev.hi - (k.lo < prev.lo ? 1 : 0);
    do {
      auto b = static_cast<unsigned char>(lo & 0x7f);
      lo = (lo >> 7) | (hi << 57);
      hi >>= 7;
      if (lo != 0 || hi != 0) {
        b |= 0x80;
      }
      enc.push_back(b);
    } while (lo != 0 || hi != 0);
    prev = k;
  }
  return true;
}

bool decodeKeys(const unsigned char* enc, std::size_t encBytes, unsigned char* raw,
                std::size_t rawBytes) {
  Key prev{0, 0};
  std::size_t p = 0;
  for (std::size_t i = 0; i < rawBytes; i += 16) {
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;
    unsigned int shift = 0;
    for (;;) {
      if (p >= encBytes || shift >= 128) {
        return false;
      }
      const std::uint64_t b = enc[p++];
      const std::uint64_t v = b & 0x7f;
      if (shift < 64) {
        lo |= v << shift;
        if (shift > 57) {
          hi |= v >> (64 - shift);
        }
      } else {
        hi |= v << (shift - 64);
      }
      shift += 7;
      if ((b & 0x80) == 0) {
        break;
      }
    }
    Key k{};
    k.lo = prev.lo + lo;
    k.hi = prev.hi + hi + (k.lo < prev.lo ? 1 : 0);
    writeKey(raw + i, k);
    prev = k;
  }
  return p == encBytes;
}

// One chunk on its way in or out.
struct Chunk {
  unsigned char kind = kKindDeflate;
  std::vector<unsigned char> raw;
  std::vector<unsigned char> enc;  // what is deflated: raw itself, or the key gaps
  std::vector<unsigned char> packed;
  std::uint64_t rawBytes = 0;
  std::uint64_t encBytes = 0;
  bool ok = true;
};

void packChunk(Chunk& c, bool keys) {
  c.rawBytes = c.raw.size();
  const unsigned char* src = c.raw.data();
  std::size_t srcBytes = c.raw.size();
  c.kind = kKindDeflate;
  if (keys && c.raw.size() % 16 == 0 && encodeKeys(c.raw.data(), c.raw.size(), c.enc)) {
    c.kind = kKindKeys;
    src = c.enc.data();
    srcBytes = c.enc.size();
  }
  c.encBytes = srcBytes;
  uLongf out = compressBound(static_cast<uLong>(srcBytes));
  c.packed.resize(out);
  c.ok = compress2(c.packed.data(), &out, src, static_cast<uLong>(srcBytes),
                   Z_DEFAULT_COMPRESSION) == Z_OK;
  c.packed.resize(out);
  std::vector<unsigned char>().swap(c.enc);
}

void unpackChunk(Chunk& c) {
  if (c.kind != kKindDeflate && c.kind != kKindKeys) {
    c.ok = false;
    return;
  }
  std::vector<unsigned char>& dst = c.kind == kKindKeys ? c.enc : c.raw;
  dst.resize(static_cast<std::size_t>(c.encBytes));
  auto out = static_cast<uLongf>(c.encBytes);
  c.ok = uncompress(dst.data(), &out, c.packed.data(), static_cast<uLong>(c.packed.size())) ==
             Z_OK &&
         out == c.encBytes;
  if (c.ok && c.kind == kKindKeys) {
    c.raw.resize(static_cast<std::size_t>(c.rawBytes));
    c.ok = c.rawBytes % 16 == 0 && decodeKeys(c.enc.data(), c.enc.size(), c.raw.data(), c.raw.size());
  }
  std::vector<unsigned char>().swap(c.enc);
  std::vector<unsigned char>().swap(c.packed);
}

// The pool to work on: the one given, or one of the call's own.
class Pool {
 public:
  Pool(ThreadPool* given, unsigned int threads) {
    if (given != nullptr) {
      pool_ = given;
      threads_ = threads != 0 ? std::min(threads, given->cores()) : given->cores();
    } else {
      threads_ = threads != 0 ? std::min(threads, kMaxThreads) : hardwareThreads();
      own_.emplace(threads_);
      pool_ = &*own_;
    }
  }
  [[nodiscard]] unsigned int threads() const { return threads_; }

  // Runs f(i) for i in 0..n-1, each once, on the pool's threads (the
  // original's inParallel gave each a thread of its own).
  template <class F>
  void each(std::size_t n, F f) {
    std::atomic<std::size_t> next{0};
    pool_->run(static_cast<unsigned int>(std::min<std::size_t>(n, threads_)), [&](unsigned int) {
      for (std::size_t i = next.fetch_add(1); i < n; i = next.fetch_add(1)) {
        f(i);
      }
    });
  }

 private:
  std::optional<ThreadPool> own_;
  ThreadPool* pool_ = nullptr;
  unsigned int threads_ = 1;
};

}  // namespace

std::uint64_t pack(const std::filesystem::path& file, std::ostream& out, bool keys,
                   unsigned int threads, std::size_t chunkBytes, ThreadPool* pool) {
  if (chunkBytes == 0 || chunkBytes > kChunk || chunkBytes % 16 != 0) {
    chunkBytes = kChunk;
  }
  std::error_code ec;
  const std::uint64_t size = std::filesystem::file_size(file, ec);
  std::ifstream in(file, std::ios::binary);
  if (ec || !in) {
    return 0;
  }
  const std::streampos start = out.tellp();
  out.write(kSignature.data(), kSignature.size());
  put<std::uint64_t>(out, size);

  Pool work(pool, threads);
  std::uint64_t left = size;
  while (left != 0 && out) {
    // Read a chunk for each thread, pack them together, write in order.
    std::vector<Chunk> batch;
    while (left != 0 && batch.size() < work.threads()) {
      Chunk c;
      c.raw.resize(static_cast<std::size_t>(std::min<std::uint64_t>(left, chunkBytes)));
      if (!in.read(reinterpret_cast<char*>(c.raw.data()),
                   static_cast<std::streamsize>(c.raw.size()))) {
        return 0;
      }
      left -= c.raw.size();
      batch.push_back(std::move(c));
    }
    work.each(batch.size(), [&](std::size_t i) { packChunk(batch[i], keys); });
    for (const Chunk& c : batch) {
      if (!c.ok) {
        return 0;
      }
      put<std::uint8_t>(out, c.kind);
      put<std::uint64_t>(out, c.rawBytes);
      put<std::uint64_t>(out, c.encBytes);
      put<std::uint64_t>(out, c.packed.size());
      out.write(reinterpret_cast<const char*>(c.packed.data()),
                static_cast<std::streamsize>(c.packed.size()));
    }
  }
  if (!out) {
    return 0;
  }
  return static_cast<std::uint64_t>(out.tellp() - start);
}

bool unpack(std::istream& in, std::uint64_t bytes, const std::filesystem::path& file,
            unsigned int threads, ThreadPool* pool) {
  std::array<char, kSignature.size()> sig{};
  std::uint64_t size = 0;
  if (bytes < sig.size() + 8 || !in.read(sig.data(), sig.size()) || sig != kSignature ||
      !get(in, size)) {
    return false;
  }
  std::uint64_t left = bytes - sig.size() - 8;
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }

  Pool work(pool, threads);
  std::uint64_t written = 0;
  while (written < size) {
    std::vector<Chunk> batch;
    std::uint64_t batchBytes = 0;
    while (written + batchBytes < size && batch.size() < work.threads()) {
      Chunk c;
      std::uint8_t kind = 0;
      std::uint64_t packedBytes = 0;
      if (left < 25 || !get(in, kind) || !get(in, c.rawBytes) || !get(in, c.encBytes) ||
          !get(in, packedBytes)) {
        return false;
      }
      left -= 25;
      if (c.rawBytes == 0 || c.rawBytes > kChunk || c.encBytes > kMost || packedBytes > kMost ||
          packedBytes > left) {
        return false;
      }
      c.kind = kind;
      c.packed.resize(static_cast<std::size_t>(packedBytes));
      if (!in.read(reinterpret_cast<char*>(c.packed.data()),
                   static_cast<std::streamsize>(packedBytes))) {
        return false;
      }
      left -= packedBytes;
      batchBytes += c.rawBytes;
      batch.push_back(std::move(c));
    }
    work.each(batch.size(), [&](std::size_t i) { unpackChunk(batch[i]); });
    for (const Chunk& c : batch) {
      if (!c.ok) {
        return false;
      }
      out.write(reinterpret_cast<const char*>(c.raw.data()),
                static_cast<std::streamsize>(c.raw.size()));
      written += c.raw.size();
    }
    if (!out) {
      return false;
    }
  }
  return written == size && left == 0 && static_cast<bool>(out);
}

}  // namespace bt::io::blockpack
