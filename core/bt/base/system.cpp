// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/sysmemory.cpp. Kept apart so the system headers it
// needs reach no other source file.
#include "bt/base/system.h"

#include <cstdlib>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
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

std::string userCacheDirectory() {
#if defined(_WIN32)
  if (const char* local = std::getenv("LOCALAPPDATA")) {
    return std::string(local) + "\\BurrTools";
  }
  return "";
#elif defined(__APPLE__)
  if (const char* home = std::getenv("HOME")) {
    return std::string(home) + "/Library/Caches/BurrTools";
  }
  return "";
#else
  if (const char* xdg = std::getenv("XDG_CACHE_HOME")) {
    if (*xdg != 0) {
      return std::string(xdg) + "/burrtools";
    }
  }
  if (const char* home = std::getenv("HOME")) {
    return std::string(home) + "/.cache/burrtools";
  }
  return "";
#endif
}

}  // namespace bt
