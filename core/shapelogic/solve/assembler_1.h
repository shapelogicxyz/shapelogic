// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Assembler 1: an exact cover with counts, after ideas of Wei-Hwa Huang.
/// Columns have a minimum and a maximum weight instead of "exactly once",
/// so it handles piece counts and ranges (several pieces of one shape, a
/// part that may be used or not) and holes in the result. Every problem
/// assembler 0 cannot take comes here.
/// Ported from src/lib/assembler_1.{h,cpp} onto the shared run layer
/// (decision 0009).

#include "shapelogic/solve/assembler.h"
#include "shapelogic/solve/resume_state.h"
#include "shapelogic/solve/rotation_filter.h"
#include "shapelogic/solve/search_run.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace bt {

class SimdHuangCoverBase;

/// Assembler 1. Rows are piece placements; the columns are the shapes
/// (weight between the part's minimum and maximum), the voxels of the result
/// (at most one, at least one for filled ones) and, when pieces have ranges,
/// one more column that counts the voxels the range pieces fill.
///
/// A run takes one of three searches: the plain search (a state machine
/// over explicit stacks, so that it can stop anywhere and be saved) on the
/// caller's thread; a search split into subtree tasks over several threads;
/// or the vector search (SimdHuangCover), alone or inside each task. A
/// stopped search carries on the way it ran.
class Assembler1 final : public Assembler {
 public:
  /// The version string of saved positions.
  static constexpr const char* kVersion = "2.1";

  /// An assembler for `problem` of `puzzle`; both must outlive it.
  Assembler1(const Puzzle& puzzle, const Problem& problem);
  ~Assembler1() override;

  /// True for every problem.
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
  /// As the original, the version is not looked at.
  AssemblerError restorePosition(std::string_view text, std::string_view version) override;

  [[nodiscard]] bool piecePlacementSupported() const override { return true; }
  [[nodiscard]] unsigned int piecePlacementCount(unsigned int piece) const override;
  unsigned int piecePlacement(unsigned int node, int delta, unsigned int piece,
                              unsigned char* tran, int* x, int* y, int* z) const override;

  void debugStep(unsigned long steps) override;
  [[nodiscard]] Assembly currentAssembly() const override;

 private:
  class Worker;

  /// The matrix nodes, one array per member. colCount is a column for
  /// normal nodes and the count (the summed weight of its rows) for column
  /// headers; weight is a node's weight, and for a column header the weight
  /// of the rows taken so far. A thread that searches on its own takes a
  /// copy.
  struct Links {
    std::vector<unsigned int> left;
    std::vector<unsigned int> right;
    std::vector<unsigned int> up;
    std::vector<unsigned int> down;
    std::vector<unsigned int> colCount;
    std::vector<unsigned int> weight;

    void coverColumnOnly(unsigned int col);
    void uncoverColumnOnly(unsigned int col);
    void coverColumnRows(unsigned int col);
    void uncoverColumnRows(unsigned int col);
    void hideRow(unsigned int r);
    void unhideRow(unsigned int r);
    /// Hides every row that would take one of the columns of `row` past its
    /// maximum, after a separator on `hidden`.
    void hideRows(unsigned int row, std::vector<unsigned int>& hidden,
                  const std::vector<unsigned int>& max);
    /// Brings back the rows hidden since the last separator.
    void unhideRows(std::vector<unsigned int>& hidden);
  };

  /// Where a search stands: a frame per level (taskStack: the state of the
  /// level; nextRowStack: the row it goes on with, or 0 or a header for a
  /// new column), the columns chosen, the rows taken, and the rows hidden
  /// (batches separated by 0). A saved position holds them, and a subtree
  /// task is the stacks at its root.
  struct SearchStacks {
    std::vector<unsigned int> taskStack;
    std::vector<unsigned int> nextRowStack;
    std::vector<unsigned int> columnStack;
    std::vector<unsigned int> rows;
    std::vector<unsigned int> hiddenRows;
  };

  /// The placement a row stands for.
  struct PiecePosition {
    int x, y, z;
    unsigned char transformation;
    unsigned int row;  ///< first node of the row
    unsigned int piece;
  };

  // ---- the matrix ------------------------------------------------------------
  void generateFirstRow(unsigned int columns);
  unsigned int addPieceNode(unsigned int piece, unsigned int rot, int x, int y, int z);
  void addVoxelNode(unsigned int col, unsigned int piecenode);
  void addRangeNode(unsigned int col, unsigned int piecenode, unsigned int weight);
  [[nodiscard]] const PiecePosition& placementOf(unsigned int node) const;
  int prepare(bool hasRange, unsigned int rangeMin, unsigned int rangeMax);
  unsigned int clumpify();
  void removeRow(unsigned int r);
  void removeColumn(unsigned int c);

  [[nodiscard]] bool columnConditionFulfilled(const Links& n, unsigned int col) const;
  [[nodiscard]] bool columnConditionFulfillable(const Links& n, unsigned int col) const;
  [[nodiscard]] bool openColumnConditionsFulfillable(const Links& n) const;
  [[nodiscard]] unsigned int findBestUnclosedColumn(const Links& n) const;
  /// The hole check at the start of a frame: true when more hole columns
  /// are empty, with no row left for them, than there may be holes.
  [[nodiscard]] bool tooManyHoles(const Links& n) const;

