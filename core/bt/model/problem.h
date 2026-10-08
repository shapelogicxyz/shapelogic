// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A problem: which shapes of the puzzle are the pieces (and how many of
/// each), which one is the result, the colour rules, the piece groups, and
/// what solving found.
/// Ported from src/lib/problem.{h,cpp} (XML reading and writing lives in
/// bt/io; the solver hooks such as setAssembler come with stages 3 and 5).

#include "bt/base/assert.h"
#include "bt/model/assembly.h"
#include "bt/model/solution.h"
#include "bt/model/xml_element.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace bt {

class Puzzle;
class Voxel;

/// How far solving a problem has got. The numbers are the file's `state`
/// attribute (solveState_e in the original).
enum class ProblemState : std::uint8_t {
  Unsolved = 0,  ///< nothing done yet
  Solving = 1,   ///< started, not finished; a saved search position may exist
  Solved = 2,    ///< finished; the counts are complete
  Unknown = 3,   ///< edited after solving: the counts are gone, solutions may remain
};

/// "No shape": the value of an unset result or goal shape.
inline constexpr unsigned int kNoShape = 0xFFFFFFFF;

/// "No limit": the value of an unset maximum number of holes.
inline constexpr unsigned int kNoMaxHoles = 0xFFFFFFFF;

/// Piece groups of a part: `count` of its pieces belong to group `group`.
struct PartGroup {
  unsigned short group = 0;
  unsigned short count = 0;
  bool operator==(const PartGroup&) const = default;
};

/// A shape used as pieces of a problem, with the number of pieces.
struct Part {
  unsigned short shapeId = 0;
  unsigned short min = 0;
  unsigned short max = 0;
  std::vector<PartGroup> groups;
  bool operator==(const Part&) const = default;
};

/// An assembly a paused solve had found but not finished with (still to be
/// taken apart). The next run does it first.
struct PendingAssembly {
  Assembly assembly;
  bool counted = false;  ///< already counted in the assemblies, with these numbers
  std::uint64_t assemblyNumber = 0;
  std::uint64_t solutionNumber = 0;
};

/// One problem of a puzzle. A value inside its Puzzle; operations that look
/// at shapes or colours take that puzzle as a parameter.
///
/// A problem has parts (shapes with a piece count range); the pieces are
/// numbered part by part, each part taking `max` numbers. Assemblies and
/// solutions list their placements in that order.
class Problem {
 public:
  Problem() = default;

  /// A copy of the definition only: no name, no solutions, unsolved
  /// (problem_c's copy constructor).
  [[nodiscard]] Problem copyDefinition() const;

  [[nodiscard]] const std::string& getName() const { return name_; }
  void setName(const std::string& nm) { name_ = nm; }

  // ---- result and goal shapes ---------------------------------------------

  /// Sets the result shape; a different shape drops all solutions.
  void setResultId(const Puzzle& puzzle, unsigned int shape);
  /// Unsets the result shape and drops all solutions.
  void clearResult();
  [[nodiscard]] bool resultValid() const { return result_ != kNoShape; }
  [[nodiscard]] unsigned int getResultId() const {
    BT_ASSERT(resultValid());
    return result_;
  }
  /// The result shape's voxel space.
  [[nodiscard]] const Voxel& resultShape(const Puzzle& puzzle) const;

  /// The goal map of sliding puzzles (optional; not used by the assembler).
  void setGoalId(const Puzzle& puzzle, unsigned int shape);
  void clearGoal();
  [[nodiscard]] bool goalValid() const { return goal_ != kNoShape; }
  [[nodiscard]] unsigned int getGoalId() const {
    BT_ASSERT(goalValid());
    return goal_;
  }

  // ---- parts and pieces -----------------------------------------------------

  /// The puzzle removed shape `shapeId`: drops its part and its uses, and
  /// renumbers the shapes above it. Called by Puzzle::removeShape.
  void removeShape(unsigned int shapeId);
  /// The puzzle swapped two shapes. Called by Puzzle::exchangeShapes.
  void exchangeShapes(unsigned int shapeId1, unsigned int shapeId2);

  /// Drops the parts, result and goal that name shapes at or above
  /// `numShapes` (a problem copied from another puzzle).
  void dropShapesFrom(unsigned int numShapes);

