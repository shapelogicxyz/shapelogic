// SPDX-License-Identifier: GPL-3.0-or-later
#include "shapelogic/base/search_memory.h"

#include "shapelogic/base/memory_share.h"
#include "shapelogic/base/system.h"

#include <algorithm>

namespace bt {

std::uint64_t memoryBudget(std::uint64_t bytes, bool high, const MemoryShare* share) {
  if (share != nullptr) {
    return std::max<std::uint64_t>(share->share(), 1);
  }
  if (bytes != 0) {
    return bytes;
  }
  return high ? std::max(kSearchMemoryBytes, physicalMemoryBytes() / 2) : kSearchMemoryBytes;
}

std::uint64_t memoryStates(std::uint64_t stateBytes, bool high, std::uint64_t bytes,
                           const MemoryShare* share) {
  if (stateBytes == 0) {
    stateBytes = 1;
  }
  return std::max<std::uint64_t>(memoryBudget(bytes, high, share) / stateBytes, 1);
}

}  // namespace bt
