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
/// disassembler_0.{h,cpp} (Classic: every move),
/// disassembler_crowell.{h,cpp} (at most 100 new positions from each
/// position, with Crowell's turn generator) and disassembler_factory.cpp.
/// The three classes are one here: they differed in that number and the
/// generator only. Stopping is the take-apart's StopToken, the threads the
/// solve's ThreadPool, the environment switches are options (decision
/// 0009).

#include "shapelogic/base/stop.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/solve/grouping.h"
#include "shapelogic/solve/movement_analysator.h"
#include "shapelogic/solve/solver_type.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace bt {

class Assembly;
class NodeHash;
class Problem;
class Puzzle;
class SearchSample;
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
  /// SHAPELOGIC_NO_DISASM_PAR of the original).
  bool levelThreads = true;
  /// A level is spread over threads only when what is left of it is
  /// reckoned to take longer than this many microseconds on one; 0 spreads
  /// every level of two positions or more (for tests). The original's
  /// process-wide setLevelThreadCostUs.
  double levelThreadCostUs = 400.0;
  /// The movement analysis's A/B switches.
  MovementAnalysator::Options analysis;
  /// When set, the search puts a picture of the position it is at here
  /// when one is asked for (a live preview). Must outlive the
  /// disassembler.
  SearchSample* sample = nullptr;
  /// How deep the tree it makes may nest (TakeApartResult::tooDeep past
  /// it): the bound of the trees a file may hold, and never more. Tests
  /// lower it: a tree at the bound itself takes a thousand pieces, and
  /// minutes in a debug build.
  unsigned int maxDepth = kMaxSeparationDepth;
};

/// What a take-apart found.
struct TakeApartResult {
  /// The separation tree, when the assembly comes apart.
  std::optional<Separation> separation;
  /// The take-apart was stopped before it could tell; separation is empty
  /// then (stopped part way is not "cannot come apart").
  bool stopped = false;
  /// The pieces came apart, part after part, until the tree would have
  /// nested deeper than DisassemblerOptions::maxDepth: no tree is made (the file
  /// with it would not open again), separation is empty, and whether the
  /// rest comes apart was not looked for. Not "cannot come apart" either:
  /// whoever asked says why (Disassembler::tooDeepMessage).
  bool tooDeep = false;
};

/// The take-apart of one problem's assemblies.
class Disassembler {
 public:
  /// The number of new positions Crowell's take-apart takes from each
  /// position (the Fortran solver's Nkeep).
  static constexpr unsigned int kCrowellMaxSuccessors = 100;

  /// A disassembler for `problem` of `puzzle` (both must outlive it); the
  /// grid must have a take-apart (canDisassemble). Reads the shapes from
  /// several threads: their caches are warmed here. The shapes, their
  /// hotspots included, must not change while it exists: its movement
  /// caches keep what they worked out of them (an edited shape needs a new
  /// disassembler).
  Disassembler(const Puzzle& puzzle, const Problem& problem, DisassemblerOptions options);
  ~Disassembler();

  Disassembler(const Disassembler&) = delete;
  Disassembler& operator=(const Disassembler&) = delete;

  /// True when the puzzle has a take-apart: its grid has one (every grid of
  /// polyhedra: not the spheres, nor the sliding and stacking puzzles) and
  /// it has no tilted tray (the pieces lie flat in a tray whose walls the
  /// cells do not hold: kNoTakeApartOnTray). The one rule for a solve with
  /// disassembly, the analysis, the movement explorer, the CLI and the
  /// Python module.
  [[nodiscard]] static bool canDisassemble(const Puzzle& puzzle);

  /// What is said where a take-apart is asked of a puzzle with a tilted tray.
  static constexpr const char* kNoTakeApartOnTray =
      "A tilted tray's pieces lie flat and lift straight out: there is nothing to take apart.";

  /// What is said of a take-apart that ended as too deep
  /// (TakeApartResult::tooDeep): the one text for the solve's failure, the
  /// analysis's, the CLI's and the Python module's.
  [[nodiscard]] static std::string tooDeepMessage();

  /// Takes `assembly` (one of the problem's) apart. One take-apart at a
  /// time. A stop ends it as soon as it can. However many parts deep the
  /// pieces come apart, the stack it takes is the same: the parts still to
  /// take apart wait on the heap (cleanup item 18.5: a round of recursion
  /// for each part overflowed a thread's stack at 450 pieces that come off
  /// one at a time).
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
  struct Round;

  MovementAnalysator& analysatorFor(unsigned int slot);
  void expandNode(MovementAnalysator& an, DisassemblerNode* node,
                  const std::vector<unsigned int>& pieces, const NodeHash& oldFront,
                  const NodeHash& curFront, const NodeHash& newFront, Expansion& e) const;
  /// The search of one part: the first position that separates `pieces`,
  /// from `start`; none when there is none, or at a stop.
  NodeRef searchLevels(const std::vector<unsigned int>& pieces, const NodeRef& start);
  /// The whole take-apart of `pieces` from `start`: the search of the part,
  /// then of each part a separation leaves, down to single pieces and
  /// groups. Sets tooDeep_ when it gives up for the tree's depth.
  std::unique_ptr<Separation> takeApart(std::vector<unsigned int> pieces, NodeRef start);
  /// Goes on to the part of `round` whose pieces st has as `removed`: true
  /// when it must be searched, from `start` with the pieces `part`; false
  /// when it is through as it is (one piece, or a group).
  bool beginPart(Round& round, bool removed, std::vector<unsigned int>& part,
                 NodeRef& start) const;
  /// The tree node of a round whose two parts are through.
  static std::unique_ptr<Separation> separationOf(Round& round);
  [[nodiscard]] unsigned short subProbGroup(const DisassemblerNode* st,
                                            const std::vector<unsigned int>& pn,
                                            bool removed) const;
  bool subProbGrouping(const std::vector<unsigned int>& pn);
  [[nodiscard]] bool aborted() const { return stop_.stopRequested(); }
  void putSample(const DisassemblerNode& node, const std::vector<unsigned int>& pieces) const;

  const Puzzle& puzzle_;
  const Problem& problem_;
  DisassemblerOptions options_;
  /// 0 or kCrowellMaxSuccessors.
  unsigned int maxSuccessors_ = 0;

  /// For pieces that may stay together.
  Grouping groups_;
  /// The part of each piece (named as the original's, which held the part
  /// too).
  std::vector<unsigned short> piece2shape_;

  /// The analysator of the calling thread (slot 0), and those of the pool's
  /// threads (slots 1 ...), made when first needed; helperMutex_ guards
  /// the list.
  std::unique_ptr<MovementAnalysator> analyse_;
  mutable std::mutex helperMutex_;
  std::vector<std::unique_ptr<MovementAnalysator>> helpers_;

  /// The take-apart's stop.
  StopToken stop_;
  /// The take-apart under way gave up at kMaxSeparationDepth.
  bool tooDeep_ = false;

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
