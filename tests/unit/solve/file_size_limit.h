// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A disk that is full only once a file is open: while a FileSizeLimit
// lives, a write that takes any file of this process past `bytes` fails
// (EFBIG), as a write to a full disk does, though the file was opened and
// created as usual. A save that checked its stream only before the close
// (cleanup item 4.1) took such a write, lost at the close, for whole.
// POSIX only (RLIMIT_FSIZE); the limit is the process's, so it is held
// only while nothing else of the test writes.

#ifndef _WIN32

#include <sys/resource.h>

#include <csignal>
#include <cstdint>

namespace bt::test {

class FileSizeLimit {
 public:
  explicit FileSizeLimit(std::uint64_t bytes) {
    // a write past the limit raises SIGXFSZ, which ends the process unless
    // ignored; ignored, the write fails with EFBIG
    oldHandler_ = std::signal(SIGXFSZ, SIG_IGN);
    getrlimit(RLIMIT_FSIZE, &old_);
    rlimit lower = old_;
    lower.rlim_cur = static_cast<rlim_t>(bytes);
    setrlimit(RLIMIT_FSIZE, &lower);
  }
  ~FileSizeLimit() {
    setrlimit(RLIMIT_FSIZE, &old_);
    std::signal(SIGXFSZ, oldHandler_);
  }
  FileSizeLimit(const FileSizeLimit&) = delete;
  FileSizeLimit& operator=(const FileSizeLimit&) = delete;

 private:
  rlimit old_{};
  void (*oldHandler_)(int) = SIG_DFL;
};

}  // namespace bt::test

#endif
