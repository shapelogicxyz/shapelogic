// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The vector search of assembler 1 (Wei-Hwa Huang's cover with piece
/// counts): shape columns with a minimum and maximum, voxel columns, an
/// optional range column and hole columns, over bit-mask rows. Read only
/// during a search, so several threads can search subtrees of one solver.
/// Ported from src/lib/simd_huang_cover.{h,cpp}. The abort flag became a
/// StopToken, and the kernel choice comes from SimdKernels instead of
/// environment variables. Assembler 1 itself is ported in stage 3b.

#include "shapelogic/base/stop.h"
#include "shapelogic/solve/simd_bitset.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace bt {

/// The size-independent interface of SimdHuangCover.
class SimdHuangCoverBase {
 public:
  virtual ~SimdHuangCoverBase() = default;

  /// Called with the node ids of the rows of each cover found; false stops
  /// the search.
  using SolutionCallback = std::function<bool(const std::vector<unsigned int>& solutionNodes)>;
  /// As SimdExactCoverBase::ProgressCallback.
  using ProgressCallback = std::function<void(float fraction)>;

  /// Declares column `col` (1-based; 0 is the header): the weight it must
  /// reach and may not pass, and what kind of column it is. Voxel columns
  /// must have a minimum of at most 1.
  virtual void setColumnBounds(unsigned int col, unsigned int minW, unsigned int maxW,
                               bool isVoxel, bool isShape, bool isRange, bool isHole) = 0;

  /// How many hole columns may stay empty.
  virtual void setHoles(unsigned int h) = 0;

  /// Adds a row: the placement `shapeRowIdx` of shape `shapeId` (shape
  /// column `shapeCol`), its weight in the range column, and its columns
  /// with their weights. Returns the row's index.
  virtual std::uint32_t addRow(unsigned int nodeId, unsigned int shapeId, unsigned int shapeCol,
                               unsigned int shapeRowIdx, unsigned int rangeWeight,
                               const std::vector<unsigned int>& cols,
                               const std::vector<unsigned int>& weights) = 0;

  /// Makes `nodeId` another name of row `rowIdx`.
  virtual void registerNodeAlias(unsigned int nodeId, std::uint32_t rowIdx) = 0;

  /// Searches every cover.
  virtual void solve(SolutionCallback callback, const StopToken& stop,
                     std::atomic<std::uint64_t>& iterations,
                     ProgressCallback progress = nullptr) const = 0;

  /// Searches the covers that hold the rows named in `prefixNodeIds` and
  /// none of those in `hiddenNodeIds` (0 separates groups there and is not
  /// a row). Thread safe.
  virtual void solveSubtree(const std::vector<unsigned int>& prefixNodeIds,
                            const std::vector<unsigned int>& hiddenNodeIds,
                            SolutionCallback callback, const StopToken& stop,
                            std::atomic<std::uint64_t>& iterations,
                            ProgressCallback progress = nullptr) const = 0;

  [[nodiscard]] virtual unsigned int getNumRows() const = 0;
  [[nodiscard]] virtual unsigned int getNumColumns() const = 0;
  [[nodiscard]] virtual unsigned int getNumShapes() const = 0;

  /// The row filter this solver runs.
  [[nodiscard]] virtual SimdKernel kernel() const = 0;
};

/// The Huang cover search on bit sets of one size (256 to 32768 voxels).
template <typename BitsetType>
class SimdHuangCover : public SimdHuangCoverBase {
 public:
  struct Row {
    BitsetType voxelMask;              ///< the voxels covered
    unsigned int nodeId = 0;           ///< the dancing links node it stands for
    unsigned int shapeId = 0;          ///< piece shape index (0 .. numShapes-1)
    unsigned int shapeCol = 0;         ///< column of the shape
    unsigned int shapeRowIdx = 0;      ///< which placement of its shape it is
    unsigned int rangeWeight = 0;      ///< weight in the range column
    std::vector<unsigned int> columns; ///< every column touched
    std::vector<unsigned int> weights; ///< the weight in each of them
  };

  struct Column {
    unsigned int minWeight = 1;
    unsigned int maxWeight = 1;
    bool isVoxel = false;
    bool isShape = false;
    bool isRange = false;
    bool isHole = false;
  };

  /// A solver for columns 1 .. numColumns (at most the bit set's size).
  SimdHuangCover(unsigned int numColumns, unsigned int numShapes, SimdKernels kernels = {});

