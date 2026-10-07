// SPDX-License-Identifier: GPL-3.0-or-later
// Replaces src/tools/gzstream.cpp.

#include "bt/io/gzip_file.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <memory>

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

}  // namespace

Result<std::string> readMaybeGzipFile(const std::filesystem::path& path) {
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
    data.append(buf.data(), static_cast<std::size_t>(n));
  }
  return data;
}

Result<void> writeGzipFile(const std::filesystem::path& path, std::string_view data) {
  GzHandle f(openGz(path, "wb9"));
  if (!f) {
    return fail(Errc::IoError, "could not open " + describe(path) + " for writing");
  }

  while (!data.empty()) {
    const auto chunk = static_cast<unsigned int>(std::min<std::size_t>(data.size(), 1u << 30));
    if (gzwrite(f.get(), data.data(), chunk) != static_cast<int>(chunk)) {
      return fail(Errc::IoError, "could not write " + describe(path));
    }
    data.remove_prefix(chunk);
  }

  if (gzclose(f.release()) != Z_OK) {
    return fail(Errc::IoError, "could not write " + describe(path));
  }
  return {};
}

}  // namespace bt::io
