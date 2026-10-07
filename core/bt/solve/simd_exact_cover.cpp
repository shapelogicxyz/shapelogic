// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/simd_exact_cover.cpp.
#include "bt/solve/simd_exact_cover.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace bt {

template <typename BitsetType>
SimdExactCover<BitsetType>::SimdExactCover(unsigned int cols, unsigned int pieces,
                                           SimdKernels kernels)
    : numColumns_(cols),
      numPieces_(pieces),
      kernel_(chooseKernel(kernels, BitsetType::kNumWords >= 8)) {
  BT_ASSERT(numColumns_ <= BitsetType::kNumWords * 64);
}

template <typename BitsetType>
void SimdExactCover<BitsetType>::setRequiredColumn(unsigned int col) {
  BT_ASSERT(col < numColumns_);
  BT_ASSERT(col < BitsetType::kNumWords * 64);
  if (!requiredColumns_.test(col)) {
    requiredColumns_.set(col);
    activeColumnList_.push_back(col);
  }
}

template <typename BitsetType>
void SimdExactCover<BitsetType>::setRequiredColumns(const BitsetType& required) {
  requiredColumns_ = required;
  activeColumnList_.clear();
  for (unsigned int c = 0; c < numColumns_; c++) {
    if (requiredColumns_.test(c)) {
      activeColumnList_.push_back(c);
    }
  }
}

template <typename BitsetType>
void SimdExactCover<BitsetType>::setOptionalColumn(unsigned int col) {
  BT_ASSERT(col < numColumns_);
  BT_ASSERT(col < BitsetType::kNumWords * 64);
  BT_ASSERT(!requiredColumns_.test(col));
  if (!optionalColumns_.test(col)) {
    optionalColumns_.set(col);
    optionalColumnList_.push_back(col);
  }
}

template <typename BitsetType>
void SimdExactCover<BitsetType>::initContext(SearchContext& ctx, const ProgressCallback& progress,
                                             unsigned int baseDepth) const {
  ctx.scratchActiveRows.resize(numPieces_ + 4);
  ctx.currentSolution.resize(numPieces_);
  ctx.colCounts.resize(numColumns_, 0);
  ctx.progress = progress ? &progress : nullptr;
  ctx.baseDepth = baseDepth;

  ctx.scratchActiveRows[0].resize(rows_.size());
  for (std::size_t i = 0; i < rows_.size(); i++) {
    ctx.scratchActiveRows[0][i] = static_cast<std::uint32_t>(i);
  }
}

template <typename BitsetType>
std::uint32_t SimdExactCover<BitsetType>::addRow(unsigned int nodeId, unsigned int pieceId,
                                                 const std::vector<unsigned int>& cols) {
  Row r;
  r.nodeId = nodeId;
  r.pieceId = pieceId;
  r.columns = cols;
  for (const unsigned int c : cols) {
    BT_ASSERT(c < numColumns_);
    BT_ASSERT(c < BitsetType::kNumWords * 64);
    r.mask.set(c);
  }
  const auto idx = static_cast<std::uint32_t>(rows_.size());
  nodeToRowIdx_[nodeId] = idx;
  rows_.push_back(std::move(r));
  return idx;
}

template <typename BitsetType>
void SimdExactCover<BitsetType>::registerNodeAlias(unsigned int nodeId, std::uint32_t rowIdx) {
  nodeToRowIdx_[nodeId] = rowIdx;
}

