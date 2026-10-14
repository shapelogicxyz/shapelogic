// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Assembler 0: Knuth's dancing links over the placement matrix, with
/// optional columns for the variable voxels of the result. Handles problems
/// where every part is exactly one piece; the "Complete (ShapeLogic)" and
/// "Fast (Crowell)" solvers assemble with it.
/// Ported from src/lib/assembler_0.{h,cpp} onto the shared run layer
/// (decision 0009); the matrix is DlxMatrix.

#include "shapelogic/solve/assembler.h"
#include "shapelogic/solve/dlx_matrix.h"
#include "shapelogic/solve/resume_state.h"
#include "shapelogic/solve/rotation_filter.h"
#include "shapelogic/solve/search_run.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace bt {

class SimdExactCoverBase;

/// Assembler 0. Rows are piece placements; the columns are the pieces, the
/// filled voxels of the result (which must be covered) and the variable
/// voxels (which may be; at most `holes` of them stay empty).
///
/// A run takes one of three searches: the plain dancing-links search on the
/// caller's thread; a search split into subtree tasks over several threads;
/// or the vector search (SimdExactCover), alone or inside each task. A
/// stopped search carries on the way it ran.
class Assembler0 final : public Assembler {
 public:
  /// The version string of saved positions.
  static constexpr const char* kVersion = "1.5";

  /// An assembler for `problem` of `puzzle`; both must outlive it.
  Assembler0(const Puzzle& puzzle, const Problem& problem);
  ~Assembler0() override;

  /// True when every part of the problem is exactly one piece.
  [[nodiscard]] static bool canHandle(const Problem& problem);

  // The Assembler interface; see assembler.h for what each one does.
  AssemblerError createMatrix(const MatrixOptions& options) override;
  void applySolutionFilterFlags(bool keepMirrors, bool keepRotations, bool complete) override;
  [[nodiscard]] int errorParam() const override { return errorsParam_; }
  ReduceStats reduce(const StopToken& stop) override;
  [[nodiscard]] unsigned int reducePiece() const override {
    return reducePiece_.load(std::memory_order_relaxed);
  }
  RunEnd assemble(const AssemblyCallback& callback, const SearchOptions& options,
                  const StopToken& stop) override;
  [[nodiscard]] float finished() const override;
  [[nodiscard]] std::uint64_t iterations() const override {
    return iterations_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] SavedPosition savePosition() const override;
  AssemblerError restorePosition(std::string_view text, std::string_view version) override;

  [[nodiscard]] bool piecePlacementSupported() const override { return true; }
  [[nodiscard]] unsigned int piecePlacementCount(unsigned int piece) const override;
  unsigned int piecePlacement(unsigned int node, int delta, unsigned int piece,
                              unsigned char* tran, int* x, int* y, int* z) const override;

  void debugStep(unsigned long steps) override;
  [[nodiscard]] Assembly currentAssembly() const override;

 private:
  class Worker;

  /// One step of a task's prefix: the column chosen and the row taken.
  struct PrefixStep {
    unsigned int col;
    unsigned int row;
  };

  /// A subtree of the search: the rows chosen so far.
  struct SubtreeTask {
    std::vector<PrefixStep> prefix;
    /// Estimated part of the whole search: one over the number of choices
    /// at each step of the prefix. Not saved; a loaded task has 1.
    double weight = 1.0;
  };

  // ---- the searches ----------------------------------------------------------
  void solution(SearchRun& run);
  void iterativeMultiSearch(SearchRun& run);
  void generateSubtreeTasks(std::vector<SubtreeTask>& tasks, unsigned int targetTasks,
                            unsigned int maxDepth);
  void parallelMultiSearch(SearchRun& run, unsigned int workers, const SearchOptions& options);
  void reportParallelSolution(SearchRun& run, const unsigned int* solutionRows,
                              unsigned int count);
  [[nodiscard]] bool simdFits(const SearchOptions& options) const;
  [[nodiscard]] bool canUseSimd(const SearchOptions& options) const;
  /// The plain search has not moved: nothing chosen at depth 0. (It also
  /// stands at depth 0 between two first-level rows, with the last one still
  /// covered; that is no start.)
  [[nodiscard]] bool atStart() const { return pos_ == 0 && (rows_.empty() || rows_[0] == 0); }
  void simdSearch(SearchRun& run, const SearchOptions& options);
  [[nodiscard]] std::unique_ptr<SimdExactCoverBase> createSimdSolver(SimdKernels kernels) const;

  /// Covers the levels of a restored position or task one after the
  /// other, checking each before it is covered; returns how many were good
  /// (and are covered). uncoverLevels() takes the first `count` back off.
  unsigned int coverChecked(const std::vector<PrefixStep>& levels);
  void uncoverLevels(const std::vector<PrefixStep>& levels, unsigned int count);

  /// The fraction of the plain search, from where it stands (the original's
  /// getFinished()); computed by the thread running it.
  [[nodiscard]] float serialFraction() const;
  void publishSerialProgress() {
    serialProgress_.store(serialFraction(), std::memory_order_relaxed);
  }

  const Puzzle& puzzle_;
  const Problem& problem_;

  /// The placement matrix.
  DlxMatrix matrix_;
  /// Which assemblies are dropped as rotations of others.
  RotationFilter filter_;

  /// The rows chosen at each depth, the columns covered there, and the
  /// number of pieces placed: the position of the plain search.
  unsigned int pos_ = 0;
  std::vector<unsigned int> rows_;
  std::vector<unsigned int> columns_;

  AssemblerError errorsState_ = AssemblerError::None;
  int errorsParam_ = 0;

  /// Search steps; written by the searching threads, read by anyone.
  std::atomic<std::uint64_t> iterations_{0};

  /// The piece createMatrix() and reduce() are at.
  std::atomic<unsigned int> reducePiece_{0};

  // the assembler debugger
  bool debug_ = false;
  long debugLoops_ = 0;

  /// The split (parallel) and vector searches' resumable state.
  SplitSearchState<SubtreeTask> split_;

  /// Progress of a search split into tasks (and of the vector search, run
  /// as one task).
  TaskProgress taskProgress_;
  /// Progress of the plain search, published by the thread running it.
  std::atomic<float> serialProgress_{0};
};

}  // namespace bt
