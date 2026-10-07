// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/assembler_bt2.cpp and bt2ChooseAssemblerWorkers in
// src/lib/bt2_assemble.cpp. The matrix is assembler 0's (DlxMatrix; the
// original's copy of it scanned for placements by brute force, which gives
// the same placements in the same order); the original's dancing links
// search of it was never called and is not ported. A split branch is a
// Branch of this assembler, not a copy of the assembler: the stop is the
// run's, the callback and the rotation filter are this assembler's, and the
// progress peers and parked branches are kept here as they were in the
// original's root.
#include "bt/solve/assembler_bt2.h"

#include "bt/base/int_cast.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"
#include "bt/solve/work_stealing.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <string>

namespace bt {

namespace {

/// strtol from `s`, cast to unsigned int; returns the characters consumed
/// (the original's getInt).
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

unsigned int bt2Threads(unsigned int requested) noexcept {
  if (requested > 0) {
    return std::min(requested, kMaxThreads);
  }
  const unsigned int hw = hardwareThreads();
  if (hw <= 2) {
    return 1;
  }
  return std::min(hw - 2, 16U);
}

struct AssemblerBt2::Driver {
  AssemblerBt2& a;

  void searchSlice(Branch& b, SearchRun& run, unsigned int budget) {
    a.searchSlice(b, run, budget);
  }
  [[nodiscard]] bool branchFinished(const Branch& b) const { return a.branchFinished(b); }
  [[nodiscard]] std::unique_ptr<Branch> splitBranch(Branch& b) { return a.splitBranch(b); }
  void retireBranch(Branch& b) { a.retireBranch(b); }
  [[nodiscard]] std::vector<std::unique_ptr<Branch>> takeParked() { return a.takeParked(); }
  void park(std::unique_ptr<Branch> b) { a.park(std::move(b)); }
};

AssemblerBt2::AssemblerBt2(const Puzzle& puzzle, const Problem& problem)
    : puzzle_(puzzle), problem_(problem) {}

AssemblerBt2::~AssemblerBt2() = default;

bool AssemblerBt2::canHandle(const Problem& problem) { return everyPartOnePiece(problem); }

AssemblerError AssemblerBt2::createMatrix(const MatrixOptions& options) {
  BT_ASSERT(problem_.resultValid());

  filter_.complete = options.completeRotations;
  strictColors_ = options.strictColors;

  if (!canHandle(problem_)) {
    return AssemblerError::PuzzleUnhandable;
  }

  errorsState_ =
      matrix_.build(puzzle_, problem_, strictColors_, filter_, reducePiece_, errorsParam_);
  if (errorsState_ != AssemblerError::None) {
    return errorsState_;
  }

  iterations_.store(0, std::memory_order_relaxed);

  filter_.apply(options.keepMirrors, options.keepRotations, options.completeRotations);

  return errorsState_;
}

void AssemblerBt2::applySolutionFilterFlags(bool keepMirrors, bool keepRotations,
                                            bool complete) {
  filter_.apply(keepMirrors, keepRotations, complete);
}

ReduceStats AssemblerBt2::reduce(const StopToken& stop) {
  return matrix_.reduce(stop, reducePiece_);
}

// The dancing cells from what is left of the matrix: the columns of the
// header list are the primary items, the variable voxels the secondary
// ones, every row still in its piece's column an option.
void AssemblerBt2::buildCells() {
  if (cellsBuilt_) {
    return;
  }

  const DlxNodes& n = matrix_.nodes;
  const unsigned int varivoxelStart = matrix_.varivoxelStart;
  const unsigned int varivoxelEnd = matrix_.varivoxelEnd;

  if (varivoxelEnd == 0 || n.left.empty()) {
    cellsBuilt_ = true;
    return;
  }

  std::vector<int> colToItem(std::size_t{varivoxelEnd} + 1, -1);
  unsigned int nPrimary = 0;
  for (unsigned int c = n.right[0]; c != 0; c = n.right[c]) {
    if (c <= varivoxelEnd) {
      colToItem[c] = asInt(nPrimary++);
    }
  }

  unsigned int nSecondary = 0;
  if (varivoxelStart < varivoxelEnd) {
    for (unsigned int c = n.right[varivoxelEnd]; c != varivoxelEnd; c = n.right[c]) {
      if (c <= varivoxelEnd) {
        colToItem[c] = asInt(nPrimary + nSecondary++);
      }
    }
  }

  root_.cells.prepare(nPrimary, nSecondary);

  for (const DlxPlacement& pp : matrix_.placements) {
    const unsigned int row = pp.row;
    const unsigned int col = n.colCount[row];
    bool alive = false;
    for (unsigned int r = n.down(col); r != col; r = n.down(r)) {
      if (r == row) {
        alive = true;
        break;
      }
    }
    if (!alive) {
      continue;
    }

    std::vector<unsigned int> items;
    unsigned int node = row;
    do {
      const unsigned int cc = n.colCount[node];
      if (cc <= varivoxelEnd && colToItem[cc] >= 0) {
        items.push_back(static_cast<unsigned int>(colToItem[cc]));
      }
      node = n.right[node];
    } while (node != row);

    if (!items.empty()) {
      root_.cells.addOption(row, items);
    }
  }

  root_.cells.finishSetup();
  cellsBuilt_ = true;
}

// A cover found by any branch: the assembly of its rows, dropped when a
// rotation of it is smaller, else handed over. The branch has gone past it
// and will not find it again, so a pause does not drop it.
void AssemblerBt2::solutionFromRowNodes(SearchRun& run, const std::vector<unsigned int>& rowIds) {
  if (!run.hasCallback()) {
    return;
  }

  Assembly assembly = matrix_.assemblyFromRows(puzzle_.getGridType(), rowIds.data(),
                                               static_cast<unsigned int>(rowIds.size()));

  if (filter_.rejects(assembly, puzzle_, problem_, strictColors_)) {
    return;
  }

  run.deliverFound(std::move(assembly));
}

// ---- the branches ----------------------------------------------------------------

void AssemblerBt2::searchSlice(Branch& b, SearchRun& run, unsigned int budget) {
  if (errorsState_ != AssemblerError::None) {
    return;
  }

  buildCells();
  b.cells.solve(budget, run.token(),
                [this, &run](const std::vector<unsigned int>& rowIds) {
                  solutionFromRowNodes(run, rowIds);
                });
  publishProgress(b);
}

void AssemblerBt2::publishProgress(Branch& b) {
  if (!cellsBuilt_) {
    return;
  }

  b.liveIterations.store(b.cells.iterations(), std::memory_order_relaxed);
  double c = b.shareBase + b.shareScale * (static_cast<double>(b.cells.progress()) - b.shareOffset);
  if (c < 0) {
    c = 0;
  }
  b.contribution.store(static_cast<float>(c), std::memory_order_relaxed);
  b.searchDone.store(b.cells.finished(), std::memory_order_relaxed);
  if (&b == &root_) {
    searchPublished_.store(true, std::memory_order_release);
  }
}

std::unique_ptr<AssemblerBt2::Branch> AssemblerBt2::splitBranch(Branch& b) {
  if (errorsState_ != AssemblerError::None) {
    return nullptr;
  }

  buildCells();

  const auto before = static_cast<double>(b.cells.progress());
  std::unique_ptr<DancingCells> cells = b.cells.split();
  if (!cells) {
    return nullptr;
  }

  auto c = std::make_unique<Branch>();
  c->cells = std::move(*cells);

  // Divide what this search had left between the two. Each one's own
  // estimate runs on from where it stands now, so it is scaled to end on its
  // part exactly.
  const double done = b.shareBase + b.shareScale * (before - b.shareOffset);
  const double rest = b.shareScale * (1.0 - before);
  const double given = rest * static_cast<double>(b.cells.lastSplitShare());

  const auto childAt = static_cast<double>(c->cells.progress());
  c->shareBase = 0;
  c->shareOffset = childAt;
  c->shareScale = (childAt < 1.0) ? given / (1.0 - childAt) : 0.0;

  const auto after = static_cast<double>(b.cells.progress());
  b.shareBase = done;
  b.shareOffset = after;
  b.shareScale = (after < 1.0) ? (rest - given) / (1.0 - after) : 0.0;

  publishProgress(b);
  publishProgress(*c);

  const std::lock_guard<std::mutex> lock(peerMutex_);
  peers_.push_back(c.get());
  return c;
}

bool AssemblerBt2::branchFinished(const Branch& b) const {
  // nothing to search when preparing failed
  if (errorsState_ != AssemblerError::None) {
    return true;
  }
  if (!cellsBuilt_) {
    return false;
  }
  return b.cells.finished();
}

void AssemblerBt2::retireBranch(Branch& b) {
  const std::lock_guard<std::mutex> lock(peerMutex_);
  const auto it = std::find(peers_.begin(), peers_.end(), &b);
  if (it == peers_.end()) {
    return;
  }

  // what it did stays in the totals
  retiredShare_ += static_cast<double>(b.contribution.load(std::memory_order_relaxed));
  iterations_.fetch_add(b.liveIterations.load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
  peers_.erase(it);
}

std::vector<std::unique_ptr<AssemblerBt2::Branch>> AssemblerBt2::takeParked() {
  const std::lock_guard<std::mutex> lock(peerMutex_);
  std::vector<std::unique_ptr<Branch>> res;
  res.swap(parked_);
  return res;
}

void AssemblerBt2::park(std::unique_ptr<Branch> b) {
  if (!b) {
    return;
  }
  const std::lock_guard<std::mutex> lock(peerMutex_);
  parked_.push_back(std::move(b));
}

// ---- runs, progress and positions --------------------------------------------------

RunEnd AssemblerBt2::assemble(const AssemblyCallback& callback, const SearchOptions& options,
                              const StopToken& stop) {
  BT_ASSERT_MSG(errorsState_ == AssemblerError::None, "assemble() after a failed createMatrix()");

  // a position saved when the search was through: nothing to search
  if (restoredThrough_.load(std::memory_order_relaxed)) {
    return RunEnd::Finished;
  }

  SearchRun run(stop, callback);
  const unsigned int workers = effectiveThreads(options.threads, options.pool);

  // made once, from the matrix as reduce() and restorePosition() left it,
  // before any thread reads it
  buildCells();

  bool parkedEmpty = false;
  {
    const std::lock_guard<std::mutex> lock(peerMutex_);
    parkedEmpty = parked_.empty();
  }

  if (workers == 1 && parkedEmpty) {
    // in slices, so that the progress others read is published as it goes
    do {
      searchSlice(root_, run, 65536);
    } while (!root_.cells.finished() && !run.stopRequested());
  } else {
    // the branches read the shapes (smallerRotationExists): their lazy
    // caches must be filled before any thread exists
    warmShapeCaches(puzzle_, problem_);
    Driver driver{*this};
    runWorkStealing<Branch>(driver, root_, workers, options.pool, run);
  }

  const std::lock_guard<std::mutex> lock(peerMutex_);
  return root_.cells.finished() && parked_.empty() ? RunEnd::Finished : RunEnd::Stopped;
}

float AssemblerBt2::finished() const {
  // before the search has run: the position a saved file gave, the start
  // or the end (see restorePosition)
  if (!searchPublished_.load(std::memory_order_acquire)) {
    return restoredThrough_.load(std::memory_order_relaxed) ? 1 : 0;
  }

  const std::lock_guard<std::mutex> lock(peerMutex_);

  if (peers_.empty() && parked_.empty() && root_.searchDone.load(std::memory_order_relaxed)) {
    return 1;
  }

  double sum = retiredShare_ + static_cast<double>(root_.contribution.load(std::memory_order_relaxed));
  for (const Branch* p : peers_) {
    sum += static_cast<double>(p->contribution.load(std::memory_order_relaxed));
  }

  // an estimate: only the search itself says when it is over
  sum = std::min(sum, 0.9999);
  return static_cast<float>(sum);
}

std::uint64_t AssemblerBt2::iterations() const {
  const std::lock_guard<std::mutex> lock(peerMutex_);
  std::uint64_t n = iterations_.load(std::memory_order_relaxed) +
                    root_.liveIterations.load(std::memory_order_relaxed);
  for (const Branch* p : peers_) {
    n += p->liveIterations.load(std::memory_order_relaxed);
  }
  return n;
}

AssemblerError AssemblerBt2::restorePosition(std::string_view text, std::string_view version) {
  if (version != kVersion) {
    return AssemblerError::CannotRestoreVersion;
  }

  // The original's reading of the dancing links position, with its syntax
  // checks.
  const std::string str(text);  // NUL-terminated for the number readers
  const char* string = str.c_str();
  const auto len = static_cast<unsigned int>(str.size());
  unsigned int spos = 0;

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

  std::vector<unsigned int> rows;
  if (pos <= matrix_.pieces) {
    rows.assign(std::size_t{pos} + 1, 0);
    for (unsigned int i = 0; i <= pos; i++) {
      while ((spos < len) && (string[spos] != '(')) {
        spos++;
      }
      spos++;
      if (spos >= len) {
        return AssemblerError::CannotRestoreSyntax;
      }

      unsigned int col = 0;
      spos += getInt(string + spos, &rows[i]);
      if (spos >= len) {
        return AssemblerError::CannotRestoreSyntax;
      }
      spos += getInt(string + spos, &col);
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

    // integrity: the rows placed must be nodes
    for (unsigned int i = 0; i < pos; i++) {
      if (rows[i] > matrix_.nodes.left.size()) {
        return AssemblerError::CannotRestoreSyntax;
      }
    }
  }

  // A search that is through: the format's position after the last piece,
  // with no rows (see savePosition).
  if (pos == matrix_.pieces + 1) {
    restoredThrough_.store(true, std::memory_order_relaxed);
    iterations_.store(it, std::memory_order_relaxed);
    return AssemblerError::None;
  }

  // Otherwise only a search that has not started can be taken up: the
  // dancing cells search never moves the position it saves, so anything
  // else is a search that ran, and where it stood is not known.
  if (pos != 0 || it != 0 || rows.empty() || rows[0] != 0) {
    return AssemblerError::CannotRestoreInterrupted;
  }
  return AssemblerError::None;
}

SavedPosition AssemblerBt2::savePosition() const {
  // The original's dancing links position, at its start: pos 0, the
  // iterations of this search and of the branches that are through (not
  // those of the branches still going), and the first row entry. A search
  // that is through stands after the last piece, as assembler 0's does
  // (the original wrote the start for it too).
  const std::uint64_t it = iterations_.load(std::memory_order_relaxed) +
                           root_.liveIterations.load(std::memory_order_relaxed);
  bool through = restoredThrough_.load(std::memory_order_relaxed);
  if (!through && cellsBuilt_) {
    const std::lock_guard<std::mutex> lock(peerMutex_);
    through = root_.cells.finished() && parked_.empty();
  }
  if (through) {
    return SavedPosition{kVersion,
                         std::to_string(matrix_.pieces + 1) + " " + std::to_string(it) + " "};
  }
  return SavedPosition{kVersion, "0 " + std::to_string(it) + " (0 0)"};
}

unsigned int AssemblerBt2::piecePlacement(unsigned int node, int delta, unsigned int piece,
                                          unsigned char* tran, int* x, int* y, int* z) const {
  return matrix_.piecePlacement(node, delta, piece, tran, x, y, z);
}

unsigned int AssemblerBt2::piecePlacementCount(unsigned int piece) const {
  return matrix_.piecePlacementCount(piece);
}

void AssemblerBt2::debugStep(unsigned long steps) {
  // a budget of 0 is "no limit" to the search; here it is no step
  if (steps == 0) {
    return;
  }
  const AssemblyCallback none;
  SearchRun run(StopToken{}, none);
  searchSlice(root_, run,
              static_cast<unsigned int>(
                  std::min<unsigned long>(steps, std::numeric_limits<unsigned int>::max())));
}

Assembly AssemblerBt2::currentAssembly() const {
  // the last assembly the search found (the original's getAssembly)
  if (cellsBuilt_) {
    const std::vector<unsigned int>& rowIds = root_.cells.currentRowIds();
    return matrix_.assemblyFromRows(puzzle_.getGridType(), rowIds.data(),
                                    static_cast<unsigned int>(rowIds.size()));
  }
  return matrix_.emptyAssembly(puzzle_.getGridType());
}

}  // namespace bt