#if defined(BT_SIMD_X86)
template <typename BitsetType>
__attribute__((target("avx512f"))) void SimdExactCover<BitsetType>::filterRowsAvx512(
    const std::vector<std::uint32_t>& src, const BitsetType& chosenMask,
    std::vector<std::uint32_t>& dst) const {
  if constexpr (BitsetType::kNumWords >= 8) {
    constexpr std::size_t kVec = BitsetType::kNumWords / 8;
    __m512i va[kVec];
    for (std::size_t i = 0; i < kVec; ++i) {
      va[i] = _mm512_load_si512(static_cast<const void*>(&chosenMask.words[i * 8]));
    }

    for (const std::uint32_t idx : src) {
      const std::uint64_t* rw = rows_[idx].mask.words;
      bool disjoint = true;
      for (std::size_t i = 0; i < kVec; ++i) {
        const __m512i vb = _mm512_load_si512(static_cast<const void*>(&rw[i * 8]));
        if (_mm512_test_epi64_mask(va[i], vb) != 0) {
          disjoint = false;
          break;
        }
      }
      if (disjoint) {
        dst.push_back(idx);
      }
    }
  } else {
    filterRowsAvx2(src, chosenMask, dst);
  }
}

template <typename BitsetType>
__attribute__((target("avx2"))) void SimdExactCover<BitsetType>::filterRowsAvx2(
    const std::vector<std::uint32_t>& src, const BitsetType& chosenMask,
    std::vector<std::uint32_t>& dst) const {
  constexpr std::size_t kVec = BitsetType::kNumWords / 4;
  __m256i va[kVec];
  for (std::size_t i = 0; i < kVec; ++i) {
    va[i] = _mm256_load_si256(reinterpret_cast<const __m256i*>(&chosenMask.words[i * 4]));
  }

  for (const std::uint32_t idx : src) {
    const std::uint64_t* rw = rows_[idx].mask.words;
    bool disjoint = true;
    for (std::size_t i = 0; i < kVec; ++i) {
      const __m256i vb = _mm256_load_si256(reinterpret_cast<const __m256i*>(&rw[i * 4]));
      if (!_mm256_testz_si256(va[i], vb)) {
        disjoint = false;
        break;
      }
    }
    if (disjoint) {
      dst.push_back(idx);
    }
  }
}
#elif defined(BT_SIMD_NEON)
template <typename BitsetType>
void SimdExactCover<BitsetType>::filterRowsNeon(const std::vector<std::uint32_t>& src,
                                                const BitsetType& chosenMask,
                                                std::vector<std::uint32_t>& dst) const {
  constexpr std::size_t kVec = BitsetType::kNumWords / 2;
  uint64x2_t va[kVec];
  for (std::size_t i = 0; i < kVec; ++i) {
    va[i] = vld1q_u64(&chosenMask.words[i * 2]);
  }

  for (const std::uint32_t idx : src) {
    const std::uint64_t* rw = rows_[idx].mask.words;
    bool disjoint = true;
    for (std::size_t i = 0; i < kVec; ++i) {
      const uint64x2_t vb = vld1q_u64(&rw[i * 2]);
      const uint64x2_t c = vandq_u64(va[i], vb);
      if ((vgetq_lane_u64(c, 0) | vgetq_lane_u64(c, 1)) != 0) {
        disjoint = false;
        break;
      }
    }
    if (disjoint) {
      dst.push_back(idx);
    }
  }
}
#endif

template <typename BitsetType>
void SimdExactCover<BitsetType>::filterRows(const std::vector<std::uint32_t>& src,
                                            const BitsetType& chosenMask,
                                            std::vector<std::uint32_t>& dst) const {
  if (dst.capacity() < src.size()) {
    dst.reserve(src.size());
  }
#if defined(BT_SIMD_X86)
  if (kernel_ == SimdKernel::Avx512) {
    filterRowsAvx512(src, chosenMask, dst);
    return;
  }
  if (kernel_ == SimdKernel::Avx2) {
    filterRowsAvx2(src, chosenMask, dst);
    return;
  }
#elif defined(BT_SIMD_NEON)
  if (kernel_ == SimdKernel::Neon) {
    filterRowsNeon(src, chosenMask, dst);
    return;
  }
#endif

  for (const std::uint32_t idx : src) {
    if (isDisjointScalar(chosenMask, rows_[idx].mask)) {
      dst.push_back(idx);
    }
  }
}

