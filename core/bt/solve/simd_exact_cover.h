// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The vector search of assembler 0: an exact cover search over bit-mask
/// rows instead of dancing links. Each node filters the rows still possible
/// with one vector AND per row; the search is read only, so several threads
/// can search subtrees of one solver at once.
/// Ported from src/lib/simd_exact_cover.{h,cpp}. The abort flag became a
/// StopToken, and the kernel choice comes from SimdKernels instead of
/// environment variables.

#include "bt/base/stop.h"
#include "bt/solve/simd_bitset.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace bt {

/// The size-independent interface of SimdExactCover.
class SimdExactCoverBase {
 public:
  virtual ~SimdExactCoverBase() = default;

  /// Called with the node ids of the rows of each cover found; false stops
  /// the search.
  using SolutionCallback = std::function<bool(const std::vector<unsigned int>& solutionNodes)>;

  /// Called now and then during a search (every few thousand nodes, and at
  /// each cover) with how far it is, 0..1, every choice at a level taken as
  /// alike. The caller can also use it to pass the iterations on.
  using ProgressCallback = std::function<void(float fraction)>;

  /// A column every cover must cover exactly once.
  virtual void setRequiredColumn(unsigned int col) = 0;

  /// An optional column (a variable voxel): covered at most once like any
  /// other, but never branched on, never a dead end when no row is left for
  /// it, and not asked for at the goal. Optional columns that are open with
  /// no row left count against the hole budget.
  virtual void setOptionalColumn(unsigned int col) = 0;

  /// How many optional columns may be left open with no row to cover them;
  /// a node with more is given up. As in the plain search this is looked at
  /// when a column is chosen and not at the goal.
  virtual void setHoleBudget(unsigned int h) = 0;

  /// Adds a row covering `cols`; `nodeId` is what solutions report for it.
  /// Returns the row's index.
  virtual std::uint32_t addRow(unsigned int nodeId, unsigned int pieceId,
                               const std::vector<unsigned int>& cols) = 0;

  /// Makes `nodeId` another name of row `rowIdx` (every node of a dancing
  /// links row names the row), for the prefixes of solveSubtree().
  virtual void registerNodeAlias(unsigned int nodeId, std::uint32_t rowIdx) = 0;

  /// Searches every cover. Nodes are added to `iterations` as it goes.
  virtual void solve(SolutionCallback callback, const StopToken& stop,
                     std::atomic<std::uint64_t>& iterations,
                     ProgressCallback progress = nullptr) const = 0;

  /// Searches the covers that hold the rows named in `prefixNodeIds` (in any
  /// order with the rows the search adds). Thread safe: several threads may
  /// search subtrees of one solver at once.
  virtual void solveSubtree(const std::vector<unsigned int>& prefixNodeIds,
                            SolutionCallback callback, const StopToken& stop,
                            std::atomic<std::uint64_t>& iterations,
                            ProgressCallback progress = nullptr) const = 0;

  [[nodiscard]] virtual unsigned int getNumRows() const = 0;
  [[nodiscard]] virtual unsigned int getNumColumns() const = 0;
  [[nodiscard]] virtual unsigned int getNumPieces() const = 0;

  /// The row filter this solver runs (the SimdKernels switches are testable
  /// only through this: every kernel gives the same answers).
  [[nodiscard]] virtual SimdKernel kernel() const = 0;
};

/// The exact cover search on bit sets of one size (256 to 32768 columns).
///
/// Assembler 0 uses it up to 2048 columns: beyond that the dense masks, the
/// per-level stack frames and the per-node column counts cost more than the
/// plain search saves. The larger sizes serve assembler 1's cover and tests.
template <typename BitsetType>
class SimdExactCover : public SimdExactCoverBase {
 public:
  struct Row {
    BitsetType mask;
    unsigned int nodeId = 0;
    unsigned int pieceId = 0;
    std::vector<unsigned int> columns;
  };

  /// A solver for columns 0 .. numColumns-1 (at most the bit set's size).
  SimdExactCover(unsigned int numColumns, unsigned int numPieces, SimdKernels kernels = {});

  void setRequiredColumn(unsigned int col) override;
  /// Sets all required columns at once.
  void setRequiredColumns(const BitsetType& required);
  void setOptionalColumn(unsigned int col) override;
  void setHoleBudget(unsigned int h) override { holes_ = h; }
  std::uint32_t addRow(unsigned int nodeId, unsigned int pieceId,
                       const std::vector<unsigned int>& cols) override;
  void registerNodeAlias(unsigned int nodeId, std::uint32_t rowIdx) override;

  void solve(SolutionCallback callback, const StopToken& stop,
             std::atomic<std::uint64_t>& iterations,
             ProgressCallback progress = nullptr) const override;

