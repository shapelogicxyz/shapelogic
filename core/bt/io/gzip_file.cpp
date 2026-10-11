// SPDX-License-Identifier: GPL-3.0-or-later
// Replaces src/tools/gzstream.cpp.

#include "bt/io/gzip_file.h"

#include "bt/base/assert.h"

#include <zlib.h>

#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <memory>
#include <ostream>
#include <streambuf>
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

/// Whether this process may write the file at `path` (which exists).
bool writable(const std::filesystem::path& path) {
#ifdef _WIN32
  return _waccess(path.c_str(), 2) == 0;
#else
  return access(path.c_str(), W_OK) == 0;
#endif
}

/// The most links followed to the file a path names (as Linux's limit).
constexpr int kMaxLinks = 40;

/// Opens the temporary file of a save for writing, as a file of this save's
/// own: a file or link already there (left by a save that died, or put there
/// so that the save would write through it to another file) is removed, and
/// the file is then made exclusively, so a link put in its place meanwhile
/// is refused rather than followed. A directory there is left, and fails the
/// open. The descriptor, or -1.
int openTemporary(const std::filesystem::path& tmp) {
  std::error_code ec;
  const std::filesystem::file_status old = std::filesystem::symlink_status(tmp, ec);
  if (!ec && (std::filesystem::is_symlink(old) || std::filesystem::is_regular_file(old))) {
    std::filesystem::remove(tmp, ec);
  }
#ifdef _WIN32
  return ::_wopen(tmp.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT,
                  _S_IREAD | _S_IWRITE);
#else
  return ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
#endif
}

void closeFd(int fd) {
#ifdef _WIN32
  ::_close(fd);
#else
  ::close(fd);
#endif
}

