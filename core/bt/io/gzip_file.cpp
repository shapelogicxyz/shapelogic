// SPDX-License-Identifier: GPL-3.0-or-later
// Replaces src/tools/gzstream.cpp.

#include "bt/io/gzip_file.h"

#include "bt/base/assert.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <system_error>

namespace bt::io {

namespace {

struct GzCloser {
  void operator()(gzFile f) const noexcept {
    if (f != nullptr) {
      gzclose(f);
    }
  }
};
using GzHandle = std::unique_ptr<gzFile_s, GzCloser>;

gzFile openGz(const std::filesystem::path& path, const char* mode) {
#ifdef _WIN32
  return gzopen_w(path.c_str(), mode);
#else
  return gzopen(path.c_str(), mode);
#endif
}

std::string describe(const std::filesystem::path& path) { return path.string(); }

Error tooLarge(std::size_t maxBytes) {
  return Error{Errc::ParseError, "the file is too large: it inflates to more than " +
                                     std::to_string(maxBytes >> 20) + " MiB"};
}

}  // namespace

Result<std::string> readMaybeGzipFile(const std::filesystem::path& path, std::size_t maxBytes) {
  // zlib reads data that is not gzip as it is ("transparent" reading)
  GzHandle f(openGz(path, "rb"));
  if (!f) {
    return fail(Errc::IoError, "could not open " + describe(path));
  }

  std::string data;
  std::array<char, 65536> buf{};
  while (true) {
    const int n = gzread(f.get(), buf.data(), static_cast<unsigned int>(buf.size()));
    if (n < 0) {
      int err = 0;
      const char* msg = gzerror(f.get(), &err);
      return fail(Errc::IoError, "could not read " + describe(path) + ": " + msg);
    }
    if (n == 0) {
      break;
    }
    if (data.size() + static_cast<std::size_t>(n) > maxBytes) {
      return std::unexpected(tooLarge(maxBytes));
    }
    data.append(buf.data(), static_cast<std::size_t>(n));
  }
  return data;
}

Result<void> writeGzipFile(const std::filesystem::path& path, std::string_view data) {
  // the original's writePuzzleFile (mainwindow_file.cpp): written beside the
  // file and renamed over it once whole, so a failed or interrupted save
  // leaves the old file as it was
  std::filesystem::path tmp = path;
  tmp += ".tmp";
  std::error_code ec;
  {
    GzHandle f(openGz(tmp, "wb9"));
    if (!f) {
      return fail(Errc::IoError, "could not open " + describe(tmp) + " for writing");
    }
    while (!data.empty()) {
      const auto chunk = static_cast<unsigned int>(std::min<std::size_t>(data.size(), 1u << 30));
      if (gzwrite(f.get(), data.data(), chunk) != static_cast<int>(chunk)) {
        f.reset();
        std::filesystem::remove(tmp, ec);
        return fail(Errc::IoError, "could not write " + describe(path));
      }
      data.remove_prefix(chunk);
    }
    // closing writes the end of the compressed data; it can fail too
    if (gzclose(f.release()) != Z_OK) {
      std::filesystem::remove(tmp, ec);
      return fail(Errc::IoError, "could not write " + describe(path));
    }
  }
  return replaceWith(tmp, path);
}

Result<void> replaceWith(const std::filesystem::path& written, const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::rename(written, path, ec);
  if (ec) {
    std::error_code ignored;
    std::filesystem::remove(written, ignored);
    return fail(Errc::IoError, "could not replace " + describe(path) + ": " + ec.message());
  }
  return {};
}

std::string gzipCompress(std::string_view data) {
  // the gzip wrapper (window bits + 16) and level 9, as gzopen's "wb9"
  z_stream z{};
  const int init = deflateInit2(&z, 9, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
  BT_ASSERT_MSG(init == Z_OK, "zlib could not start compressing");
  std::string out;
  std::array<char, 65536> buf{};
  z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
  int ret = Z_OK;
  std::size_t left = data.size();
  do {
    const auto chunk = static_cast<uInt>(std::min<std::size_t>(left, 1u << 30));
    z.avail_in = chunk;
    left -= chunk;
    const int flush = left == 0 ? Z_FINISH : Z_NO_FLUSH;
    do {
      z.next_out = reinterpret_cast<Bytef*>(buf.data());
      z.avail_out = static_cast<uInt>(buf.size());
      ret = deflate(&z, flush);
      out.append(buf.data(), buf.size() - z.avail_out);
    } while (z.avail_out == 0);
  } while (left > 0);
  BT_ASSERT(ret == Z_STREAM_END);
  deflateEnd(&z);
  return out;
}

Result<std::string> gunzipIfCompressed(std::string_view data, std::size_t maxBytes) {
  if (data.size() < 2 || static_cast<unsigned char>(data[0]) != 0x1f ||
      static_cast<unsigned char>(data[1]) != 0x8b) {
    return std::string(data);
  }
  z_stream z{};
  if (inflateInit2(&z, 15 + 16) != Z_OK) {
    return fail(Errc::Internal, "zlib could not start decompressing");
  }
  std::string out;
  std::array<char, 65536> buf{};
  z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
  z.avail_in = static_cast<uInt>(std::min<std::size_t>(data.size(), 0xFFFFFFFFu));
  int ret = Z_OK;
  while (ret != Z_STREAM_END) {
    z.next_out = reinterpret_cast<Bytef*>(buf.data());
    z.avail_out = static_cast<uInt>(buf.size());
    ret = inflate(&z, Z_NO_FLUSH);
    if (ret != Z_OK && ret != Z_STREAM_END) {
      inflateEnd(&z);
      return fail(Errc::ParseError, "broken gzip data");
    }
    if (out.size() + (buf.size() - z.avail_out) > maxBytes) {
      inflateEnd(&z);
      return std::unexpected(tooLarge(maxBytes));
    }
    out.append(buf.data(), buf.size() - z.avail_out);
    if (ret == Z_OK && z.avail_in == 0 && z.avail_out != 0) {
      inflateEnd(&z);
      return fail(Errc::ParseError, "the gzip data is cut short");
    }
  }
  inflateEnd(&z);
  return out;
}

}  // namespace bt::io
