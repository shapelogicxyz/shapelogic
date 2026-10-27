// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/sysmemory.cpp (availableDiskBytes and the disk
// budget are new). Kept apart so the system headers it needs reach no
// other source file.
#include "shapelogic/base/system.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <sys/sysctl.h>
#elif defined(__unix__)
#include <unistd.h>
#endif

namespace bt {

std::uint64_t physicalMemoryBytes() {
#if defined(_WIN32)
  MEMORYSTATUSEX status;
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status) != 0) {
    return status.ullTotalPhys;
  }
  return 0;
#elif defined(__APPLE__)
  std::uint64_t bytes = 0;
  std::size_t len = sizeof(bytes);
  if (sysctlbyname("hw.memsize", &bytes, &len, nullptr, 0) == 0) {
    return bytes;
  }
  return 0;
#elif defined(__unix__) && defined(_SC_PHYS_PAGES) && defined(_SC_PAGE_SIZE)
  const long pages = sysconf(_SC_PHYS_PAGES);
  const long pageSize = sysconf(_SC_PAGE_SIZE);
  if (pages > 0 && pageSize > 0) {
    return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(pageSize);
  }
  return 0;
#else
  return 0;
#endif
}

std::optional<std::uint64_t> availableDiskBytes(const std::filesystem::path& dir) {
  std::optional<std::uint64_t> bytes;
  std::error_code ec;
  const std::filesystem::space_info space = std::filesystem::space(dir, ec);
  if (!ec) {
    bytes = space.available;
  }
#if defined(__APPLE__)
  // The file system's free space leaves out what macOS frees on demand
  // (purgeable files, local snapshots), which can be most of the disk;
  // Finder counts it, and so does this.
  const std::string name = dir.string();
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(
      kCFAllocatorDefault, reinterpret_cast<const UInt8*>(name.data()),
      static_cast<CFIndex>(name.size()), true);
  if (url != nullptr) {
    CFTypeRef value = nullptr;
    if (CFURLCopyResourcePropertyForKey(url, kCFURLVolumeAvailableCapacityForImportantUsageKey,
                                        &value, nullptr) &&
        value != nullptr) {
      std::int64_t n = 0;
      if (CFGetTypeID(value) == CFNumberGetTypeID() &&
          CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt64Type, &n) && n > 0) {
        bytes = std::max(bytes.value_or(0), static_cast<std::uint64_t>(n));
      }
      CFRelease(value);
    }
    CFRelease(url);
  }
#endif
  return bytes;
}

DiskBudget diskBudgetFor(const std::filesystem::path& dir, std::uint64_t wanted,
                         std::uint64_t own) {
  DiskBudget b;
  b.automatic = wanted == 0;
  const std::optional<std::uint64_t> available = availableDiskBytes(dir);
  if (!available) {
    // nothing to go by: what was asked for, or almost nothing
    b.bytes = std::max<std::uint64_t>(wanted, 1);
    return b;
  }
  const std::uint64_t free = *available + own;
  b.free = free;
  const std::uint64_t share = std::min(free / 2, kAutoDiskBudgetMax);
  if (b.automatic) {
    b.bytes = std::max<std::uint64_t>(share, 1);
    return b;
  }
  const std::uint64_t reserve = std::max<std::uint64_t>(2000000000ULL, free / 20);
  const std::uint64_t room = std::max(free > reserve ? free - reserve : 0, free / 2);
  b.heldToFree = wanted > room;
  b.bytes = std::max<std::uint64_t>(std::min(wanted, room), 1);
  return b;
}

std::string diskBudgetReason(const DiskBudget& budget) {
  if (!budget.free || (!budget.automatic && !budget.heldToFree)) {
    return "";
  }
  char gb[32];
  std::snprintf(gb, sizeof(gb), "%.1f GB", static_cast<double>(*budget.free) / 1e9);
  if (budget.heldToFree) {
    return "as much as the " + std::string(gb) + " free there allows";
  }
  return budget.bytes == kAutoDiskBudgetMax
             ? "the most it takes unless told, with " + std::string(gb) + " free there"
             : "half the " + std::string(gb) + " free there";
}

std::string userCacheDirectory() {
#if defined(_WIN32)
  if (const char* local = std::getenv("LOCALAPPDATA")) {
    return std::string(local) + "\\ShapeLogic";
  }
  return "";
#elif defined(__APPLE__)
  if (const char* home = std::getenv("HOME")) {
    return std::string(home) + "/Library/Caches/ShapeLogic";
  }
  return "";
#else
  if (const char* xdg = std::getenv("XDG_CACHE_HOME")) {
    if (*xdg != 0) {
      return std::string(xdg) + "/shapelogic";
    }
  }
  if (const char* home = std::getenv("HOME")) {
    return std::string(home) + "/.cache/shapelogic";
  }
  return "";
#endif
}

}  // namespace bt
