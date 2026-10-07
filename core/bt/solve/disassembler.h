// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The take-apart: from an assembly, a breadth-first search through the
/// positions the pieces can reach, until one separates the puzzle into two
/// parts; then the same for each part, down to single pieces (or groups
/// that may stay together). The result is the separation tree.
///
/// Ported from src/lib/disassembler.h (the interface and
/// disassemblyProgress_c), disassembler_a.{h,cpp} (the search),
/// disassembler_0.{h,cpp} (Classic and BurrTools 2: every move),
/// disassembler_crowell.{h,cpp} (at most 100 new positions from each
/// position, with Crowell's turn generator) and disassembler_factory.cpp.
/// The three classes are one here: they differed in that number and the
/// generator only. Stopping is the take-apart's StopToken, the threads the
/// solve's ThreadPool, the environment switches are options (decision
/// 0009).

#include "bt/base/stop.h"
#include "bt/model/disassembly.h"
#include "bt/solve/grouping.h"
#include "bt/solve/movement_analysator.h"
#include "bt/solve/solver_type.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace bt {

class Assembly;
class NodeHash;
class Problem;
class Puzzle;
class ThreadPool;

/// What a take-apart under way is doing, for showing progress. The search
/// goes level by level (every position one move further from the start);
/// how many levels there will be is not known before the end, but how far
/// the current one is, and how many of the separations needed to free
/// every piece have been found, are.
struct DisassemblyProgress {
  /// A take-apart is running; nothing else is set when not.
  bool active = false;
  /// Milliseconds since this take-apart began.
  std::uint64_t elapsedMs = 0;
  /// The pieces of the assembly, and of the part searched now.
  unsigned int pieces = 0;
  unsigned int searchPieces = 0;
  /// Separations found so far; every piece is free after pieces - 1.
  unsigned int separations = 0;
  /// How many parts deep the search is: 1 for the whole assembly.
  unsigned int depth = 0;
  /// Moves from the start of the part searched.
  unsigned int level = 0;
  /// Positions of this level looked at, and how many it has.
  std::uint64_t levelDone = 0;
  std::uint64_t levelSize = 0;
  /// Positions found for the next level so far.
  std::uint64_t nextLevelSize = 0;
  /// Positions looked at since this take-apart began.
  std::uint64_t nodes = 0;
  /// Threads that worked on the current level.
  unsigned int threads = 1;
};

/// How a disassembler works.
struct DisassemblerOptions {
  /// Try 90 degree turns too (cube grid only).
  bool rotations = false;
  /// The solver: Crowell gets its own take-apart, the others Classic's.
  SolverType solver = SolverType::Classic;
  /// Threads to spread a level of the search over; null searches on the
  /// calling thread alone. Must outlive the disassembler.
  ThreadPool* pool = nullptr;
  /// Spread levels over the pool's threads at all (off:
  /// BURRTOOLS_NO_DISASM_PAR of the original).
  bool levelThreads = true;
  /// A level is spread over threads only when what is left of it is
  /// reckoned to take longer than this many microseconds on one; 0 spreads
  /// every level of two positions or more (for tests). The original's
  /// process-wide setLevelThreadCostUs.
  double levelThreadCostUs = 400.0;
  /// The movement analysis's A/B switches.
  MovementAnalysator::Options analysis;
};

/// What a take-apart found.
struct TakeApartResult {
  /// The separation tree, when the assembly comes apart.
  std::optional<Separation> separation;
  /// The take-apart was stopped before it could tell; separation is empty
  /// then (stopped part way is not "cannot come apart").
  bool stopped = false;
};

/// The take-apart of one problem's assemblies.
class Disassembler {
 public:
  /// The number of new positions Crowell's take-apart takes from each
  /// position (the Fortran solver's Nkeep).
  static constexpr unsigned int kCrowellMaxSuccessors = 100;