template <typename BitsetType>
void SimdExactCover<BitsetType>::solve(SolutionCallback callback, const StopToken& stop,
                                       std::atomic<std::uint64_t>& iterations,
                                       ProgressCallback progress) const {
  if (rows_.empty() || activeColumnList_.empty()) {
    return;
  }

  SearchContext ctx;
  initContext(ctx, progress, 0);

  const BitsetType occupied;
  search(0, occupied, ctx, callback, stop, iterations);

  flushIterations(ctx, iterations);
}

template <typename BitsetType>
void SimdExactCover<BitsetType>::solveSubtree(const std::vector<unsigned int>& prefixNodeIds,
                                              SolutionCallback callback, const StopToken& stop,
                                              std::atomic<std::uint64_t>& iterations,
                                              ProgressCallback progress) const {
  if (rows_.empty() || activeColumnList_.empty()) {
    return;
  }

  SearchContext ctx;
  initContext(ctx, progress, static_cast<unsigned int>(prefixNodeIds.size()));
  // the prefix may be longer than a search from the root ever gets
  if (ctx.scratchActiveRows.size() < prefixNodeIds.size() + 4) {
    ctx.scratchActiveRows.resize(prefixNodeIds.size() + 4);
  }

  BitsetType occupied;
  bool conflict = false;

  for (std::size_t d = 0; d < prefixNodeIds.size(); d++) {
    const auto it = nodeToRowIdx_.find(prefixNodeIds[d]);
    if (it == nodeToRowIdx_.end()) {
      conflict = true;
      break;
    }
    const std::uint32_t rIdx = it->second;
    const Row& row = rows_[rIdx];
    if (!isDisjointScalar(occupied, row.mask)) {
      conflict = true;
      break;
    }
    occupied = occupied | row.mask;
    if (d >= ctx.currentSolution.size()) {
      ctx.currentSolution.resize(d + 1);
    }
    ctx.currentSolution[d] = row.nodeId;

    auto& nextActive = ctx.scratchActiveRows[d + 1];
    nextActive.clear();
    filterRows(ctx.scratchActiveRows[d], row.mask, nextActive);
  }

  if (!conflict) {
    search(static_cast<unsigned int>(prefixNodeIds.size()), occupied, ctx, callback, stop,
           iterations);
  }

  flushIterations(ctx, iterations);
}