  /// Sets the minimum number of pieces of a shape (adding the part if needed).
  void setShapeMinimum(const Puzzle& puzzle, unsigned int shapeId, unsigned int count);
  /// Sets the maximum number of pieces of a shape; 0 removes the part.
  void setShapeMaximum(const Puzzle& puzzle, unsigned int shapeId, unsigned int count);
  /// The minimum / maximum for a shape, 0 when it is not a part.
  [[nodiscard]] unsigned int getShapeMinimum(unsigned int shapeId) const;
  [[nodiscard]] unsigned int getShapeMaximum(unsigned int shapeId) const;
  /// True when the shape is a part, the result or the goal.
  [[nodiscard]] bool usesShape(unsigned int shapeId) const;

  [[nodiscard]] unsigned int getNumberOfParts() const {
    return static_cast<unsigned int>(parts_.size());
  }
  [[nodiscard]] const std::vector<Part>& getParts() const { return parts_; }
  [[nodiscard]] unsigned int getPartMinimum(unsigned int partId) const;
  [[nodiscard]] unsigned int getPartMaximum(unsigned int partId) const;
  [[nodiscard]] unsigned int getShapeIdOfPart(unsigned int partId) const;
  /// The part of a shape; the shape must be a part.
  [[nodiscard]] unsigned int getPartIdForShape(unsigned int shapeId) const;
  /// The voxel space of a part.
  [[nodiscard]] const Voxel& partShape(const Puzzle& puzzle, unsigned int partId) const;
  /// Swaps two neighbouring parts, renumbering the pieces of all solutions.
  void exchangeParts(unsigned int partId1, unsigned int partId2);

  /// The number of pieces: the sum of the parts' maxima.
  [[nodiscard]] unsigned int getNumberOfPieces() const;
  /// The part of a piece.
  [[nodiscard]] unsigned int getPartIdToPieceId(unsigned int pieceId) const;
  /// Which piece of its part a piece is (0 for the first).
  [[nodiscard]] unsigned int getPartIndexToPieceId(unsigned int pieceId) const;

  // ---- colour rules ---------------------------------------------------------

  /// Colour 0 is neutral. Unless `strict`, a neutral cell on either side
  /// goes anywhere; otherwise a piece colour may go into a result colour
  /// only when allowed. With `strict` a cell may only go into a result cell
  /// of the same colour, neutral included. Without colours everything fits.
  [[nodiscard]] bool placementAllowed(const Puzzle& puzzle, unsigned int pc, unsigned int res,
                                      bool strict = false) const;
  /// Allows piece colour `pc` in result colour `res` (both 1-based).
  void allowPlacement(const Puzzle& puzzle, unsigned int pc, unsigned int res);
  void disallowPlacement(const Puzzle& puzzle, unsigned int pc, unsigned int res);
  /// The allowed pairs, (pc-1) << 16 | (res-1), as the file stores them.
  [[nodiscard]] const std::set<std::uint32_t>& getColorConstraints() const {
    return colorConstraints_;
  }

  // ---- groups ---------------------------------------------------------------

  /// Puts `count` pieces of a part into group `group`; count 0 removes it.
  /// The disassembler stops taking apart a clump of pieces of one group;
  /// group 0 means a piece of its own.
  void setPartGroup(unsigned int partId, unsigned short group, unsigned short count);
  [[nodiscard]] unsigned short getNumberOfPartGroups(unsigned int partId) const;
  [[nodiscard]] unsigned short getPartGroupId(unsigned int partId, unsigned int groupIdx) const;
  [[nodiscard]] unsigned short getPartGroupCount(unsigned int partId,
                                                 unsigned int groupIdx) const;

  // ---- maximum number of holes ------------------------------------------------

  [[nodiscard]] bool maxHolesDefined() const { return maxHoles_ != kNoMaxHoles; }
  [[nodiscard]] unsigned int getMaxHoles() const {
    BT_ASSERT(maxHolesDefined());
    return maxHoles_;
  }
  void setMaxHoles(unsigned int value) {
    BT_ASSERT(value < kNoMaxHoles);
    maxHoles_ = value;
  }
  void setMaxHolesInvalid() { maxHoles_ = kNoMaxHoles; }

