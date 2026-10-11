// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/problem.cpp.

#include "bt/model/problem.h"

#include "bt/model/puzzle.h"

#include <algorithm>
#include <utility>

namespace bt {

namespace {

// The stacks of a problem whose puzzle removed shape `removed`
// (stacking.cpp's retarget).
void retargetStacks(stacking::StackMap& map, unsigned int removed) {
  for (std::vector<stacking::DiskRef>& rod : map.rods) {
    std::erase_if(rod, [removed](const stacking::DiskRef& d) { return d.shapeId == removed; });
    for (stacking::DiskRef& d : rod) {
      if (d.shapeId > removed) {
        d.shapeId--;
      }
    }
  }
}

// The stacks of a problem whose puzzle swapped two shapes (swapIds).
void swapStackIds(stacking::StackMap& map, unsigned int a, unsigned int b) {
  for (std::vector<stacking::DiskRef>& rod : map.rods) {
    for (stacking::DiskRef& d : rod) {
      if (d.shapeId == a) {
        d.shapeId = b;
      } else if (d.shapeId == b) {
        d.shapeId = a;
      }
    }
  }
}

}  // namespace

Problem Problem::copyDefinition() const {
  // solutions, counts and the saved position are not copied, the problem is
  // unsolved; the name is left empty, the user gives a new one anyway
  Problem p;
  p.parts_ = parts_;
  p.result_ = result_;
  p.goal_ = goal_;
  p.solverOptions_ = solverOptions_;
  p.colorConstraints_ = colorConstraints_;
  p.maxHoles_ = maxHoles_;
  p.rodSetId_ = rodSetId_;
  p.startMap_ = startMap_;
  p.goalMap_ = goalMap_;
  p.preserved_ = preserved_;
  return p;
}

// ---- result and goal ------------------------------------------------------------

void Problem::setResultId(const Puzzle& puzzle, unsigned int shape) {
  BT_ASSERT(shape < puzzle.getNumberOfShapes());

  if (shape != result_) {
    removeAllSolutions();
    result_ = shape;
  }
}

void Problem::clearResult() {
  removeAllSolutions();
  result_ = kNoShape;
}

const Voxel& Problem::resultShape(const Puzzle& puzzle) const {
  return puzzle.getShape(getResultId());
}

void Problem::setGoalId(const Puzzle& puzzle, unsigned int shape) {
  BT_ASSERT(shape < puzzle.getNumberOfShapes());

  if (shape != goal_) {
    removeAllSolutions();
    goal_ = shape;
  }
}

void Problem::clearGoal() {
  removeAllSolutions();
  goal_ = kNoShape;
}

// ---- parts ----------------------------------------------------------------------

void Problem::removeShape(unsigned int idx) {
  if (result_ == idx) {
    editProblem();
    result_ = kNoShape;
  }

  if (goal_ == idx) {
    editProblem();
    goal_ = kNoShape;
  }

  // remove the shape from the parts
  setShapeMaximumUnchecked(idx, 0);

  // shapes above the removed one move down
  for (Part& part : parts_) {
    if (part.shapeId > idx) {
      part.shapeId--;
    }
  }

  if (result_ != kNoShape && result_ > idx) {
    result_--;
  }
  if (goal_ != kNoShape && goal_ > idx) {
    goal_--;
  }

  // the stacks drop the shape's discs and renumber those above it
  // (stacking::noteShapeRemoved)
  retargetStacks(startMap_, idx);
  retargetStacks(goalMap_, idx);
}

void Problem::exchangeShapes(unsigned int shapeId1, unsigned int shapeId2) {
  if (result_ == shapeId1) {
    result_ = shapeId2;
  } else if (result_ == shapeId2) {
    result_ = shapeId1;
  }

  if (goal_ == shapeId1) {
    goal_ = shapeId2;
  } else if (goal_ == shapeId2) {
    goal_ = shapeId1;
  }

  for (Part& part : parts_) {
    if (part.shapeId == shapeId1) {
      part.shapeId = static_cast<unsigned short>(shapeId2);
    } else if (part.shapeId == shapeId2) {
      part.shapeId = static_cast<unsigned short>(shapeId1);
    }
  }

  // stacking::noteShapeSwap
  swapStackIds(startMap_, shapeId1, shapeId2);
  swapStackIds(goalMap_, shapeId1, shapeId2);
}

void Problem::dropShapesFrom(unsigned int numShapes) {
  if (result_ != kNoShape && result_ >= numShapes) {
    clearResult();
  }
  if (goal_ != kNoShape && goal_ >= numShapes) {
    clearGoal();
  }
  for (std::size_t part = parts_.size(); part-- > 0;) {
    if (parts_[part].shapeId >= numShapes) {
      setShapeMaximumUnchecked(parts_[part].shapeId, 0);
    }
  }
  // the original never copied a problem into another puzzle: discs of
  // shapes it does not have go off the rods
  for (stacking::StackMap* map : {&startMap_, &goalMap_}) {
    for (std::vector<stacking::DiskRef>& rod : map->rods) {
      std::erase_if(rod, [numShapes](const stacking::DiskRef& d) { return d.shapeId >= numShapes; });
    }
  }
}

void Problem::eraseSolution(std::size_t s) {
  solutions_.erase(solutions_.begin() + static_cast<std::ptrdiff_t>(s));
  sortedBy_ = -1;
  touchSolutions();
}

void Problem::setShapeMinimum(const Puzzle& puzzle, unsigned int shape, unsigned int count) {
  BT_ASSERT(shape < puzzle.getNumberOfShapes());
  setShapeMinimumUnchecked(shape, count);
}

void Problem::setShapeMinimumUnchecked(unsigned int shape, unsigned int count) {
  editProblem();

  unsigned int pieceIdx = 0;

  for (Part& part : parts_) {
    if (part.shapeId == shape) {
      part.min = static_cast<unsigned short>(count);
      if (part.min > part.max) {
        setShapeMaximumUnchecked(shape, count);
      }

      if (count > 0) {
        // drop the solutions that do not have the minimum of these pieces
        std::size_t s = 0;
        while (s < solutions_.size()) {
          if (!solutions_[s].getAssembly().isPlaced(pieceIdx + count - 1)) {
            eraseSolution(s);
          } else {
            s++;
          }
        }
      }

      return;
    }
    pieceIdx += part.max;
  }

  // the shape is not a part yet: add it
  if (count != 0) {
    const auto c = static_cast<unsigned short>(count);
    parts_.push_back(Part{static_cast<unsigned short>(shape), c, c, {}});

    // the new pieces are not placed in the solutions; they go after the
    // others (the original inserted them past the end, see the stage-2
    // handoff note)
    for (Solution& sol : solutions_) {
      sol.addNonPlacedPieces(pieceIdx, count);
    }
  }
}

void Problem::setShapeMaximum(const Puzzle& puzzle, unsigned int shape, unsigned int count) {
  BT_ASSERT(shape < puzzle.getNumberOfShapes());
  setShapeMaximumUnchecked(shape, count);
}

void Problem::setShapeMaximumUnchecked(unsigned int shape, unsigned int count) {
  unsigned int pieceIdx = 0;

  for (std::size_t id = 0; id < parts_.size(); id++) {
    Part& part = parts_[id];
    if (part.shapeId == shape) {
      // remove the part
      if (count == 0) {
        editProblem();

        std::size_t s = 0;
        while (s < solutions_.size()) {
          if (solutions_[s].getAssembly().isPlaced(pieceIdx)) {
            eraseSolution(s);
          } else {
            solutions_[s].removePieces(pieceIdx, part.max);
            s++;
          }
        }

        parts_.erase(parts_.begin() + static_cast<std::ptrdiff_t>(id));
        trimStacks();
        return;
      }

      if (count > part.max) {
        // more pieces: add unplaced ones
        editProblem();

        for (Solution& sol : solutions_) {
          sol.addNonPlacedPieces(pieceIdx + part.max, count - part.max);
        }
      } else if (count < part.max) {
        // fewer pieces: solutions that use more than the new maximum go, the
        // others lose unplaced pieces
        editProblem();

        std::size_t s = 0;
        while (s < solutions_.size()) {
          if (solutions_[s].getAssembly().isPlaced(pieceIdx + part.max - 1)) {
            eraseSolution(s);
          } else {
            solutions_[s].removePieces(pieceIdx + count, part.max - count);
            s++;
          }
        }
      }

      part.max = static_cast<unsigned short>(count);
      if (part.max < part.min) {
        setShapeMinimumUnchecked(shape, count);
      }

      trimStacks();
      return;
    }
    pieceIdx += part.max;
  }

  if (count != 0) {
    parts_.push_back(Part{static_cast<unsigned short>(shape), 0,
                          static_cast<unsigned short>(count), {}});

    for (Solution& sol : solutions_) {
      sol.addNonPlacedPieces(pieceIdx, count);
    }
  }
  trimStacks();
}

// ---- stacking ------------------------------------------------------------------

bool Problem::rodSetValid(const Puzzle& puzzle) const { return rodSetId_ < puzzle.rodSetCount(); }

void Problem::trimStacks() {
  // A disc whose copy is beyond its shape's count is off the rods; so is
  // one of a shape that is no part (the original also dropped discs of
  // shapes past the puzzle's, which have no part either).
  for (stacking::StackMap* map : {&startMap_, &goalMap_}) {
    for (std::vector<stacking::DiskRef>& rod : map->rods) {
      std::erase_if(rod, [this](const stacking::DiskRef& d) {
        return d.instance >= getShapeMaximum(d.shapeId);
      });
    }
  }
}

unsigned int Problem::getShapeMinimum(unsigned int shape) const {
  for (const Part& part : parts_) {
    if (part.shapeId == shape) {
      return part.min;
    }
  }
  return 0;
}

unsigned int Problem::getShapeMaximum(unsigned int shape) const {
  for (const Part& part : parts_) {
    if (part.shapeId == shape) {
      return part.max;
    }
  }
  return 0;
}

bool Problem::usesShape(unsigned int shape) const {
  if (result_ == shape || goal_ == shape) {
    return true;
  }
  return std::any_of(parts_.begin(), parts_.end(),
                     [shape](const Part& p) { return p.shapeId == shape; });
}

unsigned int Problem::getPartIdForShape(unsigned int shapeId) const {
  for (std::size_t i = 0; i < parts_.size(); i++) {
    if (parts_[i].shapeId == shapeId) {
      return static_cast<unsigned int>(i);
    }
  }
  BT_ASSERT_MSG(false, "the shape is not a part of the problem");
  return 0;
}

const Voxel& Problem::partShape(const Puzzle& puzzle, unsigned int partId) const {
  BT_ASSERT(partId < parts_.size());
  return puzzle.getShape(parts_[partId].shapeId);
}

void Problem::exchangeParts(unsigned int partId1, unsigned int partId2) {
  BT_ASSERT(partId1 < parts_.size());
  BT_ASSERT(partId2 < parts_.size());

  if (partId1 == partId2) {
    return;
  }

  if (partId1 > partId2) {
    std::swap(partId1, partId2);
  }

  unsigned int p1Start = 0;
  unsigned int p2Start = 0;

  for (unsigned int i = 0; i < partId1; i++) {
    p1Start += parts_[i].max;
  }
  for (unsigned int i = 0; i < partId2; i++) {
    p2Start += parts_[i].max;
  }

  const unsigned int p1Count = parts_[partId1].max;
  const unsigned int p2Count = parts_[partId2].max;

  // only neighbouring parts can be exchanged
  BT_ASSERT(p1Start + p1Count == p2Start);

  std::swap(parts_[partId1], parts_[partId2]);

  /* the target position of each involved piece; while it is not 0, 1, 2, ...
   * pieces have to be exchanged */
  std::vector<unsigned int> pos(p1Count + p2Count);

  for (unsigned int i = 0; i < p1Count; i++) {
    pos[i] = p2Count + i;
  }
  for (unsigned int i = 0; i < p2Count; i++) {
    pos[p1Count + i] = i;
  }

  for (unsigned int i = 0; i + 1 < p1Count + p2Count; i++) {
    if (pos[i] != i) {
      // find i
      unsigned int j = i + 1;
      while (j < p1Count + p2Count && pos[j] != i) {
        j++;
      }

      BT_ASSERT(j < p1Count + p2Count);

      for (Solution& sol : solutions_) {
        sol.exchangeShape(p1Start + i, p1Start + j);
      }

      // pos[i] = i would follow, but that field is not looked at again
      pos[j] = pos[i];
    }
  }
  touchSolutions();
}

unsigned int Problem::getNumberOfPieces() const {
  unsigned int result = 0;
  for (const Part& part : parts_) {
    result += part.max;
  }
  return result;
}

unsigned int Problem::getPartIdToPieceId(unsigned int pieceId) const {
  unsigned int part = 0;
  while (part < parts_.size() && pieceId >= parts_[part].max) {
    pieceId -= parts_[part].max;
    part++;
  }
  BT_ASSERT(part < parts_.size());
  return part;
}

unsigned int Problem::getPartIndexToPieceId(unsigned int pieceId) const {
  unsigned int part = 0;
  while (part < parts_.size() && pieceId >= parts_[part].max) {
    pieceId -= parts_[part].max;
    part++;
  }
  BT_ASSERT(part < parts_.size());
  return pieceId;
}

// ---- colours --------------------------------------------------------------------

void Problem::allowPlacement(const Puzzle& puzzle, unsigned int pc, unsigned int res) {
  BT_ASSERT(pc <= puzzle.colorNumber());
  BT_ASSERT(res <= puzzle.colorNumber());

  if (pc == 0 || res == 0) {
    return;
  }

  colorConstraints_.insert((pc - 1) << 16 | (res - 1));
}

void Problem::disallowPlacement(const Puzzle& puzzle, unsigned int pc, unsigned int res) {
  BT_ASSERT(pc <= puzzle.colorNumber());
  BT_ASSERT(res <= puzzle.colorNumber());

  if (pc == 0 || res == 0) {
    return;
  }

  colorConstraints_.erase((pc - 1) << 16 | (res - 1));
}

bool Problem::placementAllowed(const Puzzle& puzzle, unsigned int pc, unsigned int res,
                               bool strict) const {
  BT_ASSERT(pc <= puzzle.colorNumber());
  BT_ASSERT(res <= puzzle.colorNumber());

  if (puzzle.colorNumber() == 0) {
    return true;
  }

  // strict: same colour only; neutral only matches neutral
  if (strict) {
    return pc == res;
  }

  return pc == 0 || res == 0 || colorConstraints_.contains((pc - 1) << 16 | (res - 1));
}

// ---- groups ---------------------------------------------------------------------

void Problem::setPartGroup(unsigned int partId, unsigned short groupId, unsigned short count) {
  BT_ASSERT(partId < parts_.size());
  std::vector<PartGroup>& groups = parts_[partId].groups;

  for (std::size_t i = 0; i < groups.size(); i++) {
    if (groups[i].group == groupId) {
      // a new count, or count 0 removes the entry
      if (count != 0) {
        groups[i].count = count;
      } else {
        groups.erase(groups.begin() + static_cast<std::ptrdiff_t>(i));
      }
      return;
    }
  }

  // not there yet: add it, but never for group 0
  if (groupId != 0 && count != 0) {
    groups.push_back(PartGroup{groupId, count});
  }
}

unsigned short Problem::getNumberOfPartGroups(unsigned int partId) const {
  BT_ASSERT(partId < parts_.size());
  return static_cast<unsigned short>(parts_[partId].groups.size());
}

unsigned short Problem::getPartGroupId(unsigned int partId, unsigned int groupIdx) const {
  BT_ASSERT(partId < parts_.size());
  BT_ASSERT(groupIdx < parts_[partId].groups.size());
  return parts_[partId].groups[groupIdx].group;
}

unsigned short Problem::getPartGroupCount(unsigned int partId, unsigned int groupIdx) const {
  BT_ASSERT(partId < parts_.size());
  BT_ASSERT(groupIdx < parts_[partId].groups.size());
  return parts_[partId].groups[groupIdx].count;
}

// ---- solving state --------------------------------------------------------------

void Problem::editProblem() {
  // the solutions may lose pieces or go (setShapeMinimum / Maximum,
  // exchangeParts): the list is read as changed
  touchSolutions();
  if (state_ == ProblemState::Solving || state_ == ProblemState::Solved) {
    makeUnknown();
  }
}

void Problem::makeUnknown() {
  state_ = ProblemState::Unknown;
  assemblerState_.clear();
  assemblerVersion_.clear();
  numAssemblies_ = 0;
  numSolutions_ = 0;
  usedMs_ = 0;
  searched_ = 0;
  searchDepth_ = 0;
  triedApart_ = 0;
}

std::vector<PendingAssembly> Problem::takePending() {
  std::vector<PendingAssembly> out;
  out.swap(pending_);
  return out;
}

void Problem::removeAllSolutions() {
  solutions_.clear();
  touchSolutions();
  sortedBy_ = -1;
  solutionsWithRotations_ = false;
  pending_.clear();
  assemblerState_.clear();
  state_ = ProblemState::Unsolved;
  numAssemblies_ = 0;
  numSolutions_ = 0;
  usedMs_ = 0;
  searched_ = 0;
  searchDepth_ = 0;
  triedApart_ = 0;
}

// ---- solutions ------------------------------------------------------------------

void Problem::addSolution(Assembly assm) { addSolution(std::move(assm), numAssemblies_); }

void Problem::addSolution(Assembly assm, std::uint64_t assemblyNumber) {
  BT_ASSERT(state_ == ProblemState::Solving);
  solutions_.emplace_back(std::move(assm), static_cast<unsigned int>(assemblyNumber));
  solutions_.back().setId(nextSolutionId_++);
  touchSolutions();
}

void Problem::addSolution(Assembly assm, Separation disasm, std::uint64_t assemblyNumber,
                          std::uint64_t solutionNumber, unsigned int pos) {
  BT_ASSERT(state_ == ProblemState::Solving);

  Solution sol(std::move(assm), static_cast<unsigned int>(assemblyNumber), std::move(disasm),
               static_cast<unsigned int>(solutionNumber));
  sol.setId(nextSolutionId_++);
  if (pos < solutions_.size()) {
    solutions_.insert(solutions_.begin() + pos, std::move(sol));
    sortedUpTo_ = std::min<std::size_t>(sortedUpTo_, pos);
  } else {
    solutions_.push_back(std::move(sol));
  }
  touchSolutions();
}

void Problem::addSolution(Assembly assm, SeparationInfo disasm, std::uint64_t assemblyNumber,
                          std::uint64_t solutionNumber, unsigned int pos) {
  BT_ASSERT(state_ == ProblemState::Solving);

  Solution sol(std::move(assm), static_cast<unsigned int>(assemblyNumber), std::move(disasm),
               static_cast<unsigned int>(solutionNumber));
  sol.setId(nextSolutionId_++);
  if (pos < solutions_.size()) {
    solutions_.insert(solutions_.begin() + pos, std::move(sol));
    sortedUpTo_ = std::min<std::size_t>(sortedUpTo_, pos);
  } else {
    solutions_.push_back(std::move(sol));
  }
  touchSolutions();
}

void Problem::addSolution(Assembly assm, Separation disasm, unsigned int pos) {
  addSolution(std::move(assm), std::move(disasm), numAssemblies_, numSolutions_, pos);
}

void Problem::addSolution(Assembly assm, SeparationInfo disasm, unsigned int pos) {
  addSolution(std::move(assm), std::move(disasm), numAssemblies_, numSolutions_, pos);
}

void Problem::assignSolutionIds() {
  for (Solution& s : solutions_) {
    if (s.getId() == 0) {
      s.setId(nextSolutionId_++);
    }
  }
  touchSolutions();
}

void Problem::removeSolution(unsigned int sol) {
  BT_ASSERT(sol < solutions_.size());
  solutions_.erase(solutions_.begin() + sol);
  touchSolutions();
  if (sol < sortedUpTo_) {
    sortedUpTo_--;
  }
}

namespace {

bool comp0Assembly(const Solution& s1, const Solution& s2) {
  return s1.getAssemblyNumber() < s2.getAssemblyNumber();
}

bool comp1Level(const Solution& s1, const Solution& s2) {
  return s1.getDisassemblyInfo() != nullptr && s2.getDisassemblyInfo() != nullptr &&
         s1.getDisassemblyInfo()->compare(*s2.getDisassemblyInfo()) < 0;
}

bool comp2Moves(const Solution& s1, const Solution& s2) {
  return s1.getDisassemblyInfo() != nullptr && s2.getDisassemblyInfo() != nullptr &&
         s1.getDisassemblyInfo()->sumMoves() < s2.getDisassemblyInfo()->sumMoves();
}

bool comp3Pieces(const Solution& s1, const Solution& s2) {
  return s1.getAssembly().comparePieces(s2.getAssembly()) > 0;
}

bool compUnsorted(const Solution& s1, const Solution& s2) {
  if (s1.getSolutionNumber() != s2.getSolutionNumber()) {
    return s1.getSolutionNumber() > s2.getSolutionNumber();
  }
  return s1.getAssemblyNumber() > s2.getAssemblyNumber();
}

bool compMovesDesc(const Solution& s1, const Solution& s2) {
  const Disassembly* d1 = s1.getDisassemblyInfo();
  const Disassembly* d2 = s2.getDisassemblyInfo();

  if (d1 == nullptr && d2 == nullptr) {
    return compUnsorted(s1, s2);
  }
  if (d1 == nullptr) {
    return false;
  }
  if (d2 == nullptr) {
    return true;
  }
  if (d1->sumMoves() != d2->sumMoves()) {
    return d1->sumMoves() > d2->sumMoves();
  }
  return compUnsorted(s1, s2);
}

bool compLevelDesc(const Solution& s1, const Solution& s2) {
  const Disassembly* d1 = s1.getDisassemblyInfo();
  const Disassembly* d2 = s2.getDisassemblyInfo();

  if (d1 == nullptr && d2 == nullptr) {
    return compUnsorted(s1, s2);
  }
  if (d1 == nullptr) {
    return false;
  }
  if (d2 == nullptr) {
    return true;
  }
  if (d1->compare(*d2) != 0) {
    return d1->compare(*d2) > 0;
  }
  return compUnsorted(s1, s2);
}

bool compRotationsDesc(const Solution& s1, const Solution& s2) {
  const Disassembly* d1 = s1.getDisassemblyInfo();
  const Disassembly* d2 = s2.getDisassemblyInfo();

  if (d1 == nullptr && d2 == nullptr) {
    return compUnsorted(s1, s2);
  }
  if (d1 == nullptr) {
    return false;
  }
  if (d2 == nullptr) {
    return true;
  }
  if (d1->sumRotations() != d2->sumRotations()) {
    return d1->sumRotations() > d2->sumRotations();
  }
  return compUnsorted(s1, s2);
}

}  // namespace

SolutionLess solutionSortOrder(int by) {
  switch (by) {
    case 0:
      return comp0Assembly;
    case 1:
      return comp1Level;
    case 2:
      return comp2Moves;
    case 3:
      return comp3Pieces;
    default:
      return nullptr;
  }
}

void Problem::sortSolutions(int by) {
  const SolutionLess less = solutionSortOrder(by);
  if (less == nullptr) {
    return;
  }
  std::stable_sort(solutions_.begin(), solutions_.end(), less);
  sortedBy_ = by;
  sortedUpTo_ = solutions_.size();
  touchSolutions();
}

void Problem::keepSolutionsSorted(int by) {
  const SolutionLess less = solutionSortOrder(by);
  if (less == nullptr) {
    return;
  }
  if (by != sortedBy_ || sortedUpTo_ > solutions_.size()) {
    sortSolutions(by);
    return;
  }
  /* each new one goes after the sorted ones it is not less than, as a
   * stable sort of the whole list would put it; a long tail is sorted and
   * merged instead */
  const std::size_t n = solutions_.size();
  const auto sortedEnd = solutions_.begin() + static_cast<std::ptrdiff_t>(sortedUpTo_);
  if (n - sortedUpTo_ > 64) {
    std::stable_sort(sortedEnd, solutions_.end(), less);
    std::inplace_merge(solutions_.begin(), sortedEnd, solutions_.end(), less);
  } else {
    for (std::size_t i = sortedUpTo_; i < n; i++) {
      const auto at = solutions_.begin() + static_cast<std::ptrdiff_t>(i);
      const auto to = std::upper_bound(solutions_.begin(), at, *at, less);
      std::rotate(to, at, at + 1);
    }
  }
  sortedUpTo_ = n;
  touchSolutions();
}

void Problem::sortSolutionsBySolverMethod(int method) {
  sortedBy_ = -1;
  touchSolutions();
  switch (method) {
    case 0:
      std::stable_sort(solutions_.begin(), solutions_.end(), compUnsorted);
      break;
    case 1:
      std::stable_sort(solutions_.begin(), solutions_.end(), compMovesDesc);
      break;
    case 2:
      std::stable_sort(solutions_.begin(), solutions_.end(), compLevelDesc);
      break;
    case 3:
      std::stable_sort(solutions_.begin(), solutions_.end(), compRotationsDesc);
      break;
    default:
      break;
  }
}

}  // namespace bt
