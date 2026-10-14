// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/disassembler_a.cpp, disassembler_0.cpp,
// disassembler_crowell.cpp and disassembler_factory.cpp.
#include "shapelogic/solve/disassembler.h"

#include "shapelogic/base/assert.h"
#include "shapelogic/base/thread_pool.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/solve/assembler.h"
#include "shapelogic/solve/node_hash.h"

#include <algorithm>
#include <chrono>
#include <unordered_set>

namespace bt {

namespace {

std::uint64_t nowUs() {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(
      duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

}  // namespace

// What the search of one position of a level gave, when the level is spread
// over threads.
struct Disassembler::Expansion {
  /// The positions it leads to that neither the old, the current nor (as it
  /// was when the threads set out) the new front has, in the order found.
  std::vector<NodeRef> cands;
  /// Moves taken from find() to get them.
  unsigned int raw = 0;
  /// Searched, not passed over or stopped.
  bool expanded = false;
  /// The last of cands separates the puzzle.
  bool separation = false;
  /// The search was left before find() ran out.
  bool more = false;

  void reset() {
    cands.clear();
    raw = 0;
    expanded = separation = more = false;
  }
};

Disassembler::Disassembler(const Puzzle& puzzle, const Problem& problem,
                           DisassemblerOptions options)
    : puzzle_(puzzle), problem_(problem), options_(options) {
  BT_ASSERT_MSG(canDisassemble(puzzle), "a grid without a take-apart");

  if (options_.solver == SolverType::Crowell) {
    maxSuccessors_ = kCrowellMaxSuccessors;
  }
  if (!options_.levelThreads) {
    options_.pool = nullptr;
  }

  // the level threads read the shapes (through their movement caches)
  warmShapeCaches(puzzle, problem);

  // by part, as subProbGrouping asks (the original registered the shape id)
  for (unsigned int i = 0; i < problem.getNumberOfParts(); i++) {
    for (unsigned int j = 0; j < problem.getNumberOfPartGroups(i); j++) {
      groups_.addPieces(i, problem.getPartGroupId(i, j), problem.getPartGroupCount(i, j));
    }
  }

  piece2shape_.resize(problem.getNumberOfPieces());
  std::size_t p = 0;
  for (unsigned int i = 0; i < problem.getNumberOfParts(); i++) {
    for (unsigned int j = 0; j < problem.getPartMaximum(i); j++) {
      piece2shape_[p++] = static_cast<unsigned short>(i);
    }
  }

  analyse_ = std::make_unique<MovementAnalysator>(puzzle, problem, options_.rotations,
                                                  options_.solver, options_.analysis);
}

Disassembler::~Disassembler() = default;

bool Disassembler::canDisassemble(const Puzzle& puzzle) {
  return (puzzle.getGridType().getCapabilities() & kCapDisassemble) != 0;
}

void Disassembler::setCheckRotations(bool enable) {
  options_.rotations = enable;
  analyse_->setCheckRotations(enable);
  const std::lock_guard<std::mutex> lock(helperMutex_);
  for (const std::unique_ptr<MovementAnalysator>& h : helpers_) {
    if (h) {
      h->setCheckRotations(enable);
    }
  }
}

std::uint64_t Disassembler::getRotationSearchUs() const {
  std::uint64_t us = analyse_->getRotationSearchUs();
  const std::lock_guard<std::mutex> lock(helperMutex_);
  for (const std::unique_ptr<MovementAnalysator>& h : helpers_) {
    if (h) {
      us += h->getRotationSearchUs();
    }
  }
  return us;
}

std::uint64_t Disassembler::getLinearSearchUs() const {
  std::uint64_t us = analyse_->getLinearSearchUs();
  const std::lock_guard<std::mutex> lock(helperMutex_);
  for (const std::unique_ptr<MovementAnalysator>& h : helpers_) {
    if (h) {
      us += h->getLinearSearchUs();
    }
  }
  return us;
}

MovementAnalysator& Disassembler::analysatorFor(unsigned int slot) {
  if (slot == 0) {
    return *analyse_;
  }

  const std::lock_guard<std::mutex> lock(helperMutex_);
  if (helpers_.size() < slot) {
    helpers_.resize(slot);
  }
  std::unique_ptr<MovementAnalysator>& h = helpers_[slot - 1];
  if (!h) {
    h = std::make_unique<MovementAnalysator>(puzzle_, problem_, options_.rotations,
                                             options_.solver, options_.analysis);
  }
  h->setStop(stop_);
  return *h;
}

DisassemblyProgress Disassembler::progress() const {
  DisassemblyProgress p;

  const std::uint64_t start = progStartUs_.load(std::memory_order_relaxed);
  if (start == 0) {
    return p;
  }

  const std::uint64_t now = nowUs();
  p.active = true;
  p.elapsedMs = now > start ? (now - start) / 1000 : 0;
  p.pieces = progPieces_.load(std::memory_order_relaxed);
  p.searchPieces = progSearchPieces_.load(std::memory_order_relaxed);
  p.separations = progSeparations_.load(std::memory_order_relaxed);
  p.depth = progDepth_.load(std::memory_order_relaxed);
  p.level = progLevel_.load(std::memory_order_relaxed);
  p.levelDone = progLevelDone_.load(std::memory_order_relaxed);
  p.levelSize = progLevelSize_.load(std::memory_order_relaxed);
  p.levelDone = std::min(p.levelDone, p.levelSize);
  p.nextLevelSize = progNextSize_.load(std::memory_order_relaxed);
  p.nodes = progNodes_.load(std::memory_order_relaxed);
  p.threads = progThreads_.load(std::memory_order_relaxed);
  return p;
}

void Disassembler::expandNode(MovementAnalysator& an, DisassemblerNode* node,
                              const std::vector<unsigned int>& pieces, const NodeHash& oldFront,
                              const NodeHash& curFront, const NodeHash& newFront,
                              Expansion& e) const {
  // a position can be reached from this one in more than one way
  std::unordered_set<const DisassemblerNode*, NodePositionHash, NodePositionEqual> seen;
  unsigned int added = 0;

  an.initFind(node, pieces);

  while (NodeRef st = an.find()) {
    e.raw++;

    if (oldFront.contains(st.get()) || curFront.contains(st.get()) ||
        newFront.contains(st.get()) || !seen.insert(st.get()).second) {
      continue;
    }

    const bool separation = st->isSeparation();
    e.cands.push_back(std::move(st));

    if (separation) {
      e.separation = true;
      e.more = true;
      break;
    }

    if (maxSuccessors_ != 0 && ++added >= maxSuccessors_) {
      e.more = true;
      break;
    }
  }

  // The analysator keeps hold of positions it handed out. Let go now, while
  // this thread is the only one to touch this position's references.
  an.endFind();

  e.expanded = !aborted();
}

// The search: breadth first through the positions, all those one move from
// the start, then two moves, and so on, until a position separates the
// puzzle into two parts.
//
// Positions are kept only as long as they can be on a shortest way to
// something still to come: in three fronts, each all the positions at one
// distance from the start. A position found from the current front that is
// in the old one is a step back, one in the current front a step sideways,
// one in the new front one there is a way to already. Only what none of
// them has is new. When the current front is through, the old one is let
// go (the counted references free what nothing points to any more), the
// current becomes the old and the new the current.
//
// With a pool the positions of a level are searched on several threads.
// What they find is put together in the order the search on one thread
// would have found it, so the result is the same either way.
std::unique_ptr<Separation> Disassembler::searchLevels(const std::vector<unsigned int>& pieces,
                                                       const NodeRef& start) {
  struct Depth {
    std::atomic<unsigned int>& d;
    explicit Depth(std::atomic<unsigned int>& depth) : d(depth) {
      d.fetch_add(1, std::memory_order_relaxed);
    }
    ~Depth() { d.fetch_sub(1, std::memory_order_relaxed); }
    Depth(const Depth&) = delete;
    Depth& operator=(const Depth&) = delete;
  } depth(progDepth_);

  progSearchPieces_.store(static_cast<unsigned int>(pieces.size()), std::memory_order_relaxed);

  NodeHash closed[3];
  std::size_t oldFront = 0;
  std::size_t curFront = 1;
  std::size_t newFront = 2;

  // the positions of the current front, in the order found, and what is
  // found for the next; the fronts keep them
  std::vector<DisassemblerNode*> level;
  std::vector<DisassemblerNode*> next;

  closed[curFront].insert(start);
  level.push_back(start.get());

  std::vector<Expansion> exp;
  NodeRef found;
  unsigned int levelNo = 0;
  ThreadPool* pool = options_.pool;

  // what one position takes to search, to tell whether a level is worth
  // waking other threads for; 0 until one has been timed
  double usPerNode = 0;

  // a new position for the next level: false when it is known (a longer or
  // equally long way to it), else noted, or kept in `found` when it
  // separates the puzzle
  auto take = [&closed, &newFront, &next, &found](NodeRef st) {
    if (closed[newFront].insert(st) != nullptr) {
      return false;
    }
    if (st->isSeparation()) {
      found = std::move(st);
      return true;
    }
    next.push_back(st.get());
    return true;
  };

  while (!level.empty() && !found) {
    progLevel_.store(levelNo, std::memory_order_relaxed);
    progLevelSize_.store(level.size(), std::memory_order_relaxed);
    progLevelDone_.store(0, std::memory_order_relaxed);
    progNextSize_.store(0, std::memory_order_relaxed);

    std::size_t pos = 0;

    while (pos < level.size() && !found) {
      if (aborted()) {
        return nullptr;
      }

      const std::size_t remaining = level.size() - pos;
      std::size_t batch = 1;
      if (pool != nullptr && remaining >= 2 &&
          usPerNode * static_cast<double>(remaining) >= options_.levelThreadCostUs) {
        batch = std::min(std::size_t{pool->cores()} * 32, remaining);
      }

      const std::uint64_t t0 = nowUs();
      std::size_t done = 0;
      unsigned int used = 1;

      if (batch == 1) {
        // one position, on this thread: what it leads to goes straight into
        // the new front
        DisassemblerNode* node = level[pos];
        unsigned int added = 0;

        analyse_->initFind(node, pieces);

        while (NodeRef st = analyse_->find()) {
          if (closed[oldFront].contains(st.get()) || closed[curFront].contains(st.get())) {
            continue;
          }
          if (!take(std::move(st))) {
            continue;
          }
          if (found) {
            break;
          }
          if (maxSuccessors_ != 0 && ++added >= maxSuccessors_) {
            break;
          }
        }

        done = 1;

      } else {
        // Several positions at once. Each thread takes the next one not
        // taken and notes what it leads to; nothing shared is changed.
        if (exp.size() < batch) {
          exp.resize(batch);
        }
        for (std::size_t i = 0; i < batch; i++) {
          exp[i].reset();
        }

        std::atomic<std::size_t> nextIdx{0};
        // the first position found to lead to a separation: those after it
        // are not wanted
        std::atomic<std::size_t> sepIdx{batch};

        const NodeHash& oF = closed[oldFront];
        const NodeHash& cF = closed[curFront];
        const NodeHash& nF = closed[newFront];
        DisassemblerNode* const* nodes = &level[pos];

        used = pool->run(static_cast<unsigned int>(std::min(batch, std::size_t{pool->cores()})),
                         [&](unsigned int slot) {
                           MovementAnalysator& an = analysatorFor(slot);
                           for (;;) {
                             const std::size_t i = nextIdx.fetch_add(1, std::memory_order_relaxed);
                             if (i >= batch || i > sepIdx.load(std::memory_order_relaxed) ||
                                 aborted()) {
                               break;
                             }
                             expandNode(an, nodes[i], pieces, oF, cF, nF, exp[i]);
                             progLevelDone_.fetch_add(1, std::memory_order_relaxed);
                             if (exp[i].separation) {
                               std::size_t s = sepIdx.load(std::memory_order_relaxed);
                               while (i < s && !sepIdx.compare_exchange_weak(
                                                   s, i, std::memory_order_relaxed)) {
                               }
                             }
                           }
                         });

        // Put together what they found, position by position in the order
        // of the level: what the search on one thread would have put into
        // the new front, in that order.
        for (; done < batch && exp[done].expanded && !found && !aborted(); done++) {
          Expansion& e = exp[done];
          unsigned int added = 0;
          bool full = false;

          for (NodeRef& st : e.cands) {
            if (found || full) {
              break;
            }
            if (!take(std::move(st))) {
              continue;
            }
            if (!found && maxSuccessors_ != 0 && ++added >= maxSuccessors_) {
              full = true;
            }
          }
          e.cands.clear();

          if (!found && !full && e.more) {
            // Its thread left this position early, at the most new
            // positions one may give or at a separation, counting some that
            // another position turned out to have given first. Go on from
            // where it left.
            unsigned int skip = e.raw;

            analyse_->initFind(nodes[done], pieces);

            while (NodeRef st = analyse_->find()) {
              if (skip != 0) {
                skip--;
                continue;
              }
              if (closed[oldFront].contains(st.get()) || closed[curFront].contains(st.get())) {
                continue;
              }
              if (!take(std::move(st))) {
                continue;
              }
              if (found) {
                break;
              }
              if (maxSuccessors_ != 0 && ++added >= maxSuccessors_) {
                break;
              }
            }
          }
        }

        // what was searched for nothing: after a separation, or a stop
        for (std::size_t i = done; i < batch; i++) {
          exp[i].cands.clear();
        }
      }

      analyse_->endFind();

      if (aborted()) {
        return nullptr;
      }

      BT_ASSERT(done > 0);

      pos += done;
      progNodes_.fetch_add(done, std::memory_order_relaxed);
      progLevelDone_.store(pos, std::memory_order_relaxed);
      progNextSize_.store(next.size(), std::memory_order_relaxed);
      progThreads_.store(used, std::memory_order_relaxed);

      const double us = static_cast<double>(nowUs() - t0) * static_cast<double>(used) /
                        static_cast<double>(done);
      usPerNode = usPerNode > 0 ? 0.75 * usPerNode + 0.25 * us : us;
    }

    if (found) {
      break;
    }

    // the current front is through: open the next
    closed[oldFront].clear();
    oldFront = curFront;
    curFront = newFront;
    newFront = (newFront + 1) % 3;

    level.swap(next);
    next.clear();
    levelNo++;
  }

  if (!found) {
    return nullptr;
  }

  // a position that separates the puzzle: on into the two parts, which
  // calls this search again for each
  return checkSubproblems(found.get(), pieces);
}

namespace {

// The start of the part of `pieces` where st's removed flag is `removed`:
// the positions they had in the position before the separation (in it,
// the removed ones are far away). Their numbers go into pn.
NodeRef newSubproblemStart(const DisassemblerNode* st, std::vector<unsigned int>& pn,
                           const std::vector<unsigned int>& pieces, unsigned int part,
                           bool removed) {
  NodeRef n = NodeRef::make(part);

  unsigned int num = 0;
  const DisassemblerNode* before = st->getComefrom();

  for (unsigned int i = 0; i < pieces.size(); i++) {
    if (st->isPieceRemoved(i) == removed) {
      n->set(num, before->getX(i), before->getY(i), before->getZ(i), before->getTrans(i));
      pn.push_back(pieces[i]);
      num++;
    }
  }

  BT_ASSERT(num == part);
  return n;
}

}  // namespace

std::unique_ptr<Separation> Disassembler::checkSubproblem(unsigned int pieceCount,
                                                          const std::vector<unsigned int>& pieces,
                                                          const DisassemblerNode* st,
                                                          bool removed, bool* ok) {
  std::unique_ptr<Separation> res;

  if (pieceCount == 1) {
    *ok = true;
  } else if (subProbGroup(st, pieces, removed) != 0) {
    *ok = true;
  } else {
    std::vector<unsigned int> pn;
    const NodeRef n = newSubproblemStart(st, pn, pieces, pieceCount, removed);
    res = searchLevels(pn, n);

    // A stopped search has not shown that the pieces cannot come apart, so
    // they are not taken for a group either.
    *ok = res != nullptr || (!aborted() && subProbGrouping(pn));
  }

  return res;
}

std::unique_ptr<Separation> Disassembler::checkSubproblems(const DisassemblerNode* st,
                                                           const std::vector<unsigned int>& pieces) {
  // A position that separates the puzzle into two parts: take each apart
  // in turn and build the tree from them.
  progSeparations_.fetch_add(1, std::memory_order_relaxed);

  unsigned int staying = 0;
  unsigned int leaving = 0;
  for (unsigned int i = 0; i < pieces.size(); i++) {
    if (st->isPieceRemoved(i)) {
      leaving++;
    } else {
      staying++;
    }
  }

  // each part has a piece at least
  BT_ASSERT(staying > 0 && leaving > 0);

  // A part of one piece is through; a part whose pieces are all in one
  // group may stay together; else take it apart, and when that fails, see
  // whether its pieces can be given one group. The removed part only when
  // the staying one is through.
  bool stayingOk = false;
  bool leavingOk = false;
  std::unique_ptr<Separation> stayTree =
      checkSubproblem(staying, pieces, st, false, &stayingOk);
  std::unique_ptr<Separation> removedTree;
  if (stayingOk) {
    removedTree = checkSubproblem(leaving, pieces, st, true, &leavingOk);
  }

  // one part that does not come apart, and the whole does not
  if (!stayingOk || !leavingOk) {
    return nullptr;
  }

  auto erg = std::make_unique<Separation>(ClonePtr<Separation>(std::move(removedTree)),
                                          ClonePtr<Separation>(std::move(stayTree)), pieces);

  // the states, from the separation back to the start
  for (const DisassemblerNode* st2 = st; st2 != nullptr; st2 = st2->getComefrom()) {
    State s(static_cast<unsigned int>(pieces.size()));

    for (unsigned int i = 0; i < pieces.size(); i++) {
      if (st2->isPieceRemoved(i)) {
        // a removed piece has a position before
        const DisassemblerNode* before = st2->getComefrom();
        BT_ASSERT(before != nullptr);

        s.set(i, before->getX(i) + 20000 * st2->getX(i), before->getY(i) + 20000 * st2->getY(i),
              before->getZ(i) + 20000 * st2->getZ(i), before->getTrans(i));
      } else {
        s.set(i, st2->getX(i), st2->getY(i), st2->getZ(i), st2->getTrans(i));
      }
    }

    if (st2->isRotationMove()) {
      const unsigned int code = st2->getDirection() - DisassemblerNode::kRotationDirBase;
      s.setRotationArrival(st2->getRotPiece(), st2->getRotPivotX(), st2->getRotPivotY(),
                           st2->getRotPivotZ(), code / 2, code % 2);
    }

    erg->addstate(std::move(s));
  }

  return erg;
}

// The group all the pieces of the part have, when each has exactly one and
// it is the same; else 0.
unsigned short Disassembler::subProbGroup(const DisassemblerNode* st,
                                          const std::vector<unsigned int>& pn,
                                          bool removed) const {
  unsigned short group = 0;

  for (unsigned int i = 0; i < pn.size(); i++) {
    if (st->isPieceRemoved(i) == removed) {
      const unsigned int part = piece2shape_[pn[i]];
      if (problem_.getNumberOfPartGroups(part) != 1) {
        return 0;
      }
      if (group == 0) {
        group = problem_.getPartGroupId(part, 0);
      } else if (group != problem_.getPartGroupId(part, 0)) {
        return 0;
      }
    }
  }

  return group;
}

bool Disassembler::subProbGrouping(const std::vector<unsigned int>& pn) {
  groups_.newSet();

  for (const unsigned int piece : pn) {
    if (!groups_.addPieceToSet(piece2shape_[piece])) {
      return false;
    }
  }

  return true;
}

TakeApartResult Disassembler::disassemble(const Assembly& assembly, const StopToken& stop) {
  BT_ASSERT(problem_.getNumberOfPieces() == assembly.placementCount());
  groups_.reSet();

  stop_ = stop;
  analyse_->setStop(stop_);

  const NodeRef start = NodeRef::make(assembly);

  if (start->getPiecenumber() < 2) {
    return {};
  }

  // the placed pieces
  std::vector<unsigned int> pieces;
  for (unsigned int j = 0; j < assembly.placementCount(); j++) {
    if (assembly.isPlaced(j)) {
      pieces.push_back(j);
    }
  }

  // for those who watch: from here until the end this take-apart runs
  struct Running {
    std::atomic<std::uint64_t>& startUs;
    explicit Running(std::atomic<std::uint64_t>& s) : startUs(s) {
      startUs.store(nowUs() | 1, std::memory_order_relaxed);
    }
    ~Running() { startUs.store(0, std::memory_order_relaxed); }
    Running(const Running&) = delete;
    Running& operator=(const Running&) = delete;
  };

  progPieces_.store(static_cast<unsigned int>(pieces.size()), std::memory_order_relaxed);
  progSeparations_.store(0, std::memory_order_relaxed);
  progNodes_.store(0, std::memory_order_relaxed);
  progLevel_.store(0, std::memory_order_relaxed);
  progLevelDone_.store(0, std::memory_order_relaxed);
  progLevelSize_.store(0, std::memory_order_relaxed);
  progNextSize_.store(0, std::memory_order_relaxed);

  std::unique_ptr<Separation> s;
  {
    const Running running(progStartUs_);
    s = searchLevels(pieces, start);
  }

  TakeApartResult result;
  if (s) {
    result.separation = std::move(*s);
  } else {
    result.stopped = aborted();
  }
  return result;
}

}  // namespace bt
