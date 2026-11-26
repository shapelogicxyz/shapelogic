// SPDX-License-Identifier: GPL-3.0-or-later
// Replaces src/tools/gzstream.cpp.

#include "shapelogic/io/gzip_file.h"

#include "shapelogic/base/assert.h"

#include <zlib.h>

#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <cerrno>
#include <fstream>
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

std::string describe(const std::filesystem::path& path) { return path.string(); }

/// The system's reason for the call that just failed, taken at once (the
/// next call may change it): errno, and on Windows GetLastError's code,
/// which tells "Access is denied (5)" (Controlled Folder Access refuses a
/// write so) from the C library's coarser errno (cleanup item 2.5).
struct SystemError {
  int err = errno;
#ifdef _WIN32
  unsigned long win = GetLastError();
#endif
};

/// Clears the system's last error before a call whose reason is read after it.
void clearSystemError() {
  errno = 0;
#ifdef _WIN32
  SetLastError(0);
#endif
}

/// ": <the system's words> (<its code>)"; empty when it gave no reason.
std::string reasonOf(const SystemError& e) {
#ifdef _WIN32
  if (e.win != 0) {
    std::string words = std::system_category().message(static_cast<int>(e.win));
    while (!words.empty() && (words.back() == '\n' || words.back() == '\r' || words.back() == '.')) {
      words.pop_back();
    }
    return ": " + words + " (" + std::to_string(e.win) + ")";
  }
#endif
  if (e.err == 0) {
    return {};
  }
  return ": " + std::generic_category().message(e.err) + " (errno " + std::to_string(e.err) + ")";
}

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
  clearSystemError();
#ifdef _WIN32
  return ::_wopen(tmp.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT,
                  _S_IREAD | _S_IWRITE);
#else
  return ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
#endif
}

/// Closes a descriptor; false when the close reported an error (the last
/// bytes may not have reached the file).
bool closeFd(int fd) {
#ifdef _WIN32
  return ::_close(fd) == 0;
#else
  return ::close(fd) == 0;
#endif
}

/// A second descriptor on the file `fd` is open on, or -1: zlib closes the
/// one it is given, and the file is synced after that close.
int dupFd(int fd) {
#ifdef _WIN32
  return ::_dup(fd);
#else
  return ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
#endif
}

/// Syncs the folder `file` is in, so that a rename just made into it lasts
/// a power cut (POSIX; Windows has no such call, and NTFS logs renames).
/// Best effort: the file is already in place, and some file systems refuse
/// to sync a folder.
void syncFolderOf(const std::filesystem::path& file) {
#ifndef _WIN32
  std::filesystem::path dir = file.parent_path();
  if (dir.empty()) {
    dir = ".";
  }
  const int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd >= 0) {
    (void)syncDescriptor(fd, Sync::Plain);
    ::close(fd);
  }
#else
  (void)file;
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

/// Whether the bytes begin as a gzip stream does.
bool isGzip(std::string_view data) {
  return data.size() >= 2 && static_cast<unsigned char>(data[0]) == 0x1f &&
         static_cast<unsigned char>(data[1]) == 0x8b;
}

Error tooLarge(std::size_t maxBytes) {
  return Error{Errc::ParseError, "the file is too large: it inflates to more than " +
                                     std::to_string(maxBytes >> 20) + " MiB"};
}

}  // namespace

bool syncDescriptor(int fd, Sync how) {
#ifdef _WIN32
  (void)how;
  // FlushFileBuffers, as _commit calls it, but with its reason read: a
  // drive that has no such call (some network and virtual drives answer
  // "Incorrect function") counts as synced, as a file system that cannot
  // sync does below; failing the save there would lose more than it guards
  const auto handle = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  if (FlushFileBuffers(handle) != 0) {
    return true;
  }
  const DWORD why = GetLastError();
  return why == ERROR_INVALID_FUNCTION || why == ERROR_NOT_SUPPORTED;
#else
#ifdef __APPLE__
  // fsync on macOS leaves the bytes in the drive's cache; F_FULLFSYNC
  // empties it, and fails on file systems that cannot (then fsync)
  if (how == Sync::Full && ::fcntl(fd, F_FULLFSYNC) == 0) {
    return true;
  }
#else
  (void)how;
#endif
  while (true) {
    if (::fsync(fd) == 0) {
      return true;
    }
    if (errno != EINTR) {
      // a file system or special file that cannot sync: nothing more to do
      return errno == EINVAL || errno == ENOTSUP || errno == EOPNOTSUPP;
    }
  }
#endif
}