/// Writes all of `data` to an open descriptor; false when the write failed.
bool writeAll(int fd, std::string_view data) {
  while (!data.empty()) {
#ifdef _WIN32
    const auto chunk = static_cast<unsigned int>(std::min<std::size_t>(data.size(), 1u << 30));
    const int n = ::_write(fd, data.data(), chunk);
#else
    const std::size_t chunk = std::min<std::size_t>(data.size(), std::size_t{1} << 30);
    const ssize_t n = ::write(fd, data.data(), chunk);
#endif
    if (n <= 0) {
      return false;
    }
    data.remove_prefix(static_cast<std::size_t>(n));
  }
  return true;
}

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
  const Result<std::filesystem::path> target = saveTarget(path);
  if (!target) {
    return std::unexpected(target.error());
  }
  std::filesystem::path tmp = *target;
  tmp += ".tmp";
  std::error_code ec;
  {
    const int fd = openTemporary(tmp);
    if (fd < 0) {
      return fail(Errc::IoError, "could not open " + describe(tmp) + " for writing");
    }
    GzHandle f(gzdopen(fd, "wb9"));
    if (!f) {
      closeFd(fd);
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
  return replaceWith(tmp, *target);
}

Result<void> writeFileWhole(const std::filesystem::path& path, std::string_view data) {
  const Result<std::filesystem::path> target = saveTarget(path);
  if (!target) {
    return std::unexpected(target.error());
  }
  std::filesystem::path tmp = *target;
  tmp += ".tmp";
  const int fd = openTemporary(tmp);
  if (fd < 0) {
    return fail(Errc::IoError, "could not open " + describe(tmp) + " for writing");
  }
  const bool written = writeAll(fd, data);
  closeFd(fd);
  if (!written) {
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    return fail(Errc::IoError, "could not write " + describe(path));
  }
  return replaceWith(tmp, *target);
}

namespace {

/// A stream buffer that writes to a file descriptor and can seek in it.
class FdOutBuf : public std::streambuf {
 public:
  explicit FdOutBuf(int fd) : fd_(fd) { setp(buf_.data(), buf_.data() + buf_.size()); }
  FdOutBuf(const FdOutBuf&) = delete;
  FdOutBuf& operator=(const FdOutBuf&) = delete;
  ~FdOutBuf() override = default;

  /// Writes what is buffered; false when the write failed.
  bool flushBuffer() {
    const std::ptrdiff_t n = pptr() - pbase();
    if (n > 0 && !writeAll(fd_, std::string_view(pbase(), static_cast<std::size_t>(n)))) {
      failed_ = true;
    }
    setp(buf_.data(), buf_.data() + buf_.size());
    return !failed_;
  }
  [[nodiscard]] bool failed() const { return failed_; }

 protected:
  int_type overflow(int_type ch) override {
    if (!flushBuffer()) {
      return traits_type::eof();
    }
    if (!traits_type::eq_int_type(ch, traits_type::eof())) {
      *pptr() = traits_type::to_char_type(ch);
      pbump(1);
    }
    return traits_type::not_eof(ch);
  }
  int sync() override { return flushBuffer() ? 0 : -1; }
  pos_type seekoff(off_type off, std::ios_base::seekdir dir,
                   std::ios_base::openmode which) override {
    if ((which & std::ios_base::out) == 0 || !flushBuffer()) {
      return pos_type(off_type(-1));
    }
    const int whence = dir == std::ios_base::beg ? SEEK_SET
                       : dir == std::ios_base::cur ? SEEK_CUR
                                                   : SEEK_END;
#ifdef _WIN32
    const long long r = ::_lseeki64(fd_, off, whence);
#else
    const off_t r = ::lseek(fd_, static_cast<off_t>(off), whence);
#endif
    return r < 0 ? pos_type(off_type(-1)) : pos_type(static_cast<off_type>(r));
  }
  pos_type seekpos(pos_type pos, std::ios_base::openmode which) override {
    return seekoff(off_type(pos), std::ios_base::beg, which);
  }

 private:
  int fd_;
  bool failed_ = false;
  std::array<char, 65536> buf_{};
};

}  // namespace

Result<void> writeFileStreamed(const std::filesystem::path& path,
                               const std::function<bool(std::ostream&)>& write) {
  const Result<std::filesystem::path> target = saveTarget(path);
  if (!target) {
    return std::unexpected(target.error());
  }
  std::filesystem::path tmp = *target;
  tmp += ".tmp";
  const int fd = openTemporary(tmp);
  if (fd < 0) {
    return fail(Errc::IoError, "could not open " + describe(tmp) + " for writing");
  }
  bool ok = false;
  {
    FdOutBuf buf(fd);
    std::ostream out(&buf);
    ok = write(out) && static_cast<bool>(out.flush()) && buf.flushBuffer();
  }
  closeFd(fd);
  if (!ok) {
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    return fail(Errc::IoError, "could not write " + describe(path));
  }
  return replaceWith(tmp, *target);
}

Result<std::filesystem::path> saveTarget(const std::filesystem::path& path) {
  // a rename replaces a link, where writing the file (as saves did before
  // they went through a temporary file) wrote the file it names
  std::filesystem::path target = path;
  std::error_code ec;
  for (int i = 0; std::filesystem::is_symlink(target, ec); i++) {
    if (i == kMaxLinks) {
      return fail(Errc::IoError, "too many links: " + describe(path));
    }
    const std::filesystem::path to = std::filesystem::read_symlink(target, ec);
    if (ec) {
      return fail(Errc::IoError,
                  "could not read the link " + describe(target) + ": " + ec.message());
    }
    target = to.is_absolute() ? to : target.parent_path() / to;
  }
  // a rename replaces a file whatever its permissions; writing it did not
  if (std::filesystem::exists(target, ec) && !writable(target)) {
    return fail(Errc::IoError, describe(path) + " is read-only");
  }
  return target;
}

Result<void> replaceWith(const std::filesystem::path& written,
                         const std::filesystem::path& target) {
  std::error_code ec;
  // the file keeps who may read and write it (a new file has the defaults)
  const std::filesystem::file_status old = std::filesystem::status(target, ec);
  if (!ec && std::filesystem::exists(old)) {
    std::error_code ignored;
    std::filesystem::permissions(written, old.permissions() & std::filesystem::perms::all,
                                 std::filesystem::perm_options::replace, ignored);
  }
  std::filesystem::rename(written, target, ec);
  if (ec) {
    std::error_code ignored;
    std::filesystem::remove(written, ignored);
    return fail(Errc::IoError, "could not replace " + describe(target) + ": " + ec.message());
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
