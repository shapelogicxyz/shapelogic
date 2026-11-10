// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/assembler_0.cpp. The search code is the original's;
// what changed is around it: the stop is the run's token, the threads come
// from the pool through runSplitTasks(), the split and vector state is a
// SplitSearchState, the saved position is a string (decision 0009), and the
// matrix is DlxMatrix (dlx_matrix.cpp).
#include "shapelogic/solve/assembler_0.h"

#include "shapelogic/base/int_cast.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/solve/simd_exact_cover.h"

#include <algorithm>
#include <cstdlib>
#include <string>

namespace bt {

namespace {

/// strtol from `s`, cast to unsigned int; returns the characters consumed
/// (the original's getInt, quirks included: no digits read 0).
unsigned int getInt(const char* s, unsigned int* i) {
  char* s2 = nullptr;
  *i = static_cast<unsigned int>(std::strtol(s, &s2, 10));
  return static_cast<unsigned int>(s2 - s);
}

/// The same for the iteration count (the original's getLong).
unsigned int getLong(const char* s, std::uint64_t* i) {
  char* s2 = nullptr;
  *i = static_cast<std::uint64_t>(std::strtol(s, &s2, 10));
  return static_cast<unsigned int>(s2 - s);
}

}  // namespace

Assembler0::Assembler0(const Puzzle& puzzle, const Problem& problem)
    : puzzle_(puzzle), problem_(problem) {}

Assembler0::~Assembler0() = default;

AssemblerError Assembler0::createMatrix(const MatrixOptions& options) {
  BT_ASSERT(problem_.resultValid());

  filter_.complete = options.completeRotations;
  strictColors_ = options.strictColors;
  startLocks_ = options.startLocks;
  freeStarts_ = options.freeStarts;
  partTransforms_ = options.partTransforms;

  if (!canHandle(problem_)) {
    return AssemblerError::PuzzleUnhandable;
  }

  // One more entry than pieces: a search stopped just after placing the last
  // piece stands at pos == pieces, and the position written then includes
  // that entry (the original read past its arrays there).
  rows_.assign(problem_.getNumberOfPieces() + 1, 0);
  columns_.assign(problem_.getNumberOfPieces() + 1, 0);

  errorsState_ =
      matrix_.build(puzzle_, problem_, strictColors_, startLocks_, freeStarts_, partTransforms_,
                    filter_, reducePiece_,
                    errorsParam_);
  if (errorsState_ != AssemblerError::None) {
    return errorsState_;
  }

  pos_ = 0;
  iterations_.store(0, std::memory_order_relaxed);

  filter_.apply(options.keepMirrors, options.keepRotations, options.completeRotations);

  publishSerialProgress();
  return errorsState_;
}

void Assembler0::applySolutionFilterFlags(bool keepMirrors, bool keepRotations, bool complete) {
  filter_.apply(keepMirrors, keepRotations, complete);
}

ReduceStats Assembler0::reduce(const StopToken& stop) { return matrix_.reduce(stop, reducePiece_); }

Assembly Assembler0::currentAssembly() const {
  // nothing placed, or the search is through: an empty assembly
  if (pos_ > matrix_.pieces) {
    return matrix_.emptyAssembly(puzzle_.getGridType());
  }

  return matrix_.assemblyFromRows(puzzle_.getGridType(), rows_.data(), pos_);
}

void Assembler0::prepareRowCheck(const SearchOptions& options) {
  rowCheck_.reset();
  if (!options.rowRotationCheck || !filter_.avoidTransformed) {
    return;
  }

  std::vector<RowRotationCheck::Row> rows;
  rows.reserve(matrix_.placements.size());
  for (const DlxPlacement& pp : matrix_.placements) {
    rows.push_back({pp.piece, Placement{pp.transformation, pp.x, pp.y, pp.z}});
  }
  rowCheck_ = RowRotationCheck::make(puzzle_, problem_, filter_, std::move(rows));

  // a row's nodes are made one after the other, the rows in order
  const std::size_t nodes = matrix_.nodes.left.size();
  if (rowCheck_ != nullptr && rowOfNode_.size() != nodes) {
    rowOfNode_.assign(nodes, 0);
    for (std::size_t i = 0; i < matrix_.placements.size(); i++) {
      const std::size_t end =
          i + 1 < matrix_.placements.size() ? matrix_.placements[i + 1].row : nodes;
      for (std::size_t node = matrix_.placements[i].row; node < end; node++) {
        rowOfNode_[node] = static_cast<std::uint32_t>(i);
      }
    }
  }
}

bool Assembler0::rowCheckRejects(const unsigned int* rows, unsigned int count,
                                 RowRotationCheck::Scratch& scratch) const {
  scratch.found.clear();
  for (unsigned int i = 0; i < count; i++) {
    scratch.found.push_back(rowOfNode_[rows[i]]);
  }
  return rowCheck_->rejects(scratch);
}

// An assembly found by the plain or the vector search on the caller's
// thread: dropped when a rotation of it is smaller, else handed over.
void Assembler0::solution(SearchRun& run) {
  if (run.hasCallback()) {
    if (rowCheck_ != nullptr && pos_ <= matrix_.pieces &&
        rowCheckRejects(rows_.data(), pos_, rowScratch_)) {
      return;
    }

    Assembly assembly = currentAssembly();

    if (rowCheck_ == nullptr && filter_.rejects(assembly, puzzle_, problem_, strictColors_)) {
      return;
    }

    run.deliver(std::move(assembly));
  }
}

float Assembler0::serialFraction() const {
  const DlxNodes& n = matrix_.nodes;
  if (rows_.empty() || columns_.empty() || n.upDown.empty()) {
    return 0;
  }

  if (pos_ > matrix_.pieces) {
    return 1;
  }

  float erg = 0;

  for (int i = asInt(pos_) - 1; i >= 0; i--) {
    const auto ui = static_cast<std::size_t>(i);
    unsigned int r = rows_[ui];
    unsigned int l = n.colCount[columns_[ui]];

    while (l && r && (r != n.down(columns_[ui]))) {
      erg += 1;
      r = n.up(r);
      l--;
    }

    erg /= static_cast<float>(n.colCount[columns_[ui]]);
  }

  return erg;
}

// The plain search: dancing links, iteratively. To follow it, know the
// dancing links algorithm first.
void Assembler0::iterativeMultiSearch(SearchRun& run) {
  DlxNodes& n = matrix_.nodes;
  const unsigned int piecenumber = matrix_.pieces;
  const unsigned int varivoxelEnd = matrix_.varivoxelEnd;
  bool cont = false;
  unsigned int sincePublished = 0;

  while (!run.stopRequested()) {
    // through when pos went below 0 (wrapping past piecenumber)
    if (pos_ > piecenumber) {
      break;
    }

    // all pieces placed and all voxels filled?
    if (!n.right[0]) {
      solution(run);
    }

    // the debugger
    if (debug_) {
      if (debugLoops_ <= 0) {
        break;
      }

      debugLoops_--;
    }

    // with all pieces placed there is nothing more to choose: the remaining
    // columns are empty anyway, so backtrack once
    if (pos_ == piecenumber) {
      pos_--;
      // a problem without pieces is through here (the original read past
      // its row list)
      if (pos_ > piecenumber) {
        break;
      }
    }

    cont = false;
    iterations_.store(iterations_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    if (++sincePublished == 4096) {
      sincePublished = 0;
      publishSerialProgress();
    }

    if (!rows_[pos_]) {
      // a new column: the one with the fewest rows among the piece and
      // filled-voxel columns; a column with none ends this branch
      unsigned int c = n.right[0];
      unsigned int s = n.colCount[c];

      if (s) {
        unsigned int j = n.right[c];

        while (j) {
          if (n.colCount[j] < s) {
            c = j;
            s = n.colCount[c];

            if (!s) {
              break;
            }
          }

          j = n.right[j];
        }
      }

      // more variable voxels that can no longer be filled than there may be
      // holes: backtrack as well
      if (s) {
        auto currentHoles = asUnsigned(matrix_.holes);
        unsigned int j = n.right[varivoxelEnd];

        while (j != varivoxelEnd) {
          if (n.colCount[j] == 0) {
            if (currentHoles == 0) {
              s = 0;
              break;
            }
            currentHoles--;
          }
          j = n.right[j];
        }
      }

      if (s) {
        // a column to search
        columns_[pos_] = c;
        rows_[pos_] = n.down(columns_[pos_]);

        cont = rows_[pos_] != columns_[pos_];
      }

      // no row, or a column with none
      if (!cont) {
        rows_[pos_] = 0;
        pos_--;
        continue;
      }

      n.cover(columns_[pos_]);

    } else {
      // back from the deeper level: uncover our row and take the next
      n.uncoverRow(rows_[pos_]);
      cont = true;

      rows_[pos_] = n.down(rows_[pos_]);

      if (rows_[pos_] == columns_[pos_]) {
        cont = false;
      }
    }

    if (cont) {
      n.coverRow(rows_[pos_]);

      pos_++;

    } else {
      // this column is done: uncover it and backtrack
      n.uncover(columns_[pos_]);

      rows_[pos_] = 0;
      pos_--;
    }
  }

  publishSerialProgress();
}

// ---- the search split over threads -----------------------------------------------

// One thread's search of tasks: a copy of the matrix it covers and uncovers
// on its own (assemblerWorker_c). Its task can get smaller while it is
// searched: when another thread has nothing to do, the rows not tried yet at
// the task's first level go to the task list and the task goes on one level
// deeper; and at a stop the task is replaced by what is left of it.
class Assembler0::Worker {
 public:
  Worker(Assembler0& parent, SearchRun& run, SplitTasks<SubtreeTask>& tasks, unsigned int slot)
      : parent_(parent),
        run_(run),
        tasks_(tasks),
        n_(parent.matrix_.nodes),
        rows_(parent.matrix_.pieces, 0),
        columns_(parent.matrix_.pieces, 0),
        slot_(slot) {}

  /// Searches task `taskIndex` to its end (true) or to a stop (false; the
  /// list then holds what is left of it).
  bool operator()(std::size_t taskIndex, const SubtreeTask& task, std::uint64_t units) {
    taskIndex_ = taskIndex;
    units_ = units;
    const bool through = searchSubtree(task);
    flushIterations();
    return through;
  }

 private:
  // How far through the task: at each step below its root, the choices
  // done out of all of them, every choice taken as alike.
  [[nodiscard]] float taskFraction() {
    float erg = 0;
    for (int i = asInt(pos_) - 1; i >= asInt(rootDepth_); i--) {
      const auto ui = static_cast<std::size_t>(i);
      const unsigned int col = columns_[ui];
      const unsigned int cnt = n_.colCount[col];
      if (!cnt) {
        continue;
      }
      unsigned int r = rows_[ui];
      unsigned int l = cnt;
      while (l && r && (r != n_.down(col))) {
        erg += 1;
        r = n_.up(r);
        l--;
      }
      erg /= static_cast<float>(cnt);
    }
    return erg;
  }

  bool solution() {
    flushIterations();
    return parent_.reportParallelSolution(run_, rows_.data(), pos_, rowScratch_);
  }

  // The steps of the first `depth` levels as a task.
  [[nodiscard]] SubtreeTask taskOf(unsigned int depth) const {
    SubtreeTask t;
    t.prefix.reserve(depth + 1);
    for (unsigned int d = 0; d < depth; d++) {
      t.prefix.push_back(PrefixStep{columns_[d], rows_[d]});
    }
    return t;
  }

  // The rows of level `level` after the one in hand, as tasks, each `part`
  // over the number of rows there; returns that share (the row in hand's).
  double branchesAt(unsigned int level, double part, TaskSplit<SubtreeTask>& out) const {
    const unsigned int col = columns_[level];
    const unsigned int count = n_.colCount[col];
    const double each = count > 0 ? part / static_cast<double>(count) : part;
    for (unsigned int r = n_.down(rows_[level]); r != col; r = n_.down(r)) {
      SubtreeTask t = taskOf(level);
      t.prefix.push_back(PrefixStep{col, r});
      out.branches.push_back(std::move(t));
      out.shares.push_back(each);
    }
    return each;
  }

  // Another thread has nothing to do: the first level of the task that has
  // rows not tried yet gives them away, and the task is the row in hand
  // there from now on.
  void handOff() {
    const unsigned int piecenumber = parent_.matrix_.pieces;
    TaskSplit<SubtreeTask> out;
    double part = 1.0;
    for (unsigned int level = rootDepth_; level < pos_ && level < piecenumber; level++) {
      if (piecenumber < level + kSplitLeafRows) {
        return;
      }
      part = branchesAt(level, part, out);
      if (out.branches.empty()) {
        continue;
      }
      rootDepth_ = level + 1;
      out.rest = taskOf(rootDepth_);
      out.restShare = part;
      out.restFraction = taskFraction();
      units_ = tasks_.handOff(slot_, taskIndex_, std::move(out));
      return;
    }
  }

  // The run is stopped, at the top of the search loop: the task is replaced
  // by what is left of it. That is the rows not tried yet at every level,
  // and the node the search stands at when it has not begun it (a level
  // whose row is in hand has finished that row: the search came back to it).
  void leave() {
    const unsigned int piecenumber = parent_.matrix_.pieces;
    if (pos_ > piecenumber) {
      return;
    }
    const bool begun = pos_ < piecenumber && rows_[pos_] != 0;
    // nothing of the task begun: it stays the task it is
    if (!begun && pos_ == rootDepth_) {
      return;
    }

    TaskSplit<SubtreeTask> out;
    double part = 1.0;
    const unsigned int end = begun ? pos_ + 1 : pos_;
    for (unsigned int level = rootDepth_; level < end; level++) {
      part = branchesAt(level, part, out);
    }
    if (!begun) {
      out.branches.push_back(taskOf(pos_));
      out.shares.push_back(part);
    }
    tasks_.leave(slot_, taskIndex_, std::move(out));
  }

  bool searchSubtree(const SubtreeTask& task) {
    const unsigned int piecenumber = parent_.matrix_.pieces;
    const unsigned int varivoxelEnd = parent_.matrix_.varivoxelEnd;

    // apply the prefix
    rootDepth_ = static_cast<unsigned int>(task.prefix.size());
    for (unsigned int d = 0; d < rootDepth_; d++) {
      n_.cover(task.prefix[d].col);
      n_.coverRow(task.prefix[d].row);
      columns_[d] = task.prefix[d].col;
      rows_[d] = task.prefix[d].row;
    }
    pos_ = rootDepth_;

    bool cont = false;
    bool stopped = false;

    for (;;) {
      // through first: a task searched to its end is finished, stop or not
      if (pos_ < rootDepth_ || pos_ > piecenumber) {
        break;
      }
      if (run_.stopRequested()) {
        leave();
        stopped = true;
        break;
      }

      // An assembly found once the run is stopped is not handed over: this
      // node is left as not begun, and the next run finds it.
      if (!n_.right[0] && !solution()) {
        leave();
        stopped = true;
        break;
      }

      if (pos_ == piecenumber) {
        pos_--;
        if (pos_ < rootDepth_ || pos_ > piecenumber) {
          break;
        }
      }

      cont = false;
      localIterations_++;
      if ((localIterations_ - flushedIterations_) >= 128) {
        parent_.iterations_.fetch_add(localIterations_ - flushedIterations_,
                                      std::memory_order_relaxed);
        flushedIterations_ = localIterations_;
        if (tasks_.wanted()) {
          handOff();
        }
        if ((++flushCount_ & 31) == 0) {
          parent_.taskProgress_.reportUnits(slot_, units_, taskFraction());
        }
      }

      if (!rows_[pos_]) {
        // the best column
        unsigned int c = n_.right[0];
        unsigned int s = n_.colCount[c];

        if (s) {
          unsigned int j = n_.right[c];
          while (j) {
            if (n_.colCount[j] < s) {
              c = j;
              s = n_.colCount[c];
              if (!s) {
                break;
              }
            }
            j = n_.right[j];
          }
        }

        // holes among the variable voxels
        if (s) {
          auto currentHoles = asUnsigned(parent_.matrix_.holes);
          unsigned int j = n_.right[varivoxelEnd];
          while (j != varivoxelEnd) {
            if (n_.colCount[j] == 0) {
              if (currentHoles == 0) {
                s = 0;
                break;
              }
              currentHoles--;
            }
            j = n_.right[j];
          }
        }

        if (s) {
          columns_[pos_] = c;
          rows_[pos_] = n_.down(columns_[pos_]);
          cont = true;
        }

        if (!cont) {
          rows_[pos_] = 0;
          pos_--;
          if (pos_ < rootDepth_) {
            break;
          }
          continue;
        }

        n_.cover(columns_[pos_]);
      } else {
        n_.uncoverRow(rows_[pos_]);
        cont = true;
        rows_[pos_] = n_.down(rows_[pos_]);
        if (rows_[pos_] == columns_[pos_]) {
          cont = false;
        }
      }

      if (cont) {
        n_.coverRow(rows_[pos_]);
        pos_++;
      } else {
        n_.uncover(columns_[pos_]);
        rows_[pos_] = 0;
        pos_--;
        if (pos_ < rootDepth_) {
          break;
        }
      }
    }

    // unwind what the task stood on (its prefix, and the levels a hand-off
    // added to it) when it was searched to its end; after a stop the copy
    // stays as it is (this thread takes no further task)
    if (!stopped) {
      for (int d = asInt(rootDepth_) - 1; d >= 0; d--) {
        const auto ud = static_cast<std::size_t>(d);
        n_.uncoverRow(rows_[ud]);
        n_.uncover(columns_[ud]);
        rows_[ud] = 0;
        columns_[ud] = 0;
      }
      pos_ = 0;
    }
    return !stopped;
  }

  void flushIterations() {
    const std::uint64_t unflushed = localIterations_ - flushedIterations_;
    if (unflushed > 0) {
      parent_.iterations_.fetch_add(unflushed, std::memory_order_relaxed);
      flushedIterations_ = localIterations_;
    }
  }

  Assembler0& parent_;
  SearchRun& run_;
  SplitTasks<SubtreeTask>& tasks_;
  /// This thread's copy of the matrix.
  DlxNodes n_;
  std::vector<unsigned int> rows_;
  std::vector<unsigned int> columns_;
  unsigned int pos_ = 0;
  /// The level the task in hand starts at: its prefix, and the levels a
  /// hand-off has added to it.
  unsigned int rootDepth_ = 0;
  std::uint64_t localIterations_ = 0;
  std::uint64_t flushedIterations_ = 0;
  /// The progress slot this thread reports to, the task it is in and its
  /// units, and a count of flushes so that it reports only now and then.
  unsigned int slot_;
  std::size_t taskIndex_ = 0;
  std::uint64_t units_ = 0;
  unsigned int flushCount_ = 0;
  RowRotationCheck::Scratch rowScratch_;
};

// An assembly found by a search split over threads: built from the rows of
// the cover (in any order), checked against its rotated copies and against
// what was reported before, and handed over. Called by the worker threads,
// the plain ones and the vector ones alike.
bool Assembler0::reportParallelSolution(SearchRun& run, const unsigned int* solutionRows,
                                        unsigned int count, RowRotationCheck::Scratch& scratch) {
  if (!run.hasCallback()) {
    return true;
  }

  if (rowCheck_ != nullptr && rowCheckRejects(solutionRows, count, scratch)) {
    return true;
  }

  Assembly assembly = matrix_.assemblyFromRows(puzzle_.getGridType(), solutionRows, count);

  if (rowCheck_ == nullptr && filter_.rejects(assembly, puzzle_, problem_, strictColors_)) {
    return true;
  }

  // tasks that never overlap and are left exactly: nothing is found twice
  if (split_.exact) {
    return run.deliverUnlessStopped(std::move(assembly));
  }

  // the signature: what tells this assembly from the others; an unplaced
  // piece counts as transformation 0 at the origin, as in the original
  std::uint64_t sig = kFnvOffset;
  for (const Placement& p : assembly.getPlacements()) {
    const bool placed = p.transformation != kUnplacedTrans;
    sig = fnvMix(sig, placed ? p.transformation : 0U);
    sig = fnvMix(sig, static_cast<std::uint32_t>(placed ? p.xpos : 0));
    sig = fnvMix(sig, static_cast<std::uint32_t>(placed ? p.ypos : 0));
    sig = fnvMix(sig, static_cast<std::uint32_t>(placed ? p.zpos : 0));
  }

  return run.deliverOnce(std::move(assembly), sig, split_.reported);
}

void Assembler0::generateSubtreeTasks(std::vector<SubtreeTask>& tasks, unsigned int targetTasks,
                                      unsigned int maxDepth) {
  DlxNodes& n = matrix_.nodes;
  const unsigned int piecenumber = matrix_.pieces;
  const unsigned int varivoxelEnd = matrix_.varivoxelEnd;

  tasks.clear();
  tasks.emplace_back();

  unsigned int currentDepth = 0;
  const unsigned int maxPrefix = piecenumber > 1 ? piecenumber - 1 : 1U;

  while (tasks.size() < targetTasks && currentDepth < maxDepth) {
    std::vector<SubtreeTask> nextTasks;
    bool anyExpanded = false;

    for (const SubtreeTask& t : tasks) {
      if (t.prefix.size() >= maxPrefix) {
        nextTasks.push_back(t);
        continue;
      }

      for (const PrefixStep& step : t.prefix) {
        n.cover(step.col);
        n.coverRow(step.row);
      }

      if (!n.right[0]) {
        nextTasks.push_back(t);
        for (int d = asInt(t.prefix.size()) - 1; d >= 0; d--) {
          n.uncoverRow(t.prefix[static_cast<std::size_t>(d)].row);
          n.uncover(t.prefix[static_cast<std::size_t>(d)].col);
        }
        continue;
      }

      unsigned int c = n.right[0];
      unsigned int s = n.colCount[c];

      if (s) {
        unsigned int j = n.right[c];
        while (j) {
          if (n.colCount[j] < s) {
            c = j;
            s = n.colCount[c];
            if (!s) {
              break;
            }
          }
          j = n.right[j];
        }
      }

      if (s) {
        auto currentHoles = asUnsigned(matrix_.holes);
        unsigned int j = n.right[varivoxelEnd];
        while (j != varivoxelEnd) {
          if (n.colCount[j] == 0) {
            if (currentHoles == 0) {
              s = 0;
              break;
            }
            currentHoles--;
          }
          j = n.right[j];
        }
      }

      if (s > 0) {
        anyExpanded = true;
        for (unsigned int r = n.down(c); r != c; r = n.down(r)) {
          SubtreeTask child = t;
          child.prefix.push_back(PrefixStep{c, r});
          child.weight = t.weight / static_cast<double>(s);
          nextTasks.push_back(std::move(child));
        }
      }

      for (int d = asInt(t.prefix.size()) - 1; d >= 0; d--) {
        n.uncoverRow(t.prefix[static_cast<std::size_t>(d)].row);
        n.uncover(t.prefix[static_cast<std::size_t>(d)].col);
      }
    }

    if (!anyExpanded) {
      break;
    }

    tasks = std::move(nextTasks);
    currentDepth++;
  }
}

void Assembler0::parallelMultiSearch(SearchRun& run, unsigned int workers,
                                     const SearchOptions& options) {
  const unsigned int piecenumber = matrix_.pieces;

  // the workers read the shapes (smallerRotationExists): their lazy caches
  // must be filled before any worker exists
  warmShapeCaches(puzzle_, problem_);

  if (split_.tasks.empty()) {
    // one thread with the vector search: one task, the whole search (the
    // split is the plain search's choice of rows, which the vector search
    // makes better by itself)
    const bool whole = workers == 1 && simdFits(options);
    const unsigned int targetTasks =
        whole ? 1 : options.splitTasks > 0 ? options.splitTasks : splitTaskTarget(workers);
    const unsigned int maxDepth =
        std::min(piecenumber > 1 ? piecenumber - 1 : 1U,
                 options.splitDepth > 0 ? options.splitDepth : kSplitDepth);
    generateSubtreeTasks(split_.tasks, targetTasks, maxDepth);
    split_.completed.assign(split_.tasks.size(), 0);
    split_.weights.clear();
    for (const SubtreeTask& t : split_.tasks) {
      split_.weights.push_back(t.weight);
    }
    split_.doneWeight = 0;
    // prefixes of the rows of one column each: no two hold the same
    // assembly, and a stop leaves each where it stood
    split_.exact = true;
  }

  taskProgress_.begin(split_.weights, split_.completed, split_.doneWeight);

  if (split_.tasks.empty()) {
    return;
  }

  if (std::find(split_.completed.begin(), split_.completed.end(), 0) ==
      split_.completed.end()) {
    // through: as at the end of a run below, so that a saved position says
    // so (the original kept the tasks and wrote flag 2 past the last piece,
    // which no restore accepts)
    pos_ = piecenumber + 1;
    split_.clearTasks();
    split_.interrupted = false;
    return;
  }

  // When the matrix fits, every thread searches its tasks with the vector
  // search: one solver, read only, shared by all. A task is the rows chosen
  // so far, and the covers below it are the covers that hold those rows
  // whatever order the rest is chosen in, so the vector search finds the
  // same ones the plain search would. Tasks, completion and signatures stay
  // what they are, and with them stopping, continuing and saving.
  std::unique_ptr<SimdExactCoverBase> simdSolver;
  if (simdFits(options)) {
    simdSolver = createSimdSolver(options.kernels);
  }
  const SimdExactCoverBase* simd = simdSolver.get();

  SplitTasks<SubtreeTask> tasks(split_, run, taskProgress_);

  // One thread's search of its tasks: the plain worker, or the vector
  // search, whose branches (rows by their nodes) become tasks here.
  struct TaskSearch final : SubtreeSplitter {
    TaskSearch(Assembler0& a, SearchRun& r, SplitTasks<SubtreeTask>& t,
               const SimdExactCoverBase* solver, unsigned int s)
        : self(a), run(r), tasks(t), simd(solver), slot(s) {
      if (simd == nullptr) {
        plain = std::make_unique<Worker>(self, run, tasks, slot);
      }
    }

    bool operator()(std::size_t index, const SubtreeTask& task, std::uint64_t taskUnits) {
      if (plain) {
        return (*plain)(index, task, taskUnits);
      }

      taskIndex = index;
      units = taskUnits;
      prefixRows.clear();
      for (const PrefixStep& step : task.prefix) {
        prefixRows.push_back(step.row);
      }

      std::atomic<std::uint64_t> taskIter{0};
      auto drain = [this, &taskIter] {
        self.iterations_.fetch_add(taskIter.exchange(0, std::memory_order_relaxed),
                                   std::memory_order_relaxed);
      };
      const bool through = simd->solveSubtree(
          prefixRows,
          [this, &drain](const std::vector<unsigned int>& nodes) -> bool {
            drain();
            return self.reportParallelSolution(
                run, nodes.data(), static_cast<unsigned int>(nodes.size()), rowScratch);
          },
          run.token(), taskIter,
          [this, &drain](float fraction) {
            drain();
            self.taskProgress_.reportUnits(slot, units, fraction);
          },
          this);
      drain();
      return through;
    }

    [[nodiscard]] bool wanted() const override { return tasks.wanted(); }

    void handOff(SubtreeBranches&& branches) override {
      TaskSplit<SubtreeTask> split = toSplit(branches);
      split.rest = toTask(branches.rest);
      split.restShare = branches.restShare;
      split.restFraction = branches.restFraction;
      units = tasks.handOff(slot, taskIndex, std::move(split));
    }

    void leave(SubtreeBranches&& branches) override {
      tasks.leave(slot, taskIndex, toSplit(branches));
    }

    // A task of the rows `nodes` name: each the node of its row in the
    // piece's column, which is open as long as the piece is not placed.
    [[nodiscard]] SubtreeTask toTask(const std::vector<unsigned int>& nodes) const {
      SubtreeTask t;
      t.prefix.reserve(nodes.size());
      for (const unsigned int node : nodes) {
        t.prefix.push_back(PrefixStep{self.matrix_.nodes.colCount[node], node});
      }
      return t;
    }

    [[nodiscard]] TaskSplit<SubtreeTask> toSplit(const SubtreeBranches& branches) const {
      TaskSplit<SubtreeTask> split;
      split.shares = branches.shares;
      split.branches.reserve(branches.branches.size());
      for (const std::vector<unsigned int>& nodes : branches.branches) {
        split.branches.push_back(toTask(nodes));
      }
      return split;
    }

    Assembler0& self;
    SearchRun& run;
    SplitTasks<SubtreeTask>& tasks;
    const SimdExactCoverBase* simd;
    unsigned int slot;
    std::unique_ptr<Worker> plain;
    std::vector<unsigned int> prefixRows;
    std::size_t taskIndex = 0;
    std::uint64_t units = 0;
    RowRotationCheck::Scratch rowScratch;
  };

  runSplitTasks(tasks, workers, options.pool, run, [this, &run, &tasks, simd](unsigned int slot) {
    return TaskSearch(*this, run, tasks, simd, slot);
  });

  if (std::find(split_.completed.begin(), split_.completed.end(), 0) ==
      split_.completed.end()) {
    pos_ = piecenumber + 1;
    split_.clearTasks();
    split_.interrupted = false;
  } else if (split_.tasks.size() == 1 && split_.tasks[0].prefix.empty()) {
    // Stopped with the whole search still one task: nothing of it was begun
    // (a task begun is replaced by what is left of it). The search stands
    // at its start, and the next run may search another way.
    split_.clearTasks();
    split_.interrupted = false;
  } else {
    // Stopped part way. The tasks are what is left of the search: the ones
    // not begun, and in place of each one a thread was in, the branches it
    // had not tried (the finished ones are dropped). The signatures see to
    // what a task searched again would find again. A saved position carries
    // all of it (flag 2).
    split_.prune();
    split_.interrupted = true;
  }
}

// ---- the vector search -----------------------------------------------------------

// Whether the matrix can go to the vector search at all. Holes and variable
// voxels do not keep it out: the variable columns are optional ones there
// and the holes a budget. The limit is 2048 columns, the variable ones
// included: above it the row masks, the masks passed down the recursion and
// the per-node column counts cost more than the plain search does.
bool Assembler0::simdFits(const SearchOptions& options) const {
  if (!options.vectorSearch) {
    return false;
  }
  if (debug_) {
    return false;
  }
  if (matrix_.varivoxelEnd == 0 || matrix_.varivoxelEnd - 1 > 2048) {
    return false;
  }
  return true;
}

bool Assembler0::canUseSimd(const SearchOptions& options) const {
  return atStart() && simdFits(options);
}

std::unique_ptr<SimdExactCoverBase> Assembler0::createSimdSolver(SimdKernels kernels) const {
  const DlxNodes& n = matrix_.nodes;
  const unsigned int varivoxelEnd = matrix_.varivoxelEnd;

  // Matrix column c is solver column c-1. The piece and filled-voxel columns
  // come first (1 .. varivoxelStart-1, the header list), the variable voxels
  // after them up to varivoxelEnd-1.
  const unsigned int maxCol = varivoxelEnd - 1;

  std::unique_ptr<SimdExactCoverBase> solver =
      makeSimdExactCover(maxCol, matrix_.pieces, kernels);

  for (unsigned int c = n.right[0]; c != 0; c = n.right[c]) {
    if (c <= maxCol) {
      solver->setRequiredColumn(c - 1);
    }
  }

  // the variable voxels are a list of their own, which the plain search
  // never branches on either
  for (unsigned int c = n.right[varivoxelEnd]; c != varivoxelEnd; c = n.right[c]) {
    solver->setOptionalColumn(c - 1);
  }
  solver->setHoleBudget(asUnsigned(matrix_.holes));

  for (unsigned int p = 1; p <= matrix_.pieces; p++) {
    for (unsigned int row = n.down(p); row != p; row = n.down(row)) {
      std::vector<unsigned int> cols;
      std::vector<unsigned int> nodesInRow;
      unsigned int curr = row;
      do {
        nodesInRow.push_back(curr);
        const unsigned int col = n.colCount[curr];
        if (col > 0 && col <= maxCol) {
          cols.push_back(col - 1);
        }
        curr = n.right[curr];
      } while (curr != row);

      const std::uint32_t rowIdx = solver->addRow(row, p - 1, cols);
      for (const unsigned int node : nodesInRow) {
        solver->registerNodeAlias(node, rowIdx);
      }
    }
  }

  return solver;
}

void Assembler0::simdSearch(SearchRun& run, const SearchOptions& options) {
  auto solver = createSimdSolver(options.kernels);
  std::atomic<std::uint64_t> simdIter{0};

  // reported as one task, so that finished() has something to say while it
  // runs; the iterations go over as it goes, not only at the end
  taskProgress_.begin({1.0}, {0});

  auto drain = [this, &simdIter] {
    iterations_.fetch_add(simdIter.exchange(0, std::memory_order_relaxed),
                          std::memory_order_relaxed);
  };

  split_.vectorDone = 0;
  solver->solve(
      [this, &run, &drain](const std::vector<unsigned int>& solutionNodes) -> bool {
        drain();
        // already reported before the search was stopped
        if (split_.vectorDone++ < split_.vectorSkip) {
          return !run.stopRequested();
        }
        if (solutionNodes.size() > rows_.size()) {
          rows_.resize(solutionNodes.size());
        }
        for (std::size_t i = 0; i < solutionNodes.size(); i++) {
          rows_[i] = solutionNodes[i];
        }
        pos_ = static_cast<unsigned int>(solutionNodes.size());
        solution(run);
        return !run.stopRequested();
      },
      run.token(), simdIter,
      [this, &drain](float fraction) {
        drain();
        taskProgress_.report(0, 0, fraction);
      });

  drain();
  if (!run.stopRequested()) {
    pos_ = matrix_.pieces + 1;
    taskProgress_.finish(0, 0);
    split_.vectorSkip = 0;
    split_.interrupted = false;
  } else {
    // rows and pos held the last solution, not a place in the matrix: back
    // to the start, passing over what was reported on the next run. The rows
    // are cleared too: savePosition() writes rows[0], and restorePosition()
    // would cover it as if the search stood there.
    pos_ = 0;
    std::fill(rows_.begin(), rows_.end(), 0U);
    std::fill(columns_.begin(), columns_.end(), 0U);
    split_.vectorSkip = std::max(split_.vectorSkip, split_.vectorDone);
    split_.interrupted = true;
  }
}

// ---- runs, progress and positions --------------------------------------------------

RunEnd Assembler0::assemble(const AssemblyCallback& callback, const SearchOptions& options,
                            const StopToken& stop) {
  BT_ASSERT_MSG(errorsState_ == AssemblerError::None, "assemble() after a failed createMatrix()");

  debug_ = false;

  SearchRun run(stop, callback);
  prepareRowCheck(options);
  const unsigned int threads = effectiveThreads(options.threads, options.pool);

  // A stopped search carries on the way it ran: the split search with its
  // remaining tasks, whatever the thread count; and a vector search an
  // older version stopped (which kept only how many assemblies it had
  // reported) passing over those, also when this run asks for no vector
  // search (the plain search cannot skip). The vector search on one thread
  // is a split search of one task, the whole search: stopped, it is left as
  // the branches it had not tried and carries on from them, where a vector
  // search by itself would have to begin again.
  SearchOptions vectorAnyway = options;
  vectorAnyway.vectorSearch = true;
  if (split_.vectorSkip > 0 && canUseSimd(vectorAnyway)) {
    simdSearch(run, vectorAnyway);
  } else if (atStart() && (threads > 1 || !split_.tasks.empty() || canUseSimd(options))) {
    parallelMultiSearch(run, threads, options);
  } else {
    // not the fraction of an earlier search that reported by tasks
    taskProgress_.reset();
    // What a save writes from now on is the plain search's position. A
    // vector search stopped before it reported anything left the flag of a
    // stopped vector search behind; with it, the save would be a flag 3
    // position away from the start, which no restore accepts (the
    // original's bug too).
    split_.interrupted = false;
    iterativeMultiSearch(run);
  }

  return pos_ > matrix_.pieces ? RunEnd::Finished : RunEnd::Stopped;
}

float Assembler0::finished() const {
  if (taskProgress_.active()) {
    return taskProgress_.fraction();
  }
  return serialProgress_.load(std::memory_order_relaxed);
}

AssemblerError Assembler0::restorePosition(std::string_view text, std::string_view version) {
  // the matrix must stand at the start, otherwise the stack would need
  // cleaning
  BT_ASSERT_MSG(!rows_.empty(), "restorePosition() before createMatrix()");
  BT_ASSERT(atStart());
  split_ = SplitSearchState<SubtreeTask>{};

  if (version != kVersion) {
    return AssemblerError::CannotRestoreVersion;
  }

  const unsigned int piecenumber = matrix_.pieces;
  const std::string str(text);  // NUL-terminated for the number readers
  const char* string = str.c_str();
  const auto len = static_cast<unsigned int>(str.size());
  unsigned int spos = 0;

  // The leading flag: 0 a position of the plain search, 2 a stopped split
  // search with its tasks and the assemblies it reported, 3 a stopped vector
  // search with how many it reported. 1, from older versions, is a stopped
  // split search saved without those, which cannot be continued.
  unsigned int interrupted = 0;
  spos += getInt(string + spos, &interrupted);
  if (spos >= len) {
    return AssemblerError::CannotRestoreSyntax;
  }
  if (interrupted == 1) {
    return AssemblerError::CannotRestoreInterrupted;
  }
  if (interrupted > 3) {
    return AssemblerError::CannotRestoreSyntax;
  }

  unsigned int pos = 0;
  spos += getInt(string + spos, &pos);
  if (spos >= len) {
    return AssemblerError::CannotRestoreSyntax;
  }

  std::uint64_t it = 0;
  spos += getLong(string + spos, &it);
  if (spos >= len) {
    return AssemblerError::CannotRestoreSyntax;
  }

  // read into these first: nothing changes unless the whole position is good
  std::vector<unsigned int> rows(rows_.size(), 0);
  std::vector<unsigned int> columns(columns_.size(), 0);

  if (pos <= piecenumber) {
    for (unsigned int i = 0; i <= pos; i++) {
      while ((spos < len) && (string[spos] != '(')) {
        spos++;
      }
      spos++;
      if (spos >= len) {
        return AssemblerError::CannotRestoreSyntax;
      }

      spos += getInt(string + spos, &rows[i]);
      if (spos >= len) {
        return AssemblerError::CannotRestoreSyntax;
      }
      spos += getInt(string + spos, &columns[i]);
      if (spos >= len) {
        return AssemblerError::CannotRestoreSyntax;
      }

      while ((spos < len) && (string[spos] != ')')) {
        spos++;
      }
      spos++;
      // after the last ')' the text may end
      if ((i < pos) && (spos >= len)) {
        return AssemblerError::CannotRestoreSyntax;
      }
    }
  }
  // A position past the last piece is a finished search, which has no rows
  // to read (the original read past its row list here).

  SplitSearchState<SubtreeTask> tail;
  if (interrupted >= 2) {
    if (pos != 0) {
      return AssemblerError::CannotRestoreSyntax;
    }
    std::size_t done = 0;
    const bool ok = readResumeTail(
        string + spos, interrupted, tail, done, [piecenumber](ResumeReader& r, SubtreeTask& t) {
          unsigned long long k = 0;
          // a worker has room for one row per piece
          if (!r.next(k) || k > piecenumber) {
            return false;
          }
          t.prefix.resize(static_cast<std::size_t>(k));
          for (PrefixStep& st : t.prefix) {
            if (!r.next(st.col) || !r.next(st.row)) {
              return false;
            }
          }
          return true;
        });
    if (!ok) {
      return AssemblerError::CannotRestoreSyntax;
    }
    tail.interrupted = true;
    // With no assembly reported, none can be found again: the tasks of a
    // search this version stopped, or of one that had found nothing yet.
    tail.exact = tail.reported.empty();

    // Such a search starts from the untouched matrix. A file written before
    // the rows were cleared on a stop may still name a row here (the last
    // solution's first); covering it below would take the matrix apart.
    rows[0] = 0;
    columns[0] = 0;
  }

  // Each task's prefix must be one the split search could have made, and
  // the position's levels a place the plain search could have stood: both
  // are covered step by step, each step checked first (the original covered
  // whatever it read, out of bounds on a damaged file). A task is covered
  // and uncovered again; the position stays covered.
  for (const SubtreeTask& t : tail.tasks) {
    const unsigned int covered = coverChecked(t.prefix);
    uncoverLevels(t.prefix, covered);
    if (covered != t.prefix.size()) {
      return AssemblerError::CannotRestoreSyntax;
    }
  }

  std::vector<PrefixStep> levels;
  if (pos <= piecenumber) {
    // the level the search stands at has a row too when it is between two
    // of its rows (backtracking into it)
    const unsigned int count =
        pos < piecenumber && rows[pos] == 0 ? pos : std::min(pos + 1, piecenumber);
    for (unsigned int p = 0; p < count; p++) {
      levels.push_back(PrefixStep{columns[p], rows[p]});
    }
  }
  const unsigned int covered = coverChecked(levels);
  if (covered != levels.size()) {
    uncoverLevels(levels, covered);
    return AssemblerError::CannotRestoreSyntax;
  }

  rows_ = std::move(rows);
  columns_ = std::move(columns);
  pos_ = pos;
  iterations_.store(it, std::memory_order_relaxed);
  split_ = std::move(tail);
  if (interrupted == 2) {
    taskProgress_.begin(split_.weights, split_.completed, split_.doneWeight);
  }

  publishSerialProgress();
  return AssemblerError::None;
}

// Each level's column must be one the search could have chosen there (a
// piece or filled-voxel column still in the header list) and its row one of
// that column's rows still in the matrix.
unsigned int Assembler0::coverChecked(const std::vector<PrefixStep>& levels) {
  DlxNodes& n = matrix_.nodes;
  const std::size_t nodeCount = n.left.size();

  unsigned int covered = 0;
  for (const PrefixStep& step : levels) {
    if (step.col == 0 || step.col >= matrix_.varivoxelStart || step.row <= matrix_.varivoxelEnd ||
        step.row >= nodeCount || n.colCount[step.row] != step.col) {
      break;
    }
    bool open = false;
    for (unsigned int c = n.right[0]; c != 0 && !open; c = n.right[c]) {
      open = c == step.col;
    }
    bool present = false;
    for (unsigned int r = n.down(step.col); r != step.col && !present; r = n.down(r)) {
      present = r == step.row;
    }
    if (!open || !present) {
      break;
    }
    n.cover(step.col);
    n.coverRow(step.row);
    covered++;
  }
  return covered;
}

void Assembler0::uncoverLevels(const std::vector<PrefixStep>& levels, unsigned int count) {
  DlxNodes& n = matrix_.nodes;
  while (count > 0) {
    count--;
    n.uncoverRow(levels[count].row);
    n.uncover(levels[count].col);
  }
}

SavedPosition Assembler0::savePosition() const {
  BT_ASSERT_MSG(pos_ > matrix_.pieces || pos_ < rows_.size(),
                "savePosition() before createMatrix()");

  // the leading flag, see restorePosition()
  const unsigned int flag = !split_.interrupted ? 0 : split_.tasks.empty() ? 3 : 2;

  std::string out = std::to_string(flag) + " ";
  out += std::to_string(pos_) + " " + std::to_string(iterations_.load(std::memory_order_relaxed)) +
         " ";

  if (pos_ <= matrix_.pieces) {
    for (unsigned int j = 0; j <= pos_; j++) {
      out += "(" + std::to_string(rows_[j]) + " " + std::to_string(columns_[j]) + ")";

      if (j < pos_) {
        out += " ";
      }
    }
  }

  writeResumeTail(out, flag, std::max(split_.vectorSkip, split_.vectorDone), split_,
                  [](std::string& o, const SubtreeTask& t) {
                    o += " " + std::to_string(t.prefix.size());
                    for (const PrefixStep& st : t.prefix) {
                      o += " " + std::to_string(st.col) + " " + std::to_string(st.row);
                    }
                  });

  return SavedPosition{kVersion, std::move(out)};
}

unsigned int Assembler0::piecePlacement(unsigned int node, int delta, unsigned int piece,
                                        unsigned char* tran, int* x, int* y, int* z) const {
  return matrix_.piecePlacement(node, delta, piece, tran, x, y, z);
}

unsigned int Assembler0::piecePlacementCount(unsigned int piece) const {
  return matrix_.piecePlacementCount(piece);
}

void Assembler0::debugStep(unsigned long steps) {
  debug_ = true;
  debugLoops_ = static_cast<long>(steps);
  const AssemblyCallback none;
  SearchRun run(StopToken{}, none);
  iterativeMultiSearch(run);
}

bool Assembler0::canHandle(const Problem& problem) { return everyPartOnePiece(problem); }

}  // namespace bt
