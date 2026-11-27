// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The one way the unit tests make a folder in the temp folder. The temp
// folder is the user's, shared by every session and build: a fixed name
// there lets two sl_tests processes at once (dev and release, a sanitizer
// run beside a test run) wipe each other's files or meet decision 0035's
// lock. A ScratchDir's name holds the process id and random digits, so no
// other process or test has it.

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace sl::test {

/// A folder of this test's own in the temp folder, made empty on
/// construction and removed (with what it holds) in the destructor.
class ScratchDir {
 public:
  /// `tag` names what the folder is for: <temp>/sl_test_<tag>-<pid>-<8 hex digits>.
  explicit ScratchDir(std::string_view tag) {
#if defined(_WIN32)
    const long pid = _getpid();
#else
    const long pid = getpid();
#endif
    std::random_device rd;
    const std::filesystem::path temp = std::filesystem::temp_directory_path();
    for (;;) {
      const std::uint32_t r = rd();
      std::string digits;
      for (int shift = 28; shift >= 0; shift -= 4) {
        digits += "0123456789abcdef"[(r >> shift) & 0xFU];
      }
      path_ = temp / ("sl_test_" + std::string(tag) + "-" + std::to_string(pid) + "-" + digits);
      // false when the name is taken: draw again
      if (std::filesystem::create_directories(path_)) {
        break;
      }
    }
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;
  ScratchDir(ScratchDir&&) = delete;
  ScratchDir& operator=(ScratchDir&&) = delete;

  /// The folder (it exists until the destructor runs).
  [[nodiscard]] const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

}  // namespace sl::test
