// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Assembler 0: Knuth's dancing links over the placement matrix, with
/// optional columns for the variable voxels of the result. Handles problems
/// where every part is exactly one piece; the "BurrTools Classic" and
/// "Andrew Crowell" solvers assemble with it.
/// Ported from src/lib/assembler_0.{h,cpp} onto the shared run layer
/// (decision 0009).

#include "bt/solve/assembler.h"
#include "bt/solve/resume_state.h"
#include "bt/solve/search_run.h"

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

  /// The placement a row stands for.
  struct PiecePosition {
    int x, y, z;
    unsigned char transformation;
    unsigned int row;    ///< first node of the row
    unsigned int piece;
  };

  // ---- the matrix ------------------------------------------------------------
  unsigned int& up(unsigned int x) { return upDown_[2 * std::size_t{x}]; }
  unsigned int& down(unsigned int x) { return upDown_[2 * std::size_t{x} + 1]; }
  [[nodiscard]] unsigned int up(unsigned int x) const { return upDown_[2 * std::size_t{x}]; }
  [[nodiscard]] unsigned int down(unsigned int x) const {
    return upDown_[2 * std::size_t{x} + 1];
  }

  void generateFirstRow();
  unsigned int addPieceNode(unsigned int piece, unsigned int rot, int x, int y, int z);
  void addVoxelNode(unsigned int col, unsigned int piecenode);
  void getPieceInformation(unsigned int node, unsigned char* tran, int* x, int* y, int* z,
                           unsigned int* piece) const;
  int prepare();
  unsigned int clumpify();
  void checkForTransformedAssemblies(unsigned int pivot, std::unique_ptr<MirrorInfo> mir);

  void cover(unsigned int col);
  void uncover(unsigned int col);
  void coverRow(unsigned int r);
  void uncoverRow(unsigned int r);
  bool tryCoverRow(unsigned int r, unsigned int* columns);
  void removeRow(unsigned int r);
  void removeColumn(unsigned int c);
  bool checkmatrix();

  // ---- the searches ----------------------------------------------------------
  void solution(SearchRun& run);
  [[nodiscard]] Assembly assemblyFromRows(const unsigned int* rows, unsigned int count) const;
  void iterativeMultiSearch(SearchRun& run);
  void generateSubtreeTasks(std::vector<SubtreeTask>& tasks, unsigned int targetTasks,
                            unsigned int maxDepth);
  void parallelMultiSearch(SearchRun& run, unsigned int workers, const SearchOptions& options);
  void reportParallelSolution(SearchRun& run, const unsigned int* solutionRows,
                              unsigned int count);
  [[nodiscard]] bool simdFits(const SearchOptions& options) const;
  [[nodiscard]] bool canUseSimd(const SearchOptions& options) const;
  void simdSearch(SearchRun& run, const SearchOptions& options);
  [[nodiscard]] std::unique_ptr<SimdExactCoverBase> createSimdSolver(SimdKernels kernels) const;

  /// The fraction of the plain search, from where it stands (the original's
  /// getFinished()); computed by the thread running it.
  [[nodiscard]] float serialFraction() const;
  void publishSerialProgress() {
    serialProgress_.store(serialFraction(), std::memory_order_relaxed);
  }

  const Puzzle& puzzle_;
  const Problem& problem_;

  // The nodes, one array per member. colCount is a column for normal nodes
  // and the count for column headers; upDown interleaves up (even) and down
  // (odd).
  std::vector<unsigned int> left_;
  std::vector<unsigned int> right_;
  std::vector<unsigned int> upDown_;
  std::vector<unsigned int> colCount_;

  /// The rows chosen at each depth, the columns covered there, and the
  /// number of pieces placed: the position of the plain search.
  unsigned int pos_ = 0;
  std::vector<unsigned int> rows_;
  std::vector<unsigned int> columns_;

  AssemblerError errorsState_ = AssemblerError::None;
  int errorsParam_ = 0;

  /// Search steps; written by the searching threads, read by anyone.
  std::atomic<std::uint64_t> iterations_{0};

  /// The number of variable voxels left empty in every assembly.
  int holes_ = 0;

  /// First and one past the last column of the variable voxels.
  unsigned int varivoxelStart_ = 0;
  unsigned int varivoxelEnd_ = 0;

  unsigned int piecenumber_ = 0;

  /// The piece reduce() is at.
  std::atomic<unsigned int> reducePiece_{0};

  std::vector<PiecePosition> piecePositions_;

  // rotation rejection
  bool avoidTransformedAssemblies_ = false;
  bool rotationFilterActive_ = false;
  unsigned int avoidTransformedPivot_ = 0;
  std::unique_ptr<MirrorInfo> avoidTransformedMirror_;
  /// Complete rotation analysis.
  bool complete_ = false;

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