  /// Brings `n` (the matrix as built) to where `s` stands: the columns
  /// chosen, the rows taken and hidden, in the order the search did. The
  /// last frame, not begun yet, is left out for a task.
  void replay(Links& n, const SearchStacks& s, bool task) const;
  /// True when `s` can be replayed and searched on without reading out of
  /// bounds (a saved position is checked before it is used).
  [[nodiscard]] bool validStacks(const SearchStacks& s, bool task) const;

  /// One step a checked replay made, so that it can be taken back.
  struct ReplayStep {
    enum class Kind : unsigned char { CloseColumn, CoverRows, HideRow, AddRow };
    Kind kind;
    unsigned int node;
  };
  /// replay() for stacks read from a file (validStacks() first), each step
  /// checked before it is made: a column closed must be open, a column's
  /// rows taken out once, a row hidden must be in the matrix whole. False at
  /// the first step that is not; the steps made are on `done` either way.
  [[nodiscard]] bool replayChecked(Links& n, const SearchStacks& s, bool task,
                                   std::vector<ReplayStep>& done) const;
  /// Takes the steps on `done` back, the last first.
  static void undoReplay(Links& n, std::vector<ReplayStep>& done);

  // ---- the searches ----------------------------------------------------------
  [[nodiscard]] Assembly assemblyFromRows(const std::vector<unsigned int>& rows) const;
  void solution(SearchRun& run);
  void iterative(SearchRun& run);
  void generateTasksAtDepth(SearchRun& run, unsigned int cutoffDepth,
                            std::vector<SearchStacks>& tasks);
  void generateSubtreeTasks(SearchRun& run, std::vector<SearchStacks>& tasks,
                            unsigned int targetTasks, unsigned int maxDepth);
  void parallelMultiSearch(SearchRun& run, unsigned int workers, const SearchOptions& options);
  void reportParallelSolution(SearchRun& run, const std::vector<unsigned int>& solutionRows);
  [[nodiscard]] bool simdFits(const SearchOptions& options) const;
  [[nodiscard]] bool canUseSimd(const SearchOptions& options) const;
  void simdSearch(SearchRun& run, const SearchOptions& options);
  [[nodiscard]] std::unique_ptr<SimdHuangCoverBase> createSimdSolver(SimdKernels kernels) const;
  /// The search is through.
  [[nodiscard]] bool searchDone() const { return stacks_.nextRowStack.empty(); }
  /// The search stands at its start: the first frame not begun. (At its
  /// end it stands on one frame too, with the first column still closed;
  /// that is no start.)
  [[nodiscard]] bool atRoot() const {
    return stacks_.taskStack.size() == 1 && stacks_.taskStack[0] == 0 &&
           stacks_.nextRowStack.size() == 1 && stacks_.rows.empty() &&
           stacks_.columnStack.empty() && stacks_.hiddenRows.empty();
  }

  /// The fraction of the plain search, from where it stands (the original's
  /// getFinished()); computed by the thread running it.
  [[nodiscard]] float serialFraction() const;
  void publishSerialProgress() {
    serialProgress_.store(serialFraction(), std::memory_order_relaxed);
  }

  const Puzzle& puzzle_;
  const Problem& problem_;

  /// The matrix, and the copy of it a run starts from (base_), which the
  /// threads of a split search re-seed from for every task.
  Links links_;
  Links base_;
  /// The weight each column must reach and may not pass.
  std::vector<unsigned int> min_;
  std::vector<unsigned int> max_;
  /// The columns up to `holes_` of which may stay empty.
  std::vector<unsigned int> holeColumns_;
  unsigned int holes_ = 0;
  /// The column headers, node 0 included.
  unsigned int headerNodes_ = 0;
  unsigned int piecenumber_ = 0;
  std::vector<PiecePosition> piecePositions_;

  /// Which assemblies are dropped as rotations of others.
  RotationFilter filter_;

  AssemblerError errorsState_ = AssemblerError::None;
  int errorsParam_ = 0;

  /// The position of the plain search, and how far each level is through
  /// its choices (finishedA_ of finishedB_), for the progress.
  SearchStacks stacks_;
  std::vector<unsigned int> finishedA_;
  std::vector<unsigned int> finishedB_;

  /// Search steps; written by the searching threads, read by anyone.
  std::atomic<std::uint64_t> iterations_{0};

  /// The part createMatrix() and the placement reduce() are at.
  std::atomic<unsigned int> reducePiece_{0};

  // the assembler debugger
  bool debug_ = false;
  long debugLoops_ = 0;

  /// The split (parallel) and vector searches' resumable state.
  SplitSearchState<SearchStacks> split_;
  /// A stopped split search loaded from a file of an older version (flag
  /// 2): its signatures are taken from the rows in the order the plain
  /// search placed them, and it goes on with that kind.
  bool rowOrderSignatures_ = false;

  /// Progress of a search split into tasks (and of the vector search, run
  /// as one task).
  TaskProgress taskProgress_;
  /// Progress of the plain search, published by the thread running it.
  std::atomic<float> serialProgress_{0};
};

}  // namespace bt