  // ---- solving state and results ----------------------------------------------

  [[nodiscard]] ProblemState getSolveState() const { return state_; }
  [[nodiscard]] bool numAssembliesKnown() const { return state_ != ProblemState::Unsolved; }
  [[nodiscard]] bool numSolutionsKnown() const { return state_ != ProblemState::Unsolved; }
  [[nodiscard]] bool usedTimeKnown() const { return state_ != ProblemState::Unsolved; }
  [[nodiscard]] std::uint64_t getNumAssemblies() const {
    BT_ASSERT(numAssembliesKnown());
    return numAssemblies_;
  }
  [[nodiscard]] std::uint64_t getNumSolutions() const {
    BT_ASSERT(numSolutionsKnown());
    return numSolutions_;
  }
  /// Time spent solving, in whole seconds.
  [[nodiscard]] std::uint64_t getUsedTime() const {
    BT_ASSERT(usedTimeKnown());
    return usedMs_ / 1000;
  }
  /// Time spent solving, in milliseconds.
  [[nodiscard]] std::uint64_t getUsedMs() const {
    BT_ASSERT(usedTimeKnown());
    return usedMs_;
  }

  /// How much the solver went through: search steps (or arrangements), the
  /// depth of a sliding search, and the assemblies tried for taking apart.
  /// 0 when unknown.
  [[nodiscard]] std::uint64_t getSearched() const { return searched_; }
  [[nodiscard]] std::uint64_t getSearchDepth() const { return searchDepth_; }
  [[nodiscard]] std::uint64_t getTriedApart() const { return triedApart_; }
  void setSearchStats(std::uint64_t s, std::uint64_t depth, std::uint64_t apart) {
    searched_ = s;
    searchDepth_ = depth;
    triedApart_ = apart;
  }

  /// Marks the problem as being solved (without a saved position).
  void markSolving() { state_ = ProblemState::Solving; }
  /// Marks solving as finished.
  void finishedSolving() { state_ = ProblemState::Solved; }
  /// Moves to the Unknown state, dropping the counts and the saved position.
  void makeUnknown();

  void incNumAssemblies() {
    BT_ASSERT(state_ == ProblemState::Solving);
    numAssemblies_++;
  }
  void incNumSolutions() {
    BT_ASSERT(state_ == ProblemState::Solving);
    numSolutions_++;
  }
  void addTimeMs(std::uint64_t ms) {
    BT_ASSERT(state_ == ProblemState::Solving);
    usedMs_ += ms;
  }

  /// The search position of a paused solve, as the assembler wrote it, and
  /// the assembler version it belongs to. Empty when there is none.
  [[nodiscard]] const std::string& getAssemblerState() const { return assemblerState_; }
  [[nodiscard]] const std::string& getAssemblerVersion() const { return assemblerVersion_; }
  /// Stores a search position to be saved with the puzzle (written only
  /// while the problem is Solving and both strings are set); empty strings
  /// drop it.
  void setAssemblerState(std::string state, std::string version) {
    assemblerState_ = std::move(state);
    assemblerVersion_ = std::move(version);
  }
  /// Drops the position text once an assembler has taken it over (the
  /// version stays, as in the original).
  void clearAssemblerState() { assemblerState_.clear(); }

  /// Assemblies a paused solve still has to finish.
  [[nodiscard]] const std::vector<PendingAssembly>& getPending() const { return pending_; }
  void addPending(PendingAssembly p) { pending_.push_back(std::move(p)); }
  [[nodiscard]] std::vector<PendingAssembly> takePending();

  /// Drops all solutions and everything solving found, back to Unsolved.
  void removeAllSolutions();

  // ---- saved solutions ----------------------------------------------------------

  [[nodiscard]] unsigned int getNumberOfSavedSolutions() const {
    return static_cast<unsigned int>(solutions_.size());
  }
  [[nodiscard]] const Solution& getSavedSolution(unsigned int sol) const {
    BT_ASSERT(sol < solutions_.size());
    return solutions_[sol];
  }
  [[nodiscard]] Solution& getSavedSolution(unsigned int sol) {
    BT_ASSERT(sol < solutions_.size());
    return solutions_[sol];
  }