  /// A disassembler for `problem` of `puzzle` (both must outlive it); the
  /// grid must have a take-apart (canDisassemble). Reads the shapes from
  /// several threads: their caches are warmed here.
  Disassembler(const Puzzle& puzzle, const Problem& problem, DisassemblerOptions options);
  ~Disassembler();

  Disassembler(const Disassembler&) = delete;
  Disassembler& operator=(const Disassembler&) = delete;

  /// True when the puzzle's grid has a take-apart (cubes and prisms).
  [[nodiscard]] static bool canDisassemble(const Puzzle& puzzle);

  /// Takes `assembly` (one of the problem's) apart. One take-apart at a
  /// time. A stop ends it as soon as it can.
  TakeApartResult disassemble(const Assembly& assembly, const StopToken& stop = {});

  /// Turns the 90 degree turns on or off; not while a take-apart runs.
  void setCheckRotations(bool enable);

  /// What the take-apart under way is doing; from any thread.
  [[nodiscard]] DisassemblyProgress progress() const;

  /// Microseconds spent finding turns, and slides and removals, over all
  /// take-aparts so far.
  [[nodiscard]] std::uint64_t getRotationSearchUs() const;
  [[nodiscard]] std::uint64_t getLinearSearchUs() const;

 private:
  struct Expansion;

  MovementAnalysator& analysatorFor(unsigned int slot);
  void expandNode(MovementAnalysator& an, DisassemblerNode* node,
                  const std::vector<unsigned int>& pieces, const NodeHash& oldFront,
                  const NodeHash& curFront, const NodeHash& newFront, Expansion& e) const;
  std::unique_ptr<Separation> searchLevels(const std::vector<unsigned int>& pieces,
                                           const NodeRef& start);
  std::unique_ptr<Separation> checkSubproblems(const DisassemblerNode* st,
                                               const std::vector<unsigned int>& pieces);
  std::unique_ptr<Separation> checkSubproblem(unsigned int pieceCount,
                                              const std::vector<unsigned int>& pieces,
                                              const DisassemblerNode* st, bool removed, bool* ok);
  [[nodiscard]] unsigned short subProbGroup(const DisassemblerNode* st,
                                            const std::vector<unsigned int>& pn,
                                            bool removed) const;
  bool subProbGrouping(const std::vector<unsigned int>& pn);
  [[nodiscard]] bool aborted() const { return stop_.stopRequested(); }

  const Puzzle& puzzle_;
  const Problem& problem_;
  DisassemblerOptions options_;
  /// 0 or kCrowellMaxSuccessors.
  unsigned int maxSuccessors_ = 0;

  /// For pieces that may stay together.
  Grouping groups_;
  /// The part (shape) of each piece.
  std::vector<unsigned short> piece2shape_;

  /// The analysator of the calling thread (slot 0), and those of the pool's
  /// threads (slots 1 ...), made when first needed; helperMutex_ guards
  /// the list.
  std::unique_ptr<MovementAnalysator> analyse_;
  mutable std::mutex helperMutex_;
  std::vector<std::unique_ptr<MovementAnalysator>> helpers_;

  /// The take-apart's stop.
  StopToken stop_;

  // progress, see DisassemblyProgress; progStartUs_ is 0 when idle
  std::atomic<std::uint64_t> progStartUs_{0};
  std::atomic<unsigned int> progPieces_{0};
  std::atomic<unsigned int> progSearchPieces_{0};
  std::atomic<unsigned int> progSeparations_{0};
  std::atomic<unsigned int> progDepth_{0};
  std::atomic<unsigned int> progLevel_{0};
  std::atomic<unsigned int> progThreads_{1};
  std::atomic<std::uint64_t> progLevelDone_{0};
  std::atomic<std::uint64_t> progLevelSize_{0};
  std::atomic<std::uint64_t> progNextSize_{0};
  std::atomic<std::uint64_t> progNodes_{0};
};

}  // namespace bt