Result<std::string> readPlainFile(const std::filesystem::path& path, std::size_t maxBytes) {
  // (a folder opens as a stream that reads nothing, without an error)
  std::error_code folderEc;
  if (std::filesystem::is_directory(path, folderEc)) {
    return fail(Errc::IoError, "could not open " + describe(path) + ": it is a folder");
  }
  clearSystemError();
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    const SystemError why;
    return fail(Errc::IoError, "could not open " + describe(path) + reasonOf(why));
  }
  std::string data;
  std::array<char, 65536> buf{};
  while (true) {
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    const auto n = static_cast<std::size_t>(in.gcount());
    if (n == 0) {
      break;
    }
    if (data.size() + n > maxBytes) {
      return std::unexpected(tooLarge(maxBytes));
    }
    data.append(buf.data(), n);
  }
  if (in.bad()) {
    return fail(Errc::IoError, "could not read " + describe(path));
  }
  return data;
}

Result<std::string> readMaybeGzipFile(const std::filesystem::path& path, std::size_t maxBytes) {
  // the file's bytes, then the one inflate every reader goes through
  // (cleanup item 16.5): zlib's own file reader took a stream cut short for
  // its end, so half a file was read as a whole one
  Result<std::string> bytes = readPlainFile(path, maxBytes);
  if (!bytes || !isGzip(*bytes)) {
    return bytes;
  }
  return gunzipIfCompressed(*bytes, maxBytes);
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
      const SystemError why;
      return fail(Errc::IoError, "could not open " + describe(tmp) + " for writing" + reasonOf(why));
    }
    // kept open past zlib's close, to sync the file
    const int syncFd = dupFd(fd);
    if (syncFd < 0) {
      const SystemError why;
      (void)closeFd(fd);
      std::filesystem::remove(tmp, ec);
      return fail(Errc::IoError, "could not open " + describe(tmp) + " for writing" + reasonOf(why));
    }
    GzHandle f(gzdopen(fd, "wb9"));
    if (!f) {
      (void)closeFd(fd);
      (void)closeFd(syncFd);
      std::filesystem::remove(tmp, ec);
      return fail(Errc::IoError, "could not open " + describe(tmp) + " for writing");
    }
    while (!data.empty()) {
      const auto chunk = static_cast<unsigned int>(std::min<std::size_t>(data.size(), 1u << 30));
      if (gzwrite(f.get(), data.data(), chunk) != static_cast<int>(chunk)) {
        const SystemError why;
        f.reset();
        (void)closeFd(syncFd);
        std::filesystem::remove(tmp, ec);
        return fail(Errc::IoError, "could not write " + describe(path) + reasonOf(why));
      }
      data.remove_prefix(chunk);
    }
    // closing writes the end of the compressed data; it can fail too
    clearSystemError();
    bool whole = gzclose(f.release()) == Z_OK && syncDescriptor(syncFd, Sync::Full);
    SystemError why;
    if (!closeFd(syncFd) && whole) {
      why = SystemError{};
      whole = false;
    }
    if (!whole) {
      std::filesystem::remove(tmp, ec);
      return fail(Errc::IoError, "could not write " + describe(path) + reasonOf(why));
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
    const SystemError why;
    return fail(Errc::IoError, "could not open " + describe(tmp) + " for writing" + reasonOf(why));
  }
  clearSystemError();
  bool written = writeAll(fd, data) && syncDescriptor(fd, Sync::Full);
  SystemError why;
  // the close is checked too: some file systems report a failed write only then
  if (!closeFd(fd) && written) {
    why = SystemError{};
    written = false;
  }
  if (!written) {
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    return fail(Errc::IoError, "could not write " + describe(path) + reasonOf(why));
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
                               const std::function<bool(std::ostream&)>& write, Sync how) {
  const Result<std::filesystem::path> target = saveTarget(path);
  if (!target) {
    return std::unexpected(target.error());
  }
  std::filesystem::path tmp = *target;
  tmp += ".tmp";
  const int fd = openTemporary(tmp);
  if (fd < 0) {
    const SystemError why;
    return fail(Errc::IoError, "could not open " + describe(tmp) + " for writing" + reasonOf(why));
  }
  bool ok = false;
  clearSystemError();
  {
    FdOutBuf buf(fd);
    std::ostream out(&buf);
    ok = write(out) && static_cast<bool>(out.flush()) && buf.flushBuffer();
  }
  ok = ok && syncDescriptor(fd, how);
  // a writer that failed on its own (not the file) leaves errno 0: no reason then
  SystemError why;
  // the close is checked too: some file systems report a failed write only then
  if (!closeFd(fd) && ok) {
    why = SystemError{};
    ok = false;
  }
  if (!ok) {
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    return fail(Errc::IoError, "could not write " + describe(path) + reasonOf(why));
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
  syncFolderOf(target);
  return {};
}

std::string gzipCompress(std::string_view data, int level) {
  // the gzip wrapper (window bits + 16); level 9 is gzopen's "wb9"
  BT_ASSERT(level >= 1 && level <= 9);
  z_stream z{};
  const int init = deflateInit2(&z, level, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
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
  if (!isGzip(data)) {
    return std::string(data);
  }
  z_stream z{};
  if (inflateInit2(&z, 15 + 16) != Z_OK) {
    return fail(Errc::Internal, "zlib could not start decompressing");
  }
  struct Ender {
    z_stream* z;
    Ender(const Ender&) = delete;
    Ender& operator=(const Ender&) = delete;
    explicit Ender(z_stream* stream) : z(stream) {}
    ~Ender() { inflateEnd(z); }
  };
  const Ender ender(&z);
  const Error cutShort{Errc::ParseError, "the file is cut short"};
  std::string out;
  std::array<char, 65536> buf{};
  // zlib counts its input in 32 bits: handed over a piece at a time. The
  // pieces follow each other in `data`, so what zlib has not read yet is
  // always the bytes from z.next_in to the end of `data`.
  z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
  std::size_t left = data.size();
  while (true) {
    if (z.avail_in == 0 && left > 0) {
      const std::size_t piece = std::min<std::size_t>(left, std::size_t{1} << 30);
      z.avail_in = static_cast<uInt>(piece);
      left -= piece;
    }
    z.next_out = reinterpret_cast<Bytef*>(buf.data());
    z.avail_out = static_cast<uInt>(buf.size());
    const int ret = inflate(&z, Z_NO_FLUSH);
    const bool inputDone = z.avail_in == 0 && left == 0;
    if (ret == Z_BUF_ERROR && inputDone) {
      // nothing more to read, and the stream has not ended
      return std::unexpected(cutShort);
    }
    if (ret != Z_OK && ret != Z_STREAM_END) {
      return fail(Errc::ParseError, "broken gzip data");
    }
    const std::size_t made = buf.size() - z.avail_out;
    if (out.size() + made > maxBytes) {
      return std::unexpected(tooLarge(maxBytes));
    }
    out.append(buf.data(), made);
    if (ret == Z_STREAM_END) {
      // A file of several gzip streams is their contents one after
      // another, as zlib's file reader read it; what follows the last
      // stream and is not one is passed over, as it was.
      const std::string_view rest(reinterpret_cast<const char*>(z.next_in), z.avail_in + left);
      if (!isGzip(rest) || inflateReset(&z) != Z_OK) {
        break;
      }
      continue;
    }
    if (inputDone && z.avail_out != 0) {
      return std::unexpected(cutShort);
    }
  }
  return out;
}

}  // namespace bt::io