  void setColumnBounds(unsigned int col, unsigned int minW, unsigned int maxW, bool isVoxel,
                       bool isShape, bool isRange, bool isHole) override;
  void setHoles(unsigned int h) override { holes_ = h; }
  std::uint32_t addRow(unsigned int nodeId, unsigned int shapeId, unsigned int shapeCol,
                       unsigned int shapeRowIdx, unsigned int rangeWeight,
                       const std::vector<unsigned int>& cols,
                       const std::vector<unsigned int>& weights) override;
  void registerNodeAlias(unsigned int nodeId, std::uint32_t rowIdx) override;

  void solve(SolutionCallback callback, const StopToken& stop,
             std::atomic<std::uint64_t>& iterations,
             ProgressCallback progress = nullptr) const override;

  void solveSubtree(const std::vector<unsigned int>& prefixNodeIds,
                    const std::vector<unsigned int>& hiddenNodeIds, SolutionCallback callback,
                    const StopToken& stop, std::atomic<std::uint64_t>& iterations,
                    ProgressCallback progress = nullptr) const override;

  [[nodiscard]] unsigned int getNumRows() const override {
    return static_cast<unsigned int>(rows_.size());
  }
  [[nodiscard]] unsigned int getNumColumns() const override { return numColumns_; }
  [[nodiscard]] unsigned int getNumShapes() const override { return numShapes_; }
  [[nodiscard]] SimdKernel kernel() const override { return kernel_; }

 private:
  static constexpr unsigned int kProgressLevels = 8;

  struct SearchContext {
    BitsetType placedVoxels;
    std::vector<std::uint32_t> colWeights;
    std::vector<std::vector<std::uint32_t>> scratchActiveRows;
    std::vector<unsigned int> currentSolution;
    std::vector<std::uint32_t> colCounts;
    std::uint64_t localIterations = 0;
    /// the nodes already added to the shared counter, see flushIterations()
    std::uint64_t flushedIterations = 0;

    const ProgressCallback* progress = nullptr;
    unsigned int baseDepth = 0;
    std::uint32_t branchIdx[kProgressLevels] = {};
    std::uint32_t branchCnt[kProgressLevels] = {};

    [[nodiscard]] float fraction() const {
      double f = 0;
      double scale = 1;
      for (unsigned int l = 0; l < kProgressLevels && branchCnt[l]; l++) {
        scale /= branchCnt[l];
        f += branchIdx[l] * scale;
      }
      return static_cast<float>(f);
    }
  };

  /// The nodes go over to the shared counter in batches of 256, and what is
  /// left at every solution and at the end, so that a reader sees the count
  /// move even in a search shorter than one batch.
  static void flushIterations(SearchContext& ctx, std::atomic<std::uint64_t>& iterations) {
    const std::uint64_t unflushed = ctx.localIterations - ctx.flushedIterations;
    if (unflushed > 0) {
      iterations.fetch_add(unflushed, std::memory_order_relaxed);
      ctx.flushedIterations = ctx.localIterations;
    }
  }

  /// How deep a search can go. Every row placed uses up one unit of its
  /// shape column, so the depth is at most the sum of the shapes' maxima; it
  /// also covers at least one voxel no other row placed covers, so it is at
  /// most the number of columns too (the maxima come from the file and may
  /// be more than fits).
  [[nodiscard]] unsigned int searchDepthBound() const {
    unsigned int bound = 0;
    for (unsigned int c = 1; c <= numShapes_; c++) {
      bound += columns_[c].maxWeight;
    }
    return std::min(bound, numColumns_);
  }

  /// A fresh context. The scratch lists are indexed by depth, so they are
  /// sized by the depth bound and not by the number of columns. The list
  /// must not be resized during a search (search() holds references into
  /// it), hence the spare 16.
  void initContext(SearchContext& ctx) const {
    ctx.scratchActiveRows.resize(searchDepthBound() + 16);
    ctx.currentSolution.reserve(numColumns_);
    ctx.colWeights.assign(numColumns_ + 1, 0);
    ctx.colCounts.assign(numColumns_ + 1, 0);
  }

  void search(unsigned int depth, SearchContext& ctx, SolutionCallback& callback,
              const StopToken& stop, std::atomic<std::uint64_t>& iterations) const;