template <typename BitsetType>
void SimdExactCover<BitsetType>::search(unsigned int depth, const BitsetType& occupied,
                                        SearchContext& ctx, SolutionCallback& callback,
                                        const StopToken& stop,
                                        std::atomic<std::uint64_t>& iterations) const {
  if (stop.stopRequested()) {
    return;
  }

  ctx.localIterations++;
  if (ctx.localIterations - ctx.flushedIterations >= 256) {
    iterations.fetch_add(256, std::memory_order_relaxed);
    ctx.flushedIterations += 256;
  }
  // on a count of its own: where solutions come thick, each of them flushes
  // and the batch above is never full
  if (ctx.progress && (ctx.localIterations & 4095) == 0) {
    (*ctx.progress)(ctx.fraction());
  }

  // goal: are all required columns covered?
  if (occupied.containsAll(requiredColumns_)) {
    ctx.currentSolution.resize(depth);
    flushIterations(ctx, iterations);
    // told here as well: a search with few nodes between its solutions (or
    // one that waits a long time in the callback) would otherwise never say
    // how far it is
    if (ctx.progress) {
      (*ctx.progress)(ctx.fraction());
    }
    (void)callback(ctx.currentSolution);
    return;
  }

  const auto& currActive = ctx.scratchActiveRows[depth];
  if (currActive.empty()) {
    return;
  }

  // count the options per column over the rows still possible
  std::fill(ctx.colCounts.begin(), ctx.colCounts.end(), 0);
  for (const std::uint32_t rIdx : currActive) {
    for (const unsigned int c : rows_[rIdx].columns) {
      ctx.colCounts[c]++;
    }
  }

  // Holes: an optional column that is open and has no row left stays open in
  // all of the subtree below. Their number never goes down along a path, so
  // more of them than the budget allows ends the path here.
  if (!optionalColumnList_.empty()) {
    unsigned int holeCount = 0;
    for (const unsigned int c : optionalColumnList_) {
      if (ctx.colCounts[c] == 0 && !occupied.test(c)) {
        if (++holeCount > holes_) {
          return;
        }
      }
    }
  }

  // the uncovered required column with the fewest options (MRV)
  std::uint32_t minCount = std::numeric_limits<std::uint32_t>::max();
  unsigned int bestCol = std::numeric_limits<unsigned int>::max();

  for (const unsigned int c : activeColumnList_) {
    if (occupied.test(c)) {
      continue;
    }
    const std::uint32_t cnt = ctx.colCounts[c];
    if (cnt == 0) {
      // a required column that can no longer be covered
      return;
    }
    if (cnt < minCount) {
      minCount = cnt;
      bestCol = c;
      if (minCount == 1) {
        break;  // a forced choice: nothing beats 1
      }
    }
  }

  if (bestCol == std::numeric_limits<unsigned int>::max()) {
    return;
  }

  // which choice of how many, for the progress report
  const unsigned int level = depth - ctx.baseDepth;
  const bool track = level < kProgressLevels;
  if (track) {
    ctx.branchCnt[level] = minCount;
    ctx.branchIdx[level] = 0;
    if (level + 1 < kProgressLevels) {
      ctx.branchCnt[level + 1] = 0;
    }
  }
  bool first = true;

  // branch on every row still possible that covers bestCol
  for (const std::uint32_t rIdx : currActive) {
    const Row& candidateRow = rows_[rIdx];
    if (!candidateRow.mask.test(bestCol)) {
      continue;
    }

    if (track) {
      if (!first) {
        ctx.branchIdx[level]++;
        if (level + 1 < kProgressLevels) {
          ctx.branchCnt[level + 1] = 0;
        }
      }
      first = false;
    }

    if (depth >= ctx.currentSolution.size()) {
      ctx.currentSolution.resize(depth + 1);
    }
    ctx.currentSolution[depth] = candidateRow.nodeId;

    auto& nextActive = ctx.scratchActiveRows[depth + 1];
    nextActive.clear();

    filterRows(currActive, candidateRow.mask, nextActive);

    search(depth + 1, occupied | candidateRow.mask, ctx, callback, stop, iterations);

    if (stop.stopRequested()) {
      return;
    }
  }
}

template class SimdExactCover<SimdBitset256>;
template class SimdExactCover<SimdBitset512>;
template class SimdExactCover<SimdBitset1024>;
template class SimdExactCover<SimdBitset2048>;
template class SimdExactCover<SimdBitset4096>;
template class SimdExactCover<SimdBitset8192>;
template class SimdExactCover<SimdBitset16384>;
template class SimdExactCover<SimdBitset32768>;

std::unique_ptr<SimdExactCoverBase> makeSimdExactCover(unsigned int numColumns,
                                                       unsigned int numPieces,
                                                       SimdKernels kernels) {
  if (numColumns <= 256) {
    return std::make_unique<SimdExactCover256>(numColumns, numPieces, kernels);
  }
  if (numColumns <= 512) {
    return std::make_unique<SimdExactCover512>(numColumns, numPieces, kernels);
  }
  if (numColumns <= 1024) {
    return std::make_unique<SimdExactCover1024>(numColumns, numPieces, kernels);
  }
  if (numColumns <= 2048) {
    return std::make_unique<SimdExactCover2048>(numColumns, numPieces, kernels);
  }
  if (numColumns <= 4096) {
    return std::make_unique<SimdExactCover4096>(numColumns, numPieces, kernels);
  }
  if (numColumns <= 8192) {
    return std::make_unique<SimdExactCover8192>(numColumns, numPieces, kernels);
  }
  if (numColumns <= 16384) {
    return std::make_unique<SimdExactCover16384>(numColumns, numPieces, kernels);
  }
  return std::make_unique<SimdExactCover32768>(numColumns, numPieces, kernels);
}

}  // namespace bt
