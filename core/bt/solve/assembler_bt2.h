// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The BurrTools 2 assembler: assembler 0's matrix, searched by dancing
/// cells, split over threads that take work from each other. Handles the
/// problems assembler 0 handles (every part exactly one piece).
/// Ported from src/lib/assembler_bt2.{h,cpp} onto the run layer (decision
/// 0009): the branches of a split search are small objects of the
/// assembler instead of copies of it, the work stealing is
/// runWorkStealing() (work_stealing.h), and there is no stop flag.

#include "bt/solve/assembler.h"
#include "bt/solve/dancing_cells.h"
#include "bt/solve/dlx_matrix.h"
#include "bt/solve/rotation_filter.h"
#include "bt/solve/search_run.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace bt {

/// The threads BurrTools 2 assembles with (bt2ChooseAssemblerWorkers): the
/// count asked for when one is, else the hardware's threads less two, at
/// least 1 and at most 16.
[[nodiscard]] unsigned int bt2Threads(unsigned int requested) noexcept;

/// The BurrTools 2 assembler.
///
/// A run with one thread searches the dancing cells of the matrix a slice at
/// a time; with more, the search is split into branches that the threads
/// take from each other (runWorkStealing). A stopped run keeps the branches
/// it had not finished, and the next run takes them up.
///
/// The saved position is the original's: the dancing links position of
/// assembler 0's format, which the dancing cells search never moves from its
/// start ("0 <iterations> (0 0)"). A paused search therefore cannot be
/// continued from a saved position; restorePosition() accepts only a search
/// that has not started, or one that is through: that one is saved as the
/// format's position after the last piece ("<pieces + 1> <iterations> ",
/// what assembler 0 writes when it is through; the original wrote the start
/// for it too, so a solve paused while its last assemblies were still being
/// taken apart lost everything when it was loaded again).
class AssemblerBt2 final : public Assembler {
 public:
  /// The version string of saved positions.
  static constexpr const char* kVersion = "2.0-bt2";

  /// An assembler for `problem` of `puzzle`; both must outlive it.
  AssemblerBt2(const Puzzle& puzzle, const Problem& problem);
  ~AssemblerBt2() override;

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
  [[nodiscard]] std::uint64_t iterations() const override;
  [[nodiscard]] SavedPosition savePosition() const override;
  /// Accepts only the position of a search that has not started ("0 0
  /// (0 0)") or of one that is through ("<pieces + 1> <iterations> "; the
  /// next run finds nothing and is Finished). Any other is
  /// CannotRestoreInterrupted: where the dancing cells search stood is not
  /// saved, so it can only start again, and beginSolving() then drops what
  /// it had found (the original started again and kept it, so every
  /// assembly found before came twice).
  AssemblerError restorePosition(std::string_view text, std::string_view version) override;

  [[nodiscard]] bool piecePlacementSupported() const override { return true; }
  [[nodiscard]] unsigned int piecePlacementCount(unsigned int piece) const override;
  unsigned int piecePlacement(unsigned int node, int delta, unsigned int piece,
                              unsigned char* tran, int* x, int* y, int* z) const override;

  void debugStep(unsigned long steps) override;
  [[nodiscard]] Assembly currentAssembly() const override;

 private:
  /// One part of the search: a dancing cells search and its share of the
  /// progress. What it has done of the whole is shareBase + shareScale *
  /// (cells.progress() - shareOffset): a split divides the estimated rest
  /// between the two searches, and the total does not move. Published by
  /// the thread that searches it, so that others never read the search.
  struct Branch {
    DancingCells cells;
    double shareBase = 0;
    double shareScale = 1;
    double shareOffset = 0;
    std::atomic<float> contribution{0};
    std::atomic<std::uint64_t> liveIterations{0};
    std::atomic<bool> searchDone{false};
  };

  /// What runWorkStealing() calls (work_stealing.h).
  struct Driver;

  void searchSlice(Branch& b, SearchRun& run, unsigned int budget);
  [[nodiscard]] bool branchFinished(const Branch& b) const;
  [[nodiscard]] std::unique_ptr<Branch> splitBranch(Branch& b);
  void retireBranch(Branch& b);
  [[nodiscard]] std::vector<std::unique_ptr<Branch>> takeParked();
  void park(std::unique_ptr<Branch> b);

  void buildCells();
  void solutionFromRowNodes(SearchRun& run, const std::vector<unsigned int>& rowIds);
  void publishProgress(Branch& b);

  const Puzzle& puzzle_;
  const Problem& problem_;

  /// The placement matrix (assembler 0's).
  DlxMatrix matrix_;
  /// Which assemblies are dropped as rotations of others; read only while
  /// a search runs, shared by its threads.
  RotationFilter filter_;

  AssemblerError errorsState_ = AssemblerError::None;
  int errorsParam_ = 0;

  /// The piece createMatrix() and reduce() are at.
  std::atomic<unsigned int> reducePiece_{0};

  /// The search the caller's thread runs; the branches are split off it.
  Branch root_;
  /// The dancing cells have been made from the matrix (at the first run,
  /// after reduce()).
  bool cellsBuilt_ = false;
  /// The root has published its progress at least once.
  std::atomic<bool> searchPublished_{false};
  /// Restored from the position of a search that is through: there is
  /// nothing left to search. Read by finished() from any thread.
  std::atomic<bool> restoredThrough_{false};

  /// Guards everything below: the branches split off the root that are not
  /// through (peers), those a stopped run left (parked; they stay peers),
  /// and what finished branches did.
  mutable std::mutex peerMutex_;
  std::vector<Branch*> peers_;
  std::vector<std::unique_ptr<Branch>> parked_;
  double retiredShare_ = 0;
  /// Iterations of finished branches (and of a restored position).
  std::atomic<std::uint64_t> iterations_{0};
};

}  // namespace bt