  void filterRows(const std::vector<std::uint32_t>& src, std::uint32_t chosenIdx,
                  const BitsetType& chosenVoxelMask, unsigned int chosenShape, bool shapeIsFull,
                  bool filterMonotonic, unsigned int chosenShapeRowIdx, bool checkRange,
                  unsigned int maxAllowedRangeWeight, std::vector<std::uint32_t>& dst) const;

#if defined(BT_SIMD_X86)
  // the targets on the declarations too: with them at the definitions only,
  // Apple clang 21 compiled filterRowsAvx2 for x86-64 without AVX and refused
  // its intrinsics (found by the first x86-64 build, release-x64)
  __attribute__((target("avx512f"))) void filterRowsAvx512(
      const std::vector<std::uint32_t>& src, std::uint32_t chosenIdx,
      const BitsetType& chosenVoxelMask, unsigned int chosenShape, bool shapeIsFull,
      bool filterMonotonic, unsigned int chosenShapeRowIdx, bool checkRange,
      unsigned int maxAllowedRangeWeight, std::vector<std::uint32_t>& dst) const;
  __attribute__((target("avx2"))) void filterRowsAvx2(
      const std::vector<std::uint32_t>& src, std::uint32_t chosenIdx,
      const BitsetType& chosenVoxelMask, unsigned int chosenShape, bool shapeIsFull,
      bool filterMonotonic, unsigned int chosenShapeRowIdx, bool checkRange,
      unsigned int maxAllowedRangeWeight, std::vector<std::uint32_t>& dst) const;
#elif defined(BT_SIMD_NEON)
  void filterRowsNeon(const std::vector<std::uint32_t>& src, std::uint32_t chosenIdx,
                      const BitsetType& chosenVoxelMask, unsigned int chosenShape,
                      bool shapeIsFull, bool filterMonotonic, unsigned int chosenShapeRowIdx,
                      bool checkRange, unsigned int maxAllowedRangeWeight,
                      std::vector<std::uint32_t>& dst) const;
#endif

  unsigned int numColumns_;
  unsigned int numShapes_;
  unsigned int totalMinPieces_ = 0;
  unsigned int holes_ = 0;
  unsigned int rangeColumn_ = 0;
  bool hasRange_ = false;

  BitsetType requiredVoxels_;
  std::vector<Column> columns_;
  std::vector<Row> rows_;
  std::vector<unsigned int> activeColumnList_;
  std::vector<unsigned int> holeColumns_;
  std::unordered_map<unsigned int, std::uint32_t> nodeToRowIdx_;
  SimdKernel kernel_;
};

using SimdHuangCover256 = SimdHuangCover<SimdBitset256>;
using SimdHuangCover512 = SimdHuangCover<SimdBitset512>;
using SimdHuangCover1024 = SimdHuangCover<SimdBitset1024>;
using SimdHuangCover2048 = SimdHuangCover<SimdBitset2048>;
using SimdHuangCover4096 = SimdHuangCover<SimdBitset4096>;
using SimdHuangCover8192 = SimdHuangCover<SimdBitset8192>;
using SimdHuangCover16384 = SimdHuangCover<SimdBitset16384>;
using SimdHuangCover32768 = SimdHuangCover<SimdBitset32768>;

/// A Huang cover of the smallest size class that holds `numColumns`
/// columns (at most 32768).
[[nodiscard]] std::unique_ptr<SimdHuangCoverBase> makeSimdHuangCover(unsigned int numColumns,
                                                                    unsigned int numShapes,
                                                                    SimdKernels kernels = {});

/// The bytes a Huang cover of `numColumns` columns with `numRows` rows and
/// `numNodes` matrix nodes needs: for each row its mask (as wide as the size
/// class makeSimdHuangCover() picks) and its fixed part, and for each node
/// its column and weight in the row's lists and its entry in the table of
/// node numbers. Ported from simdHuangMemoryEstimate in
/// src/lib/assembler_1.cpp.
[[nodiscard]] std::uint64_t simdHuangMemoryEstimate(unsigned int numColumns, std::uint64_t numRows,
                                                    std::uint64_t numNodes) noexcept;

/// True when the estimate is within `budget` bytes (simdHuangFitsMemory,
/// whose budget came from SHAPELOGIC_HUANG_MEM_MB; see
/// SearchOptions::vectorMemoryBudget).
[[nodiscard]] inline bool simdHuangFitsMemory(unsigned int numColumns, std::uint64_t numRows,
                                              std::uint64_t numNodes,
                                              std::uint64_t budget) noexcept {
  return simdHuangMemoryEstimate(numColumns, numRows, numNodes) <= budget;
}

extern template class SimdHuangCover<SimdBitset256>;
extern template class SimdHuangCover<SimdBitset512>;
extern template class SimdHuangCover<SimdBitset1024>;
extern template class SimdHuangCover<SimdBitset2048>;
extern template class SimdHuangCover<SimdBitset4096>;
extern template class SimdHuangCover<SimdBitset8192>;
extern template class SimdHuangCover<SimdBitset16384>;
extern template class SimdHuangCover<SimdBitset32768>;

}  // namespace bt
