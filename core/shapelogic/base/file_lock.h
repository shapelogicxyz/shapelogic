// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A lock that two holders cannot have at once, whether they are two
/// programs or two parts of one: the solve of a problem takes it so that
/// no other solve works in the same search folders (decision 0035).

#include <cstdint>
#include <filesystem>

namespace bt {

/// An exclusive lock named by a file. It is taken without waiting and let
/// go when the holder releases it, is destroyed, or its program ends in
/// any way (the system drops it), so a crash never leaves it held. The
/// file is there only while the lock is held.
class FileLock {
 public:
  /// How tryAcquire ended.
  enum class Outcome {
    Held,    ///< The lock is this object's now.
    Busy,    ///< Another holder has it.
    Failed,  ///< The file cannot be made (no such folder, no write access).
  };

  FileLock() = default;
  ~FileLock();
  FileLock(FileLock&& other) noexcept;
  FileLock& operator=(FileLock&& other) noexcept;
  FileLock(const FileLock&) = delete;
  FileLock& operator=(const FileLock&) = delete;

  /// Takes the lock named by `path` (a file in a folder that exists) if
  /// nobody holds it. A lock this object holds already is released first.
  [[nodiscard]] Outcome tryAcquire(const std::filesystem::path& path);

  /// Whether this object holds a lock.
  [[nodiscard]] bool held() const noexcept { return handle_ != kNone; }

  /// Lets the lock go and removes its file; nothing when none is held.
  void release() noexcept;

 private:
  static constexpr std::intptr_t kNone = -1;

  /// The open file: a descriptor, or on Windows a handle.
  std::intptr_t handle_ = kNone;
  std::filesystem::path path_;
};

}  // namespace bt