  /// Adds an assembly as a solution, numbered as the current assembly count.
  void addSolution(Assembly assm);
  /// Adds an assembly as a solution with an explicit assembly number.
  void addSolution(Assembly assm, std::uint64_t assemblyNumber);
  /// Adds a solution with its disassembly, at `pos` (default: the end).
  void addSolution(Assembly assm, Separation disasm, std::uint64_t assemblyNumber,
                   std::uint64_t solutionNumber, unsigned int pos = 0xFFFFFFFF);
  void addSolution(Assembly assm, SeparationInfo disasm, std::uint64_t assemblyNumber,
                   std::uint64_t solutionNumber, unsigned int pos = 0xFFFFFFFF);
  /// The same, numbered with the current assembly and solution counts.
  void addSolution(Assembly assm, Separation disasm, unsigned int pos = 0xFFFFFFFF);
  void addSolution(Assembly assm, SeparationInfo disasm, unsigned int pos = 0xFFFFFFFF);

  /// Removes one saved solution.
  void removeSolution(unsigned int sol);

  /// Gives the solutions that have none an id (Solution::getId), in list
  /// order: the file reader calls it for the solutions it read.
  void assignSolutionIds();

  /// Sorts the solutions: 0 by assembly number, 1 by level, 2 by moves,
  /// 3 by the pieces used.
  void sortSolutions(int by);

  /// sortSolutions(by), but cheap when the list was sorted that way before
  /// and solutions were only added or removed since.
  void keepSolutionsSorted(int by);

  /// Sorts by the solver's "sort by" choice, highest first: 0 unsorted (by
  /// solution and assembly number), 1 moves, 2 level, 3 rotations.
  void sortSolutionsBySolverMethod(int method);

  /// True when solutions were found with rotation take-apart: they are
  /// written as <solutionsWithRotations>, which older versions skip.
  [[nodiscard]] bool getSolutionsWithRotations() const { return solutionsWithRotations_; }
  void setSolutionsWithRotations(bool v) { solutionsWithRotations_ = v; }

  /// The Solver tab settings of this problem, as name/value pairs. Only
  /// stored here; the front end decides what they mean.
  [[nodiscard]] const std::map<std::string, std::string>& getSolverOptions() const {
    return solverOptions_;
  }
  void setSolverOptions(const std::map<std::string, std::string>& o) { solverOptions_ = o; }

  /// File content of the problem element this version does not model
  /// (e.g. <stacking>).
  [[nodiscard]] const PreservedXml& preserved() const { return preserved_; }
  [[nodiscard]] PreservedXml& preserved() { return preserved_; }

 private:
  friend struct ProblemFileAccess;  // the .xmpuzzle reader (bt/io)

  /// The problem was edited: what solving found no longer holds.
  void editProblem();

  void setShapeMaximumUnchecked(unsigned int shapeId, unsigned int count);
  void setShapeMinimumUnchecked(unsigned int shapeId, unsigned int count);
  void eraseSolution(std::size_t s);

  std::string name_;
  std::vector<Part> parts_;
  unsigned int result_ = kNoShape;
  unsigned int goal_ = kNoShape;

  std::vector<Solution> solutions_;
  /// The id the next solution gets (runtime only, not saved).
  std::uint64_t nextSolutionId_ = 1;
  /// What keepSolutionsSorted knows: the first sortedUpTo_ solutions are in
  /// sortSolutions(sortedBy_) order; -1 when nothing is known.
  int sortedBy_ = -1;
  std::size_t sortedUpTo_ = 0;

  std::vector<PendingAssembly> pending_;
  bool solutionsWithRotations_ = false;
  std::map<std::string, std::string> solverOptions_;
  std::set<std::uint32_t> colorConstraints_;

  ProblemState state_ = ProblemState::Unsolved;
  std::uint64_t numAssemblies_ = 0;
  std::uint64_t numSolutions_ = 0;
  std::string assemblerState_;
  std::string assemblerVersion_;
  std::uint64_t usedMs_ = 0;
  std::uint64_t searched_ = 0;
  std::uint64_t searchDepth_ = 0;
  std::uint64_t triedApart_ = 0;
  unsigned int maxHoles_ = kNoMaxHoles;

  PreservedXml preserved_;
};

}  // namespace bt
