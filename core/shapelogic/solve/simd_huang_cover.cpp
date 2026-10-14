// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/simd_huang_cover.cpp.
#include "shapelogic/solve/simd_huang_cover.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>

namespace bt {

template <typename BitsetType>
SimdHuangCover<BitsetType>::SimdHuangCover(unsigned int numCols, unsigned int numS,
                                           SimdKernels kernels)
    : numColumns_(numCols),
      numShapes_(numS),
      kernel_(chooseKernel(kernels, BitsetType::kNumWords >= 8)) {
  BT_ASSERT(numCols <= BitsetType::kNumWords * 64);
  columns_.resize(numColumns_ + 1);
}

template <typename BitsetType>
void SimdHuangCover<BitsetType>::setColumnBounds(unsigned int col, unsigned int minW,
                                                 unsigned int maxW, bool isVoxel, bool isShape,
                                                 bool isRange, bool isHole) {
  BT_ASSERT(col <= numColumns_);
  BT_ASSERT(col <= BitsetType::kNumWords * 64);

  // search() checks voxel columns with placedVoxels.containsAll(requiredVoxels),
  // a presence test that cannot tell weight 1 from weight n: a voxel column
  // demanding more than one unit would count as satisfied at one.
  BT_ASSERT(!isVoxel || minW <= 1);

  if (col >= columns_.size()) {
    columns_.resize(col + 1);
  }

  // What this column already added to totalMinPieces_, so that setting the
  // same shape column twice replaces it instead of counting it twice (which
  // would push the goal check out of reach and suppress every solution).
  const unsigned int prevMinPieces = columns_[col].isShape ? columns_[col].minWeight : 0U;

  columns_[col].minWeight = minW;
  columns_[col].maxWeight = maxW;
  columns_[col].isVoxel = isVoxel;
  columns_[col].isShape = isShape;
  columns_[col].isRange = isRange;
  columns_[col].isHole = isHole;

  if (isShape) {
    totalMinPieces_ += minW;
  }
  totalMinPieces_ -= prevMinPieces;

  if (isRange) {
    hasRange_ = true;
    rangeColumn_ = col;
  }
  if (isHole && std::find(holeColumns_.begin(), holeColumns_.end(), col) == holeColumns_.end()) {
    // for the same reason as totalMinPieces_: listing a hole column twice
    // would inflate the empty-hole count the search prunes on
    holeColumns_.push_back(col);
  }
  if (isVoxel && minW > 0 && col > 0 && (col - 1) < BitsetType::kNumWords * 64) {
    requiredVoxels_.set(col - 1);
  }
  activeColumnList_.push_back(col);
}

template <typename BitsetType>
std::uint32_t SimdHuangCover<BitsetType>::addRow(unsigned int nodeId, unsigned int shapeId,
                                                 unsigned int shapeCol, unsigned int shapeRowIdx,
                                                 unsigned int rangeWeight,
                                                 const std::vector<unsigned int>& cols,
                                                 const std::vector<unsigned int>& weights) {
  const auto idx = static_cast<std::uint32_t>(rows_.size());
  Row r;
  r.nodeId = nodeId;
  r.shapeId = shapeId;
  r.shapeCol = shapeCol;
  r.shapeRowIdx = shapeRowIdx;
  r.rangeWeight = rangeWeight;
  r.columns = cols;
  r.weights = weights;

  for (const unsigned int c : cols) {
    BT_ASSERT(c <= numColumns_);
    BT_ASSERT(c <= BitsetType::kNumWords * 64);
    if (c > 0 && c <= numColumns_ && (c - 1) < BitsetType::kNumWords * 64 &&
        columns_[c].isVoxel) {
      r.voxelMask.set(c - 1);
    }
  }

  nodeToRowIdx_[nodeId] = idx;
  rows_.push_back(std::move(r));
  return idx;
}

template <typename BitsetType>
void SimdHuangCover<BitsetType>::registerNodeAlias(unsigned int nodeId, std::uint32_t rowIdx) {
  nodeToRowIdx_[nodeId] = rowIdx;
}

#if defined(BT_SIMD_X86)
template <typename BitsetType>
__attribute__((target("avx512f"))) void SimdHuangCover<BitsetType>::filterRowsAvx512(
    const std::vector<std::uint32_t>& src, std::uint32_t chosenIdx,
    const BitsetType& chosenVoxelMask, unsigned int chosenShape, bool shapeIsFull,
    bool filterMonotonic, unsigned int chosenShapeRowIdx, bool checkRange,
    unsigned int maxAllowedRangeWeight, std::vector<std::uint32_t>& dst) const {
  if constexpr (BitsetType::kNumWords >= 8) {
    constexpr std::size_t kVec = BitsetType::kNumWords / 8;
    __m512i va[kVec];
    for (std::size_t i = 0; i < kVec; ++i) {
      va[i] = _mm512_load_si512(static_cast<const void*>(&chosenVoxelMask.words[i * 8]));
    }

    for (const std::uint32_t idx : src) {
      if (idx == chosenIdx) {
        continue;
      }
      const Row& cand = rows_[idx];
      if (cand.shapeId == chosenShape) {
        if (shapeIsFull || (filterMonotonic && cand.shapeRowIdx <= chosenShapeRowIdx)) {
          continue;
        }
      }
      if (checkRange && cand.rangeWeight > maxAllowedRangeWeight) {
        continue;
      }

      const std::uint64_t* rw = cand.voxelMask.words;
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
    filterRowsAvx2(src, chosenIdx, chosenVoxelMask, chosenShape, shapeIsFull, filterMonotonic,
                   chosenShapeRowIdx, checkRange, maxAllowedRangeWeight, dst);
  }
}

template <typename BitsetType>
__attribute__((target("avx2"))) void SimdHuangCover<BitsetType>::filterRowsAvx2(
    const std::vector<std::uint32_t>& src, std::uint32_t chosenIdx,
    const BitsetType& chosenVoxelMask, unsigned int chosenShape, bool shapeIsFull,
    bool filterMonotonic, unsigned int chosenShapeRowIdx, bool checkRange,
    unsigned int maxAllowedRangeWeight, std::vector<std::uint32_t>& dst) const {
  constexpr std::size_t kVec = BitsetType::kNumWords / 4;
  __m256i va[kVec];
  for (std::size_t i = 0; i < kVec; ++i) {
    va[i] = _mm256_load_si256(reinterpret_cast<const __m256i*>(&chosenVoxelMask.words[i * 4]));
  }

  for (const std::uint32_t idx : src) {
    if (idx == chosenIdx) {
      continue;
    }
    const Row& cand = rows_[idx];
    if (cand.shapeId == chosenShape) {
      if (shapeIsFull || (filterMonotonic && cand.shapeRowIdx <= chosenShapeRowIdx)) {
        continue;
      }
    }
    if (checkRange && cand.rangeWeight > maxAllowedRangeWeight) {
      continue;
    }

    const std::uint64_t* rw = cand.voxelMask.words;
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
void SimdHuangCover<BitsetType>::filterRowsNeon(
    const std::vector<std::uint32_t>& src, std::uint32_t chosenIdx,
    const BitsetType& chosenVoxelMask, unsigned int chosenShape, bool shapeIsFull,
    bool filterMonotonic, unsigned int chosenShapeRowIdx, bool checkRange,
    unsigned int maxAllowedRangeWeight, std::vector<std::uint32_t>& dst) const {
  constexpr std::size_t kVec = BitsetType::kNumWords / 2;
  uint64x2_t va[kVec];
  for (std::size_t i = 0; i < kVec; ++i) {
    va[i] = vld1q_u64(&chosenVoxelMask.words[i * 2]);
  }

  for (const std::uint32_t idx : src) {
    if (idx == chosenIdx) {
      continue;
    }
    const Row& cand = rows_[idx];
    if (cand.shapeId == chosenShape) {
      if (shapeIsFull || (filterMonotonic && cand.shapeRowIdx <= chosenShapeRowIdx)) {
        continue;
      }
    }
    if (checkRange && cand.rangeWeight > maxAllowedRangeWeight) {
      continue;
    }

    const std::uint64_t* rw = cand.voxelMask.words;
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
void SimdHuangCover<BitsetType>::filterRows(
    const std::vector<std::uint32_t>& src, std::uint32_t chosenIdx,
    const BitsetType& chosenVoxelMask, unsigned int chosenShape, bool shapeIsFull,
    bool filterMonotonic, unsigned int chosenShapeRowIdx, bool checkRange,
    unsigned int maxAllowedRangeWeight, std::vector<std::uint32_t>& dst) const {
  if (dst.capacity() < src.size()) {
    dst.reserve(src.size());
  }

#if defined(BT_SIMD_X86)
  if (kernel_ == SimdKernel::Avx512) {
    filterRowsAvx512(src, chosenIdx, chosenVoxelMask, chosenShape, shapeIsFull, filterMonotonic,
                     chosenShapeRowIdx, checkRange, maxAllowedRangeWeight, dst);
    return;
  }
  if (kernel_ == SimdKernel::Avx2) {
    filterRowsAvx2(src, chosenIdx, chosenVoxelMask, chosenShape, shapeIsFull, filterMonotonic,
                   chosenShapeRowIdx, checkRange, maxAllowedRangeWeight, dst);
    return;
  }
#elif defined(BT_SIMD_NEON)
  if (kernel_ == SimdKernel::Neon) {
    filterRowsNeon(src, chosenIdx, chosenVoxelMask, chosenShape, shapeIsFull, filterMonotonic,
                   chosenShapeRowIdx, checkRange, maxAllowedRangeWeight, dst);
    return;
  }
#endif

  for (const std::uint32_t idx : src) {
    if (idx == chosenIdx) {
      continue;
    }
    const Row& cand = rows_[idx];
    if (cand.shapeId == chosenShape) {
      if (shapeIsFull || (filterMonotonic && cand.shapeRowIdx <= chosenShapeRowIdx)) {
        continue;
      }
    }
    if (checkRange && cand.rangeWeight > maxAllowedRangeWeight) {
      continue;
    }

    if (isDisjointScalar(chosenVoxelMask, cand.voxelMask)) {
      dst.push_back(idx);
    }
  }
}

template <typename BitsetType>
void SimdHuangCover<BitsetType>::solve(SolutionCallback callback, const StopToken& stop,
                                       std::atomic<std::uint64_t>& iterations,
                                       ProgressCallback progress) const {
  if (rows_.empty() || activeColumnList_.empty()) {
    return;
  }

  SearchContext ctx;
  initContext(ctx);
  ctx.progress = progress ? &progress : nullptr;

  ctx.scratchActiveRows[0].resize(rows_.size());
  for (std::size_t i = 0; i < rows_.size(); i++) {
    ctx.scratchActiveRows[0][i] = static_cast<std::uint32_t>(i);
  }

  search(0, ctx, callback, stop, iterations);

  flushIterations(ctx, iterations);
}

template <typename BitsetType>
void SimdHuangCover<BitsetType>::solveSubtree(const std::vector<unsigned int>& prefixNodeIds,
                                              const std::vector<unsigned int>& hiddenNodeIds,
                                              SolutionCallback callback, const StopToken& stop,
                                              std::atomic<std::uint64_t>& iterations,
                                              ProgressCallback progress) const {
  if (rows_.empty() || activeColumnList_.empty()) {
    return;
  }

  SearchContext ctx;
  initContext(ctx);
  ctx.progress = progress ? &progress : nullptr;
  ctx.baseDepth = static_cast<unsigned int>(prefixNodeIds.size());
  // the prefix comes from the plain search, which may have placed more rows
  // than this search would ever have on its own
  if (ctx.scratchActiveRows.size() < prefixNodeIds.size() + 16) {
    ctx.scratchActiveRows.resize(prefixNodeIds.size() + 16);
  }

  std::vector<bool> isHidden(rows_.size(), false);
  for (const unsigned int h : hiddenNodeIds) {
    if (h == 0) {
      continue;
    }
    const auto it = nodeToRowIdx_.find(h);
    if (it != nodeToRowIdx_.end()) {
      isHidden[it->second] = true;
    }
  }

  ctx.scratchActiveRows[0].reserve(rows_.size());
  for (std::size_t i = 0; i < rows_.size(); i++) {
    if (!isHidden[i]) {
      ctx.scratchActiveRows[0].push_back(static_cast<std::uint32_t>(i));
    }
  }

  bool conflict = false;

  for (std::size_t d = 0; d < prefixNodeIds.size(); d++) {
    const auto it = nodeToRowIdx_.find(prefixNodeIds[d]);
    if (it == nodeToRowIdx_.end()) {
      conflict = true;
      break;
    }
    const std::uint32_t rIdx = it->second;
    const Row& cand = rows_[rIdx];

    // a voxel already covered
    if (!isDisjointScalar(ctx.placedVoxels, cand.voxelMask)) {
      conflict = true;
      break;
    }
    // the shape's maximum
    if (ctx.colWeights[cand.shapeCol] + 1 > columns_[cand.shapeCol].maxWeight) {
      conflict = true;
      break;
    }
    // the range's maximum
    if (hasRange_ &&
        ctx.colWeights[rangeColumn_] + cand.rangeWeight > columns_[rangeColumn_].maxWeight) {
      conflict = true;
      break;
    }

    // place it
    ctx.placedVoxels = ctx.placedVoxels | cand.voxelMask;
    for (std::size_t i = 0; i < cand.columns.size(); i++) {
      ctx.colWeights[cand.columns[i]] += cand.weights[i];
    }
    ctx.currentSolution.push_back(cand.nodeId);

    const bool shapeFull = ctx.colWeights[cand.shapeCol] >= columns_[cand.shapeCol].maxWeight;
    const unsigned int maxAllowedRange =
        hasRange_ ? (columns_[rangeColumn_].maxWeight - ctx.colWeights[rangeColumn_]) : 0;

    auto& nextActive = ctx.scratchActiveRows[d + 1];
    nextActive.clear();
    filterRows(ctx.scratchActiveRows[d], rIdx, cand.voxelMask, cand.shapeId, shapeFull, false,
               cand.shapeRowIdx, hasRange_, maxAllowedRange, nextActive);
  }

  if (!conflict) {
    search(static_cast<unsigned int>(prefixNodeIds.size()), ctx, callback, stop, iterations);
  }

  flushIterations(ctx, iterations);
}

template <typename BitsetType>
void SimdHuangCover<BitsetType>::search(unsigned int depth, SearchContext& ctx,
                                        SolutionCallback& callback, const StopToken& stop,
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

  // goal: is every condition met?
  if (ctx.currentSolution.size() >= totalMinPieces_) {
    if (ctx.placedVoxels.containsAll(requiredVoxels_)) {
      bool allFulfilled = true;
      for (unsigned int c = 1; c <= numShapes_; c++) {
        if (ctx.colWeights[c] < columns_[c].minWeight ||
            ctx.colWeights[c] > columns_[c].maxWeight) {
          allFulfilled = false;
          break;
        }
      }
      if (allFulfilled && hasRange_) {
        if (ctx.colWeights[rangeColumn_] < columns_[rangeColumn_].minWeight ||
            ctx.colWeights[rangeColumn_] > columns_[rangeColumn_].maxWeight) {
          allFulfilled = false;
        }
      }
      if (allFulfilled) {
        flushIterations(ctx, iterations);
        // told here as well, see SimdExactCover::search
        if (ctx.progress) {
          (*ctx.progress)(ctx.fraction());
        }
        (void)callback(ctx.currentSolution);
        return;
      }
    }
  }

  const auto& currActive = ctx.scratchActiveRows[depth];
  if (currActive.empty()) {
    return;
  }

  // the weight the rows still possible give each column
  std::fill(ctx.colCounts.begin(), ctx.colCounts.end(), 0);
  for (const std::uint32_t rIdx : currActive) {
    const Row& r = rows_[rIdx];
    const unsigned int* cols = r.columns.data();
    const unsigned int* wgts = r.weights.data();
    const std::size_t sz = r.columns.size();
    for (std::size_t i = 0; i < sz; i++) {
      ctx.colCounts[cols[i]] += wgts[i];
    }
  }

  // holes
  if (holes_ < holeColumns_.size()) {
    unsigned int emptyHoles = 0;
    for (const unsigned int hc : holeColumns_) {
      if (ctx.colCounts[hc] == 0 && ctx.colWeights[hc] == 0) {
        emptyHoles++;
        if (emptyHoles > holes_) {
          return;
        }
      }
    }
  }

  // dead ends and the pivot column (fewest choices) in one pass
  unsigned int minMetric = std::numeric_limits<unsigned int>::max();
  unsigned int bestCol = std::numeric_limits<unsigned int>::max();

  // 1. the shape columns (1..numShapes)
  for (unsigned int c = 1; c <= numShapes_; c++) {
    if (ctx.colWeights[c] >= columns_[c].minWeight) {
      continue;
    }
    const unsigned int count = ctx.colCounts[c];
    if (ctx.colWeights[c] + count < columns_[c].minWeight) {
      return;  // the shape cannot get its minimum of pieces
    }
    const unsigned int remainingNeed = columns_[c].minWeight - ctx.colWeights[c];
    const unsigned int metric = count * remainingNeed;
    if (metric < minMetric) {
      minMetric = metric;
      bestCol = c;
    }
  }

  // 2. The range column: to prune, and without variable voxels never to
  // branch on. When every voxel is covered exactly once the range weight of
  // a complete cover is forced, so the column only has to be checked (rows
  // over the maximum are filtered out, under the minimum is pruned here,
  // and the goal test looks at both). Branching on it would report two range
  // rows that go together once for each order, as nothing keeps its rows in
  // order the way a shape column's are. With variable voxels it stays a
  // pivot: filling holes may be the only way to the minimum.
  if (hasRange_ && ctx.colWeights[rangeColumn_] < columns_[rangeColumn_].minWeight) {
    const unsigned int count = ctx.colCounts[rangeColumn_];
    if (ctx.colWeights[rangeColumn_] + count < columns_[rangeColumn_].minWeight) {
      return;  // the range minimum cannot be reached
    }
    if (!holeColumns_.empty()) {
      const unsigned int remainingNeed =
          columns_[rangeColumn_].minWeight - ctx.colWeights[rangeColumn_];
      const unsigned int metric = count * remainingNeed;
      if (metric < minMetric) {
        minMetric = metric;
        bestCol = rangeColumn_;
      }
    }
  }

  // 3. the voxels not yet covered, found by scanning the bit set
  for (std::size_t w = 0; w < BitsetType::kNumWords; ++w) {
    std::uint64_t unplaced = requiredVoxels_.words[w] & ~ctx.placedVoxels.words[w];
    while (unplaced != 0) {
      const int bit = std::countr_zero(unplaced);
      const auto c = static_cast<unsigned int>(w * 64 + static_cast<std::size_t>(bit) + 1);
      unplaced &= (unplaced - 1);

      const unsigned int count = ctx.colCounts[c];
      if (count == 0) {
        return;  // a voxel no row can cover any more
      }

      if (count < minMetric) {
        minMetric = count;
        bestCol = c;
      }
    }
  }

  if (bestCol == std::numeric_limits<unsigned int>::max()) {
    return;
  }

  // No resize of ctx.scratchActiveRows here: currActive is a reference into
  // it, which a reallocation would leave dangling. The depth stays within
  // the list: see searchDepthBound() and initContext().
  BT_ASSERT(depth + 1 < ctx.scratchActiveRows.size());

  auto isCandidate = [&](const Row& cand) -> bool {
    const bool coversBest = columns_[bestCol].isShape   ? (cand.shapeCol == bestCol)
                            : columns_[bestCol].isVoxel ? cand.voxelMask.test(bestCol - 1)
                                                        : (cand.rangeWeight > 0);
    if (!coversBest) {
      return false;
    }
    if (ctx.colWeights[cand.shapeCol] + 1 > columns_[cand.shapeCol].maxWeight) {
      return false;
    }
    if (hasRange_ &&
        ctx.colWeights[rangeColumn_] + cand.rangeWeight > columns_[rangeColumn_].maxWeight) {
      return false;
    }
    return true;
  };

  // which choice of how many, for the progress report (near the top only,
  // where counting the choices first costs nothing that matters)
  const unsigned int level = depth - ctx.baseDepth;
  const bool track = ctx.progress && level < kProgressLevels;
  if (track) {
    std::uint32_t n = 0;
    for (const std::uint32_t rIdx : currActive) {
      if (isCandidate(rows_[rIdx])) {
        n++;
      }
    }
    ctx.branchCnt[level] = n;
    ctx.branchIdx[level] = 0;
    if (level + 1 < kProgressLevels) {
      ctx.branchCnt[level + 1] = 0;
    }
  }
  bool first = true;

  for (const std::uint32_t rIdx : currActive) {
    const Row& cand = rows_[rIdx];

    if (!isCandidate(cand)) {
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

    // place the row
    ctx.placedVoxels = ctx.placedVoxels | cand.voxelMask;
    for (std::size_t i = 0; i < cand.columns.size(); i++) {
      ctx.colWeights[cand.columns[i]] += cand.weights[i];
    }
    ctx.currentSolution.push_back(cand.nodeId);

    const bool shapeFull = ctx.colWeights[cand.shapeCol] >= columns_[cand.shapeCol].maxWeight;
    const bool filterMonotonic = !shapeFull && columns_[bestCol].isShape;
    const unsigned int maxAllowedRange =
        hasRange_ ? (columns_[rangeColumn_].maxWeight - ctx.colWeights[rangeColumn_]) : 0;

    auto& nextActive = ctx.scratchActiveRows[depth + 1];
    nextActive.clear();

    filterRows(currActive, rIdx, cand.voxelMask, cand.shapeId, shapeFull, filterMonotonic,
               cand.shapeRowIdx, hasRange_, maxAllowedRange, nextActive);

    search(depth + 1, ctx, callback, stop, iterations);

    // take it back (no allocation, no relinking)
    ctx.currentSolution.pop_back();
    for (std::size_t i = 0; i < cand.columns.size(); i++) {
      ctx.colWeights[cand.columns[i]] -= cand.weights[i];
    }
    ctx.placedVoxels = ctx.placedVoxels ^ cand.voxelMask;

    if (stop.stopRequested()) {
      return;
    }
  }
}

template class SimdHuangCover<SimdBitset256>;
template class SimdHuangCover<SimdBitset512>;
template class SimdHuangCover<SimdBitset1024>;
template class SimdHuangCover<SimdBitset2048>;
template class SimdHuangCover<SimdBitset4096>;
template class SimdHuangCover<SimdBitset8192>;
template class SimdHuangCover<SimdBitset16384>;
template class SimdHuangCover<SimdBitset32768>;

std::unique_ptr<SimdHuangCoverBase> makeSimdHuangCover(unsigned int numColumns,
                                                       unsigned int numShapes,
                                                       SimdKernels kernels) {
  if (numColumns <= 256) {
    return std::make_unique<SimdHuangCover256>(numColumns, numShapes, kernels);
  }
  if (numColumns <= 512) {
    return std::make_unique<SimdHuangCover512>(numColumns, numShapes, kernels);
  }
  if (numColumns <= 1024) {
    return std::make_unique<SimdHuangCover1024>(numColumns, numShapes, kernels);
  }
  if (numColumns <= 2048) {
    return std::make_unique<SimdHuangCover2048>(numColumns, numShapes, kernels);
  }
  if (numColumns <= 4096) {
    return std::make_unique<SimdHuangCover4096>(numColumns, numShapes, kernels);
  }
  if (numColumns <= 8192) {
    return std::make_unique<SimdHuangCover8192>(numColumns, numShapes, kernels);
  }
  if (numColumns <= 16384) {
    return std::make_unique<SimdHuangCover16384>(numColumns, numShapes, kernels);
  }
  return std::make_unique<SimdHuangCover32768>(numColumns, numShapes, kernels);
}

std::uint64_t simdHuangMemoryEstimate(unsigned int numColumns, std::uint64_t numRows,
                                      std::uint64_t numNodes) noexcept {
  std::uint64_t maskBytes = 32;
  while (maskBytes * 8 < numColumns && maskBytes < 4096) {
    maskBytes *= 2;
  }
  const std::uint64_t rowFixed = 6 * sizeof(unsigned int) + 2 * sizeof(std::vector<unsigned int>);
  const std::uint64_t perNode = 2 * sizeof(unsigned int) + 40;
  return numRows * (maskBytes + rowFixed) + numNodes * perNode;
}

}  // namespace bt
