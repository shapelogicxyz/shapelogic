// SPDX-License-Identifier: GPL-3.0-or-later
// Kept apart so the system headers it needs reach no other source file.
#include "shapelogic/base/file_lock.h"

#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace bt {

FileLock::~FileLock() {
  release();
}

FileLock::FileLock(FileLock&& other) noexcept
    : handle_(std::exchange(other.handle_, kNone)), path_(std::move(other.path_)) {
  other.path_.clear();
}

FileLock& FileLock::operator=(FileLock&& other) noexcept {
  if (this != &other) {
    release();
    handle_ = std::exchange(other.handle_, kNone);
    path_ = std::move(other.path_);
    other.path_.clear();
  }
  return *this;
}

#if defined(_WIN32)

// The file is opened with no sharing, so a second open fails while the
// first is there, in this program or another; the system removes it when
// the handle closes, which it does for a program that dies too.
FileLock::Outcome FileLock::tryAcquire(const std::filesystem::path& path) {
  release();
  const HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    // (access denied is a folder that cannot be written, or for an instant
    // a holder's file going as it closes: no lock either way)
    return GetLastError() == ERROR_SHARING_VIOLATION ? Outcome::Busy : Outcome::Failed;
  }
  handle_ = reinterpret_cast<std::intptr_t>(h);
  path_ = path;
  return Outcome::Held;
}

void FileLock::release() noexcept {
  if (handle_ == kNone) {
    return;
  }
  CloseHandle(reinterpret_cast<HANDLE>(handle_));
  handle_ = kNone;
  path_.clear();
}

#else

// flock() locks the open file, not the program: a second open of the file
// is refused in this program as in another, and the system lets the lock
// go when the descriptor closes, which it does for a program that dies
// too. The holder removes the file before it closes it, so a lock can be
// taken on a file that has just been removed: the lock counts only when
// the file it is on is still the one the path names.
FileLock::Outcome FileLock::tryAcquire(const std::filesystem::path& path) {
  release();
  for (int attempt = 0; attempt < 16; attempt++) {
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
      return Outcome::Failed;
    }
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
      const bool busy = errno == EWOULDBLOCK || errno == EAGAIN;
      ::close(fd);
      return busy ? Outcome::Busy : Outcome::Failed;
    }
    struct stat mine{};
    struct stat named{};
    if (::fstat(fd, &mine) == 0 && ::stat(path.c_str(), &named) == 0 &&
        mine.st_dev == named.st_dev && mine.st_ino == named.st_ino) {
      handle_ = fd;
      path_ = path;
      return Outcome::Held;
    }
    // its holder removed it as this opened it: once more, on a new file
    ::close(fd);
  }
  return Outcome::Busy;
}

void FileLock::release() noexcept {
  if (handle_ == kNone) {
    return;
  }
  // removed while still locked, so nobody takes a lock that counts on it
  ::unlink(path_.c_str());
  ::close(static_cast<int>(handle_));
  handle_ = kNone;
  path_.clear();
}

#endif

}  // namespace bt