  void solveSubtree(const std::vector<unsigned int>& prefixNodeIds, SolutionCallback callback,
                    const StopToken& stop, std::atomic<std::uint64_t>& iterations,
                    ProgressCallback progress = nullptr) const override;

  [[nodiscard]] unsigned int getNumRows() const override {
    return static_cast<unsigned int>(rows_.size());
  }
  [[nodiscard]] unsigned int getNumColumns() const override { return numColumns_; }
  [[nodiscard]] unsigned int getNumPieces() const override { return numPieces_; }
  [[nodiscard]] SimdKernel kernel() const override { return kernel_; }

 private:
  /// How many levels below the start of a search its progress is taken from.
  static constexpr unsigned int kProgressLevels = 8;

  struct SearchContext {
    std::vector<std::vector<std::uint32_t>> scratchActiveRows;
    std::vector<unsigned int> currentSolution;
    std::vector<std::uint32_t> colCounts;
    std::uint64_t localIterations = 0;
    /// The nodes already added to the shared counter: they go over in
    /// batches of 256, and what is left at every solution, so that a reader
    /// sees the count move even in a search shorter than one batch.
    std::uint64_t flushedIterations = 0;

    /// Progress: for the first levels below baseDepth, which choice the
    /// search is in out of how many.
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

  void initContext(SearchContext& ctx, const ProgressCallback& progress,
                   unsigned int baseDepth) const;

  static void flushIterations(SearchContext& ctx, std::atomic<std::uint64_t>& iterations) {
    const std::uint64_t unflushed = ctx.localIterations - ctx.flushedIterations;
    if (unflushed > 0) {
      iterations.fetch_add(unflushed, std::memory_order_relaxed);
      ctx.flushedIterations = ctx.localIterations;
    }
  }

  void search(unsigned int depth, const BitsetType& occupied, SearchContext& ctx,
              SolutionCallback& callback, const StopToken& stop,
              std::atomic<std::uint64_t>& iterations) const;

  /// Appends to `dst` the rows of `src` that share no column with `chosenMask`.
  void filterRows(const std::vector<std::uint32_t>& src, const BitsetType& chosenMask,
                  std::vector<std::uint32_t>& dst) const;

#if defined(BT_SIMD_X86)
  // the targets on the declarations too: with them at the definitions only,
  // Apple clang 21 compiled filterRowsAvx2 for x86-64 without AVX and refused
  // its intrinsics (found by the first x86-64 build, release-x64)
  __attribute__((target("avx512f"))) void filterRowsAvx512(
      const std::vector<std::uint32_t>& src, const BitsetType& chosenMask,
      std::vector<std::uint32_t>& dst) const;
  __attribute__((target("avx2"))) void filterRowsAvx2(const std::vector<std::uint32_t>& src,
                                                      const BitsetType& chosenMask,
                                                      std::vector<std::uint32_t>& dst) const;
#elif defined(BT_SIMD_NEON)
  void filterRowsNeon(const std::vector<std::uint32_t>& src, const BitsetType& chosenMask,
                      std::vector<std::uint32_t>& dst) const;
#endif

  unsigned int numColumns_;
  unsigned int numPieces_;
  BitsetType requiredColumns_;
  BitsetType optionalColumns_;
  std::vector<unsigned int> optionalColumnList_;
  unsigned int holes_ = 0;
  std::vector<Row> rows_;
  std::vector<unsigned int> activeColumnList_;
  std::unordered_map<unsigned int, std::uint32_t> nodeToRowIdx_;
  SimdKernel kernel_;
};

using SimdExactCover256 = SimdExactCover<SimdBitset256>;
using SimdExactCover512 = SimdExactCover<SimdBitset512>;
using SimdExactCover1024 = SimdExactCover<SimdBitset1024>;
using SimdExactCover2048 = SimdExactCover<SimdBitset2048>;
using SimdExactCover4096 = SimdExactCover<SimdBitset4096>;
using SimdExactCover8192 = SimdExactCover<SimdBitset8192>;
using SimdExactCover16384 = SimdExactCover<SimdBitset16384>;
using SimdExactCover32768 = SimdExactCover<SimdBitset32768>;

extern template class SimdExactCover<SimdBitset256>;
extern template class SimdExactCover<SimdBitset512>;
extern template class SimdExactCover<SimdBitset1024>;
extern template class SimdExactCover<SimdBitset2048>;
extern template class SimdExactCover<SimdBitset4096>;
extern template class SimdExactCover<SimdBitset8192>;
extern template class SimdExactCover<SimdBitset16384>;
extern template class SimdExactCover<SimdBitset32768>;

/// A solver of the smallest size that holds `numColumns` columns (at most
/// 32768), as assembler 0 picks it.
[[nodiscard]] std::unique_ptr<SimdExactCoverBase> makeSimdExactCover(unsigned int numColumns,
                                                                     unsigned int numPieces,
                                                                     SimdKernels kernels);

}  // namespace bt
