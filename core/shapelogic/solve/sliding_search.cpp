// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/sliding.cpp: the slide search (legacySearch over text
// keys; the level search over the key sets of slidelevels.h is in
// slide_level_search.h, the board rules in slide_board.h, the position
// tables in slide_space.h, the sets and saved searches in slide_store.h),
// findSlidePath, slideRoute, applySlideRoutes and finalPlacementKey. The
// level search's streamed levels, its search from both ends, its thinned
// levels and its saved searches are new (see LevelSearch).
#include "shapelogic/solve/sliding_search.h"

#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/slide_marks.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/solve/disasm_to_moves.h"
#include "shapelogic/solve/slide_level_search.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <type_traits>

namespace bt::sliding {

using namespace detail;

namespace {

// The original slide search: an arrangement is a text key, and each move
// works out the free cells afresh. Kept for trays the position tables
// cannot pack into the widest key (eachKey), and for A/B runs
// (SlideSearch::legacy). It tells the copies of a piece apart.
void legacySearch(const Voxel& tray, const std::vector<Cell3List>& cells,
                  const std::vector<Cell3List>& outlines, bool nested, const SlideState& initial,
                  const GoalCheck& goals, const StopGrid& stops, SlideSearch& search,
                  std::vector<SlideState>& path) {
  const auto n = static_cast<unsigned int>(initial.places.size());

  std::queue<SlideState> q;
  // parent[key] = {the key it came from, the piece moved}; n for the root
  std::map<std::string, std::pair<std::string, unsigned int>> parent;

  const std::string startKey = stateKey(initial);

  // Each arrangement held costs a map node with two copies of its key, plus
  // its share of the queue; measured at about 115 + 2 bytes per key
  // character, so this leaves some room.
  const std::uint64_t stateBytes = 128 + 3 * std::uint64_t{startKey.size()};
  // a shared budget is read again as the search goes (another search
  // starting shrinks it)
  const bool shared = search.maxMemoryStates == 0 && search.memoryShare != nullptr;
  std::uint64_t memoryLimit =
      search.maxMemoryStates != 0
          ? search.maxMemoryStates
          : memoryStates(stateBytes, search.highMemory, search.memoryBytes, search.memoryShare);
  search.memoryStates = memoryLimit;
  q.push(initial);
  parent[startKey] = {"", n};

  std::uint64_t visited = 0;
  // what the progress has been told of visited
  std::uint64_t told = 0;
  std::string goalKey;
  bool found = false;

  while (!q.empty()) {
    if (parent.size() >= memoryLimit) {
      search.outcome = SlideOutcome::Memory;
      break;
    }
    if ((visited & 1023) == 0) {
      if (search.progress != nullptr) {
        search.progress->fetch_add(visited - told, std::memory_order_relaxed);
        told = visited;
      }
      if (search.stop.stopRequested()) {
        search.outcome = SlideOutcome::Stopped;
        break;
      }
      if (shared) {
        memoryLimit = memoryStates(stateBytes, false, 0, search.memoryShare);
        search.memoryStates = memoryLimit;
      }
    }
    const SlideState cur = q.front();
    q.pop();
    visited++;
    putSample(search.sample, cur);

    // One move is one piece going anywhere it can reach while the others
    // stay put, round corners included, so the search finds the fewest
    // moves. Counting single-cell steps or straight runs instead leaves
    // equally short paths that alternate pieces, or stop a piece at a
    // corner and come back to it after another piece has moved. With
    // nested slides a piece may also carry what is nested inside it.
    const std::string curKey = stateKey(cur);
    for (unsigned int pi = 0; pi < n && !found; pi++) {
      std::vector<std::vector<unsigned int>> moverSets{{pi}};
      if (nested) {
        // Partial nesting adds a larger group; it does not replace the
        // wholly nested one, or moves that leave a partly nested piece
        // behind would be lost.
        std::vector<unsigned int> group = nestedGroup(cells, outlines, cur, pi, false);
        const std::size_t whole = group.size();
        if (whole > 1) {
          moverSets.push_back(std::move(group));
        }
        if (search.partialNested) {
          std::vector<unsigned int> wider = nestedGroup(cells, outlines, cur, pi, true);
          if (wider.size() > whole) {
            moverSets.push_back(std::move(wider));
          }
        }
      }
      for (const std::vector<unsigned int>& movers : moverSets) {
        for (const SlideShift& shift :
             reachableShifts(tray, cells, cur, movers, search.verticalMoves)) {
          SlideState next = cur;
          bool rests = true;
          for (const unsigned int m : movers) {
            next.places[m].x += shift[0];
            next.places[m].y += shift[1];
            next.places[m].z += shift[2];
            rests = rests && stops.allows(m, next.places[m].x, next.places[m].y);
          }
          if (!rests) {
            continue;
          }
          std::string key = stateKey(next);
          if (parent.count(key) != 0) {
            continue;
          }
          parent[key] = {curKey, pi};
          if (goals.all(next)) {
            goalKey = key;
            found = true;
            q = {};
            break;
          }
          q.push(next);
        }
        if (found) {
          break;
        }
      }
    }
  }

  search.visited = visited;
  if (search.progress != nullptr) {
    search.progress->fetch_add(visited - told, std::memory_order_relaxed);
  }
  if (!found) {
    return;
  }

  // The path back from the goal. States keep each piece's placement so the
  // player can put them straight onto the board.
  std::string k = goalKey;
  for (;;) {
    SlideState st;
    st.places.resize(n);
    std::istringstream is(k);
    for (unsigned int i = 0; i < n; i++) {
      char colon = 0;
      char comma = 0;
      char bar = 0;
      int t = 0;
      int x = 0;
      int y = 0;
      int z = 0;
      if (i != 0) {
        is >> bar;
      }
      is >> t >> colon >> x >> comma >> y >> comma >> z;
      st.places[i].trans = static_cast<unsigned char>(t);
      st.places[i].x = x;
      st.places[i].y = y;
      st.places[i].z = z;
    }
    path.push_back(st);
    auto it = parent.find(k);
    if (it == parent.end() || it->second.second == n) {
      break;
    }
    k = it->second.first;
  }
  std::reverse(path.begin(), path.end());
}

// Tries a search in each key that might hold it, the narrowest first, until
// one does more than not fit: attempt(a tag of the key type, pack), which
// tableSearch or tableProbe answers. What the last one tried said.
//
// A code for each piece in 64 bits, then in 128, as ever: those searches
// are bit for bit what they were. Then the copies of a piece as one number
// (packed keys, SlideSpace::Pack) in 128 bits, and in the wider keys, 192,
// 256 and 512 bits, where a key costs more to sort and to keep. Loose stops
// are never packed, so with them the wider keys hold a code for each piece.
// SlideSearch::packing and minKeyBits are the A/B switches.
template <class F>
Tabled eachKey(const SlideSearch& search, bool loose, F&& attempt) {
  const bool mayPack = !loose && search.packing != SlidePacking::Never;
  const bool always = mayPack && search.packing == SlidePacking::Always;
  Tabled done = Tabled::NoFit;
  const auto tryKey = [&](auto key, unsigned int bits, bool pack) {
    if (done == Tabled::NoFit && bits >= search.minKeyBits) {
      done = attempt(key, pack);
    }
  };
  tryKey(std::type_identity<std::uint64_t>{}, 64, always);
#ifdef __SIZEOF_INT128__
  tryKey(std::type_identity<WideKey>{}, 128, always);
  if (mayPack && !always) {
    tryKey(std::type_identity<WideKey>{}, 128, true);
  }
#else
  tryKey(std::type_identity<BigKey<2>>{}, 128, mayPack);
#endif
  tryKey(std::type_identity<BigKey<3>>{}, 192, mayPack);
  tryKey(std::type_identity<BigKey<4>>{}, 256, mayPack);
  tryKey(std::type_identity<BigKey<8>>{}, 512, mayPack);
  return done;
}

// One table search findSlidePath tries: the tables' form but for packing
// (eachKey chooses it with the key), and whether only a saved search is
// carried on.
struct SlideAttempt {
  bool loose = false;
  bool plainKeys = false;
  bool savedOnly = false;
  bool bothEnds = false;

  [[nodiscard]] TableForm form(bool pack) const { return TableForm{loose, plainKeys, pack, bothEnds}; }
};

// The table searches findSlidePath tries for one start, in order, until one
// searches (cleanup item 11.7). savedFromStartAlone probes the same list,
// so the advice Continue gives (sliding.resumeAdvice) follows any change of
// order. `resumeSaved`: a grid, `resume`, and saved searches of the problem
// in the work folder.
std::vector<SlideAttempt> slideAttempts(const SlideSearch& search, bool nested, bool resumeSaved) {
  std::vector<SlideAttempt> out;
  const bool grid = search.grid > 1;
  // Loose stops only with nested slides: one-way moves, which the loose
  // arrangements make more of, are looked for in the levels kept, and only
  // nested slides keep them.
  const bool wantLoose = nested && grid && !search.gridStopsApart;
  // A search saved for this start is carried on as it was begun: with
  // loose stops or with them apart, and with the keys it has (those saved
  // by an earlier build have a code for every position). Only a grid
  // gives the tables more than one form. (A search from both ends is one
  // with the stops apart.)
  if (resumeSaved) {
    for (const bool loose : {true, false}) {
      for (const bool plainKeys : {false, true}) {
        if ((wantLoose || !loose) && (plainKeys || !search.plainKeys)) {
          out.push_back({.loose = loose, .plainKeys = plainKeys, .savedOnly = true});
        }
      }
    }
  }
  // From both ends where the search can: the two ends hold the same
  // arrangements, so the stops are apart, and what that costs (up to
  // about three times the arrangements a level) is little beside the
  // levels the meeting saves.
  if (wantLoose && !search.oneEnd) {
    out.push_back({.plainKeys = search.plainKeys, .bothEnds = true});
  }
  // Loose stops in either key before stops apart: they may take a bit
  // more a piece than the narrow key has.
  if (wantLoose) {
    out.push_back({.loose = true, .plainKeys = search.plainKeys});
  }
  out.push_back({.plainKeys = search.plainKeys});
  return out;
}

// What a search of a problem from a start works on. Filled in place and
// never moved: the goals look at the pieces through `pieces`.
struct SlideSetup {
  SlideSetup() = default;
  SlideSetup(const SlideSetup&) = delete;
  SlideSetup& operator=(const SlideSetup&) = delete;

  // The pieces as placed at the start; owned here, seen through `pieces`.
  std::vector<std::unique_ptr<Voxel>> owned;
  std::vector<const Voxel*> pieces;
  SlideState initial;
  std::optional<GoalCheck> goals;
  std::vector<Cell3List> cells;
  std::vector<Cell3List> outlines;
  StopGrid stops;

  // The pieces, the start and the goals; false when the start does not
  // place every piece.
  bool place(const Puzzle& puzzle, const Problem& problem, const Assembly& start) {
    const unsigned int n = start.placementCount();
    if (n == 0) {
      return false;
    }
    owned.resize(n);
    pieces.assign(n, nullptr);
    initial.places.resize(n);
    unsigned int pc = 0;
    for (unsigned int part = 0; part < problem.getNumberOfParts(); part++) {
      for (unsigned int j = 0; j < problem.getPartMaximum(part); j++) {
        if (pc >= n || !start.isPlaced(pc)) {
          return false;
        }
        owned[pc] = problem.partShape(puzzle, part).clone();
        if (!owned[pc]->transform(start.getTransformation(pc))) {
          return false;
        }
        pieces[pc] = owned[pc].get();
        initial.places[pc].x = start.getX(pc);
        initial.places[pc].y = start.getY(pc);
        initial.places[pc].trans = start.getTransformation(pc);
        initial.places[pc].z = start.getZ(pc);
        pc++;
      }
    }
    goals.emplace(puzzle, problem, pieces);
    return true;
  }

  // The pieces' cells and outlines and the grid's stops (SlideSearch::grid;
  // none below 2), after place.
  void shapes(const Voxel& tray, bool nested, int gridSize) {
    // the grid's holes are the outlines' too
    const bool grid = gridSize > 1;
    const std::size_t n = pieces.size();
    cells.resize(n);
    outlines.resize(n);
    for (std::size_t i = 0; i < n; i++) {
      cells[i] = cellsOf(*pieces[i]);
      if (nested || grid) {
        outlines[i] = outlineOf(*pieces[i]);
      }
    }
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): a setup that was placed has its goals
    stops = grid ? stopGridFor(gridSize, cells, outlines, knownPlaces(tray, cells, initial, *goals))
                 : stopGridFor(0, cells, outlines, {});
  }
};
}  // namespace

std::int64_t slideClockMs() {
  const std::int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count();
  return ms == 0 ? 1 : ms;
}

void SlidePhaseProgress::begin(SlidePhase p, std::uint64_t ofTotal) {
  done.store(0, std::memory_order_relaxed);
  total.store(ofTotal, std::memory_order_relaxed);
  since.store(slideClockMs(), std::memory_order_relaxed);
  phase.store(p, std::memory_order_release);
}

void SlidePhaseProgress::end() {
  phase.store(SlidePhase::None, std::memory_order_release);
  since.store(0, std::memory_order_relaxed);
  done.store(0, std::memory_order_relaxed);
  total.store(0, std::memory_order_relaxed);
}

std::vector<std::string> savedFolders(const Puzzle& puzzle, const Problem& problem,
                                      const std::string& workDir) {
  std::vector<std::string> out;
  if (workDir.empty() || !problem.resultValid()) {
    return out;
  }
  const std::uint64_t key = problemKeyOf(puzzle, problem);
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(workDir, ec)) {
    if (!e.is_directory(ec) || !e.path().filename().string().starts_with("slide-")) {
      continue;
    }
    SavedSearch st;
    if (readState(e.path(), st, false) && st.problemKey == key) {
      out.push_back(e.path().string());
    }
  }
  std::ranges::sort(out);
  return out;
}

bool pausedWithNothingSaved(const Puzzle& puzzle, const Problem& problem,
                            const std::string& workDir) {
  return problem.getSolveState() == ProblemState::Solving && !problem.getPending().empty() &&
         problem.getNumAssemblies() == 0 && problem.getNumSolutions() == 0 &&
         savedFolders(puzzle, problem, workDir).empty();
}

std::string solveLockPath(const Puzzle& puzzle, const Problem& problem,
                          const std::string& workDir) {
  if (workDir.empty() || !problem.resultValid()) {
    return "";
  }
  char name[40];
  std::snprintf(name, sizeof(name), "solve-%016llx.lock",
                static_cast<unsigned long long>(problemKeyOf(puzzle, problem)));
  return (std::filesystem::path(workDir) / name).string();
}

void discardSaved(const Puzzle& puzzle, const Problem& problem, const std::string& workDir) {
  std::error_code ec;
  for (const std::string& dir : savedFolders(puzzle, problem, workDir)) {
    std::filesystem::remove_all(dir, ec);
  }
}

namespace {

// The pieces of a path in problem order, each its part's shape turned as
// the state says, with their places: what slideRoute works on.
bool pathPieces(const Puzzle& puzzle, const Problem& problem, const State& a, unsigned int n,
                std::vector<Cell3List>& cells, SlideState& st) {
  unsigned int pc = 0;
  for (unsigned int part = 0; part < problem.getNumberOfParts() && pc < n; part++) {
    for (unsigned int j = 0; j < problem.getPartMaximum(part) && pc < n; j++, pc++) {
      std::unique_ptr<Voxel> v = problem.partShape(puzzle, part).clone();
      (void)v->transform(a.getOrient(pc));
      cells.push_back(cellsOf(*v));
      st.places.push_back(
          Place{a.getX(pc), a.getY(pc), static_cast<unsigned char>(a.getOrient(pc)), a.getZ(pc)});
    }
  }
  return pc == n;
}

}  // namespace

SlideNaming slideNaming(const SlideSettings& settings, int grid) {
  // Every setting by its name: one added to SlideSettings fails the build
  // here, until it is said below whether it names a search (a member of
  // SlideNaming, with its row in slideNameValues) or why it does not.
  const auto& [highMemory, memoryBytes, nested, partialNested, verticalMoves, workDir, diskBudget,
               compression, failWriteAt, failWriteOnce, oneEnd, legacy, likePiecesApart, packing,
               minKeyBits, gridStopsApart, plainKeys] = settings;
  SlideNaming n;
  n.nested = nested || partialNested;
  n.partialNested = partialNested;
  n.verticalMoves = verticalMoves;
  n.grid = grid > 1 ? grid : 0;
  n.likePiecesApart = likePiecesApart;
  n.packing = packing;
  n.minKeyBits = minKeyBits;
  n.plainKeys = plainKeys;
  n.gridStopsApart = gridStopsApart;
  n.legacy = legacy;
  // No part of a name:
  // - highMemory, memoryBytes: how much memory the search may fill;
  // - workDir, diskBudget: where the search is saved and how much it may take there
  //   (the refusal asks the paused run's folder what is saved);
  // - compression: a saved search is carried on whatever it was written with;
  // - failWriteAt, failWriteOnce: the tests' failing disk;
  // - oneEnd: a saved search with a goal end is carried on with it, and one from the
  //   start alone on a grid is found under its own name (slideAttempts).
  return n;
}

SlideNaming slideNaming(const SlidingOptions& options, int grid) {
  // (optimizedPaths and options.grid are in `grid`: searchGrid)
  SlideNaming n = slideNaming(static_cast<const SlideSettings&>(options), grid);
  n.anyOrientation = options.anyOrientation;
  n.mirroredOrientations = options.anyOrientation && options.mirroredOrientations;
  return n;
}

std::array<SlideNameValue, kSlideNameValues> slideNameValues(const SlideNaming& naming) {
  // Every member by its name: one added to SlideNaming fails the build
  // here until it has its row.
  const auto& [nested, partialNested, verticalMoves, anyOrientation, mirroredOrientations, grid,
               likePiecesApart, packing, minKeyBits, plainKeys, gridStopsApart, legacy] = naming;
  const auto flag = [](bool on) { return std::uint64_t{on ? 1U : 0U}; };
  return {{
      {"nested slides", flag(nested)},
      {"partially nested slides", flag(partialNested)},
      {"vertical moves", flag(verticalMoves)},
      {"any orientation for unplaced pieces", flag(anyOrientation)},
      {"mirrored orientations", flag(mirroredOrientations)},
      {"optimized paths", static_cast<std::uint64_t>(grid)},
      {"like pieces told apart", flag(likePiecesApart)},
      {"how the copies of a piece are kept in a key", static_cast<std::uint64_t>(packing)},
      {"the narrowest key", minKeyBits},
      {"a code for every position", flag(plainKeys)},
      {"the stops of a grid cell told apart", flag(gridStopsApart)},
      {"the search over text keys", flag(legacy)},
  }};
}

std::vector<std::string_view> namesDiffer(const SlideNaming& a, const SlideNaming& b) {
  const std::array<SlideNameValue, kSlideNameValues> was = slideNameValues(a);
  const std::array<SlideNameValue, kSlideNameValues> now = slideNameValues(b);
  std::vector<std::string_view> out;
  for (std::size_t i = 0; i < was.size(); i++) {
    if (was[i].value != now[i].value) {
      out.push_back(was[i].label);
    }
  }
  return out;
}

SlideSearch slideSearchFor(const SlidingOptions& options) {
  SlideSearch s;
  static_cast<SlideSettings&>(s) = static_cast<const SlideSettings&>(options);
  s.grid = options.grid > 1 ? options.grid : 0;
  return s;
}

SlideGrid slideGridFor(const Puzzle& puzzle, const Problem& problem) {
  SlideGrid none;
  if (!problem.resultValid() || !usesStartMarks(puzzle, problem)) {
    return none;
  }
  const Voxel& tray = problem.resultShape(puzzle);
  const SlideMarks& marks = problem.slideMarks();

  // the floor's bounding box: the board the grid divides
  int fx0 = asInt(tray.getX());
  int fy0 = asInt(tray.getY());
  int fx1 = -1;
  int fy1 = -1;
  for (unsigned int z = 0; z < tray.getZ(); z++) {
    for (unsigned int y = 0; y < tray.getY(); y++) {
      for (unsigned int x = 0; x < tray.getX(); x++) {
        if (tray.getState(x, y, z) != Voxel::kEmpty) {
          fx0 = std::min(fx0, asInt(x));
          fy0 = std::min(fy0, asInt(y));
          fx1 = std::max(fx1, asInt(x));
          fy1 = std::max(fy1, asInt(y));
        }
      }
    }
  }
  if (fx1 < 0) {
    return none;
  }
  const int W = fx1 - fx0 + 1;
  const int H = fy1 - fy0 + 1;

  // one entry a part, as drawn (a stamp is), with every copy's starts and
  // goals: the copies share their offsets anyway
  const unsigned int parts = problem.getNumberOfParts();
  std::vector<Cell3List> cells(parts);
  std::vector<Cell3List> outlines(parts);
  std::vector<std::vector<Place>> known(parts);
  std::vector<std::array<int, 3>> lo(parts);
  std::vector<std::array<int, 2>> extent(parts);
  bool anyStart = false;
  for (unsigned int part = 0; part < parts; part++) {
    const Voxel& piece = problem.partShape(puzzle, part);
    cells[part] = cellsOf(piece);
    outlines[part] = outlineOf(piece);
    if (cells[part].empty()) {
      return none;
    }
    std::array<int, 3> mn{cells[part][0].x, cells[part][0].y, cells[part][0].z};
    std::array<int, 2> mx{mn[0], mn[1]};
    for (const Cell3& c : cells[part]) {
      mn = {std::min(mn[0], c.x), std::min(mn[1], c.y), std::min(mn[2], c.z)};
      mx = {std::max(mx[0], c.x), std::max(mx[1], c.y)};
    }
    lo[part] = mn;
    extent[part] = {mx[0] - mn[0] + 1, mx[1] - mn[1] + 1};
    const unsigned int shapeId = problem.getShapeIdOfPart(part);
    for (const bool goal : {false, true}) {
      const auto copies = stampCopies(tray, marks, piece, shapeId, goal);
      if (!copies) {
        return none;
      }
      for (const std::vector<TrayCell>& copy : *copies) {
        if (copy.empty()) {
          continue;
        }
        std::array<int, 3> at = copy[0];
        for (const TrayCell& c : copy) {
          at = {std::min(at[0], c[0]), std::min(at[1], c[1]), std::min(at[2], c[2])};
        }
        known[part].push_back(Place{at[0] - mn[0], at[1] - mn[1], 0, at[2] - mn[2]});
        anyStart = anyStart || !goal;
      }
    }
  }
  if (!anyStart) {
    return none;
  }

  for (int size = std::min(W, H) / 2; size > 1; size--) {
    if (W % size != 0 || H % size != 0) {
      continue;
    }
    // the blocks, which measure the grid's cells, start and end on its lines
    bool blocks = false;
    bool aligned = true;
    for (unsigned int part = 0; part < parts && aligned; part++) {
      if (extent[part][0] % size != 0 || extent[part][1] % size != 0) {
        continue;
      }
      for (const Place& p : known[part]) {
        blocks = true;
        aligned = aligned && (p.x + lo[part][0] - fx0) % size == 0 &&
                  (p.y + lo[part][1] - fy0) % size == 0;
      }
    }
    if (!blocks || !aligned) {
      continue;
    }

    const StopGrid g = stopGridFor(size, cells, outlines, known);
    SlideGrid out;
    out.size = size;
    for (unsigned int part = 0; part < parts; part++) {
      std::uint64_t all = 0;
      std::uint64_t kept = 0;
      for (int z = 0; z < asInt(tray.getZ()); z++) {
        for (int y = 0; y < asInt(tray.getY()); y++) {
          for (int x = 0; x < asInt(tray.getX()); x++) {
            // a place a piece can stop on: every cell on a normal cell
            const bool stop =
                std::all_of(cells[part].begin(), cells[part].end(), [&](const Cell3& c) {
                  return isFloor(tray, x + c.x, y + c.y, z + c.z) &&
                         tray.getState(asUnsigned(x + c.x), asUnsigned(y + c.y),
                                       asUnsigned(z + c.z)) == Voxel::kFilled;
                });
            if (stop) {
              all++;
              kept += g.allows(part, x, y) ? 1 : 0;
            }
          }
        }
      }
      const std::uint64_t copies = problem.getPartMaximum(part);
      out.stops += all * copies;
      out.gridStops += kept * copies;
    }
    if (out.gridStops < out.stops) {
      return out;
    }
  }
  return none;
}

int searchGrid(const Puzzle& puzzle, const Problem& problem, const SlidingOptions& options) {
  if (options.grid > 1) {
    return options.grid;
  }
  return options.optimizedPaths ? slideGridFor(puzzle, problem).size : 0;
}

std::optional<Separation> findSlidePath(const Puzzle& puzzle, const Problem& problem,
                                        const Assembly& start, bool nested) {
  SlideSearch search;
  search.nested = nested;
  return findSlidePath(puzzle, problem, start, search);
}

std::optional<Separation> findSlidePath(const Puzzle& puzzle, const Problem& problem,
                                        const Assembly& start, SlideSearch& search) {
  const SlideNaming naming = slideNaming(search, search.grid);
  const bool nested = naming.nested;
  search.outcome = SlideOutcome::NoPath;
  search.visited = 0;
  search.keyWidth = 0;
  search.keyBits = 0;
  search.packedKeys = false;
  if (!problem.resultValid()) {
    return std::nullopt;
  }

  const Voxel& tray = problem.resultShape(puzzle);
  SlideSetup setup;
  if (!setup.place(puzzle, problem, start)) {
    return std::nullopt;
  }
  const unsigned int n = start.placementCount();
  const SlideState& initial = setup.initial;
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): a setup that was placed has its goals
  const GoalCheck& goals = *setup.goals;

  std::vector<unsigned int> pcs(n);
  for (unsigned int i = 0; i < n; i++) {
    pcs[i] = i;
  }

  if (goals.all(initial)) {
    search.outcome = SlideOutcome::Found;
    Separation sep(ClonePtr<Separation>(), ClonePtr<Separation>(), pcs);
    State st(n);
    for (unsigned int i = 0; i < n; i++) {
      st.set(i, initial.places[i].x, initial.places[i].y, start.getZ(i), initial.places[i].trans);
    }
    sep.addstate(std::move(st));
    return sep;
  }

  const bool grid = naming.grid > 1;
  setup.shapes(tray, nested, naming.grid);
  const std::vector<Cell3List>& cells = setup.cells;
  const std::vector<Cell3List>& outlines = setup.outlines;
  const StopGrid& stops = setup.stops;

  // start to goal, in order; empty when no path was found
  std::vector<SlideState> path;
  bool searched = false;
  if (!search.legacy) {
    // The tables in the narrowest key that holds them (eachKey), in the
    // order slideAttempts gives.
    const SlideBoard board{puzzle, problem, tray, cells, outlines, naming, initial, goals, stops};
    const bool resumeSaved =
        grid && search.resume && !savedFolders(puzzle, problem, search.workDir).empty();
    for (const SlideAttempt& a : slideAttempts(search, nested, resumeSaved)) {
      searched = eachKey(search, a.loose, [&]<class Key>(std::type_identity<Key>, bool pack) {
                   return tableSearch<Key>(board, a.form(pack), a.savedOnly, search, path);
                 }) == Tabled::Searched;
      if (searched) {
        break;
      }
    }
  }
  if (!searched) {
    if (search.onLog) {
      search.onLog(LogLevel::Info,
                   search.legacy
                       ? "The slide search runs over text keys, as asked (the original's "
                         "search): every arrangement kept in memory, the slowest search."
                       : "The slide search runs over text keys (the original's search): its "
                         "arrangements fit no key up to 512 bits. Every arrangement is kept "
                         "in memory, and it is the slowest search.");
    }
    legacySearch(tray, cells, outlines, nested, initial, goals, stops, search, path);
  }

  if (path.empty()) {
    return std::nullopt;
  }
  search.outcome = SlideOutcome::Found;

  Separation sep(ClonePtr<Separation>(), ClonePtr<Separation>(), pcs);

  // The player puts each piece at these coordinates, so they are the
  // assembly placement, not a shift from it.
  for (std::size_t si = path.size(); si-- > 0;) {
    State st(n);
    for (unsigned int i = 0; i < n; i++) {
      st.set(i, path[si].places[i].x, path[si].places[i].y, path[si].places[i].z,
             path[si].places[i].trans);
    }
    sep.addstate(std::move(st));
  }
  return sep;
}

bool savedFromStartAlone(const Puzzle& puzzle, const Problem& problem, const Assembly& start,
                         const SlideSearch& search) {
  const SlideNaming naming = slideNaming(search, search.grid);
  const bool nested = naming.nested;
  // what findSlidePath asks before its saved pass carries on a search with
  // loose stops, and before it searches from both ends
  if (!nested || search.grid <= 1 || search.gridStopsApart || search.oneEnd || search.legacy ||
      !search.resume || !problem.resultValid() ||
      savedFolders(puzzle, problem, search.workDir).empty()) {
    return false;
  }
  const Voxel& tray = problem.resultShape(puzzle);
  SlideSetup setup;
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): a setup that was placed has its goals
  if (!setup.place(puzzle, problem, start) || setup.goals->all(setup.initial)) {
    return false;
  }
  setup.shapes(tray, nested, naming.grid);

  // the tables in each key, as findSlidePath tries them (slideAttempts)
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): a setup that was placed has its goals
  const GoalCheck& goals = *setup.goals;
  const SlideBoard board{puzzle, problem,       tray,  setup.cells, setup.outlines,
                         naming, setup.initial, goals, setup.stops};
  auto would = [&](const SlideAttempt& a) {
    return eachKey(search, a.loose, [&]<class Key>(std::type_identity<Key>, bool pack) {
             return tableProbe<Key>(board, a.form(pack), a.savedOnly, search);
           }) == Tabled::Searched;
  };
  const std::vector<SlideAttempt> attempts = slideAttempts(search, nested, true);
  // A search saved with loose stops is one from the start alone (those
  // arrangements have no moves backwards): findSlidePath carries it on when
  // it is the first attempt that searches. The saved ones with loose stops
  // come first, so the probes stop at the first that is not one.
  bool saved = false;
  for (const SlideAttempt& a : attempts) {
    if (!a.savedOnly || !a.loose) {
      break;
    }
    if (would(a)) {
      saved = true;
      break;
    }
  }
  if (!saved) {
    return false;
  }
  // and nothing saved, it would search from both ends
  const auto both = std::ranges::find_if(attempts, &SlideAttempt::bothEnds);
  return both != attempts.end() && would(*both);
}

SlideSearchName searchName(const Puzzle& puzzle, const Problem& problem, const Assembly& start,
                           const SlideSearch& search) {
  SlideSearchName out;
  const SlideNaming naming = slideNaming(search, search.grid);
  const bool nested = naming.nested;
  if (search.legacy || !problem.resultValid()) {
    return out;
  }
  const Voxel& tray = problem.resultShape(puzzle);
  SlideSetup setup;
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): a setup that was placed has its goals
  if (!setup.place(puzzle, problem, start) || setup.goals->all(setup.initial)) {
    return out;
  }
  setup.shapes(tray, nested, naming.grid);
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): a setup that was placed has its goals
  const GoalCheck& goals = *setup.goals;
  const SlideBoard board{puzzle, problem,       tray,  setup.cells, setup.outlines,
                         naming, setup.initial, goals, setup.stops};
  // the tables in each key, as findSlidePath tries them with nothing saved
  for (const SlideAttempt& a : slideAttempts(search, nested, false)) {
    const Tabled done =
        eachKey(search, a.loose, [&]<class Key>(std::type_identity<Key>, bool pack) {
          NamedSpace<Key> named;
          if (const std::optional<Tabled> no = nameSpace(board, a.form(pack), named)) {
            return *no;
          }
          out.folder = folderName(named.name);
          out.keyWidth = static_cast<unsigned int>(8 * sizeof(Key));
          out.packedKeys = named.space.packed;
          out.looseStops = named.space.loose;
          return Tabled::Searched;
        });
    if (done == Tabled::Searched) {
      break;
    }
  }
  return out;
}

bool pendingFromStartAlone(const Puzzle& puzzle, const Problem& problem,
                           const SlidingOptions& options) {
  if (problem.getPending().empty()) {
    return false;
  }
  SlideSearch search = slideSearchFor(options);
  search.grid = searchGrid(puzzle, problem, options);
  return std::any_of(problem.getPending().begin(), problem.getPending().end(),
                     [&](const PendingAssembly& p) {
                       return savedFromStartAlone(puzzle, problem, p.assembly, search);
                     });
}

SlideBackCheck checkSlideMovesBack(const Puzzle& puzzle, const Problem& problem,
                                   const Assembly& start, const SlideSearch& search,
                                   std::uint64_t limit) {
  using Key = std::uint64_t;
  SlideBackCheck out;
  if (!problem.resultValid()) {
    return out;
  }
  const bool nested = search.nested || search.partialNested;
  const Voxel& tray = problem.resultShape(puzzle);
  SlideSetup setup;
  if (!setup.place(puzzle, problem, start)) {
    return out;
  }
  setup.shapes(tray, nested, search.grid);
  SlideSpace<Key> space;
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): a setup that was placed has its goals
  const GoalCheck& goals = *setup.goals;
  if (!space.build(tray, setup.cells, setup.outlines, nested, search.partialNested,
                   setup.initial, goals, search.likePiecesApart, search.verticalMoves,
                   setup.stops, false, search.plainKeys, search.packing == SlidePacking::Always)) {
    return out;
  }
  if (!space.backMoves) {
    return out;
  }

  SlideMover<Key> mover(space);
  const auto sorted = [](std::vector<Key>& v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
  };
  const auto has = [](const std::vector<Key>& v, Key k) {
    return std::binary_search(v.begin(), v.end(), k);
  };
  // an arrangement's moves, worked out once (also of one the start does
  // not lead to, which the backward side may give)
  std::map<Key, std::vector<Key>> after;
  const auto movesOf = [&](Key x) -> const std::vector<Key>& {
    const auto [it, fresh] = after.try_emplace(x);
    if (fresh) {
      std::vector<Key>& to = it->second;
      mover.moves(x, [&](Key raw, bool) {
        to.push_back(space.canon(raw));
        return false;
      });
      sorted(to);
    }
    return it->second;
  };

  std::vector<Key> reached{space.startCanon};
  std::set<Key> seen{space.startCanon};
  for (std::size_t head = 0; head < reached.size(); head++) {
    if (reached.size() > limit) {
      return out;
    }
    const Key x = reached[head];
    for (const Key y : movesOf(x)) {
      out.moves++;
      if (seen.insert(y).second) {
        reached.push_back(y);
      }
    }
  }
  out.arrangements = reached.size();

  std::map<Key, std::vector<Key>> before;
  std::vector<Key> twoWay;
  for (const Key y : reached) {
    std::vector<Key>& from = before[y];
    twoWay.clear();
    mover.movesBack(y, [&](Key raw, bool oneWay) {
      const Key x = space.canon(raw);
      from.push_back(x);
      if (oneWay) {
        out.oneWay++;
      } else {
        twoWay.push_back(x);
      }
      return false;
    });
    sorted(from);
    sorted(twoWay);
    for (const Key x : from) {
      out.extra += has(movesOf(x), y) ? 0 : 1;
    }
    for (const Key x : twoWay) {
      out.wronglyTwoWay += has(movesOf(y), x) ? 0 : 1;
    }
  }
  for (const Key x : reached) {
    for (const Key y : movesOf(x)) {
      out.missing += has(before[y], x) ? 0 : 1;
    }
  }
  out.checked = true;
  return out;
}

std::vector<SlideShift> slideRoute(const Puzzle& puzzle, const Problem& problem,
                                   const Separation& path, unsigned int step,
                                   std::vector<unsigned int>* movers) {
  std::vector<SlideShift> none;
  if (movers != nullptr) {
    movers->clear();
  }
  if (!problem.resultValid() || step >= path.getMoves()) {
    return none;
  }
  const State& a = path.getState(step);
  const State& b = path.getState(step + 1);
  const unsigned int n = path.getPieceNumber();

  // The pieces that move all shift by the same amount: one piece, or an
  // outer piece and what is nested inside it.
  std::vector<unsigned int> moving;
  SlideShift shift{0, 0, 0};
  for (unsigned int i = 0; i < n; i++) {
    const SlideShift d{b.getX(i) - a.getX(i), b.getY(i) - a.getY(i), b.getZ(i) - a.getZ(i)};
    if (d == SlideShift{0, 0, 0}) {
      continue;
    }
    if (a.getOrient(i) != b.getOrient(i)) {
      return none;
    }
    if (!moving.empty() && d != shift) {
      return none;
    }
    shift = d;
    moving.push_back(i);
  }
  if (moving.empty()) {
    return none;
  }

  // the pieces in path order are the problem's parts, copy by copy
  std::vector<Cell3List> cells;
  SlideState st;
  if (!pathPieces(puzzle, problem, a, n, cells, st)) {
    return none;
  }
  if (movers != nullptr) {
    *movers = moving;
  }
  // a move that ends in its layer is drawn in it when it can be, which it
  // always can when the search kept the pieces in their layers
  const Voxel& tray = problem.resultShape(puzzle);
  if (shift[2] == 0) {
    ShiftList flat = fewestTurns(tray, cells, st, moving, shift, false);
    if (!flat.empty()) {
      return flat;
    }
  }
  return fewestTurns(tray, cells, st, moving, shift, true);
}

void applySlideRoutes(const Puzzle& puzzle, const Problem& problem, const Separation& path,
                      DisasmToMoves& anim) {
  for (unsigned int step = 0; step < path.getMoves(); step++) {
    std::vector<unsigned int> movers;
    const std::vector<SlideShift> route = slideRoute(puzzle, problem, path, step, &movers);
    // a straight run animates correctly without help
    if (route.size() < 3) {
      continue;
    }
    std::vector<std::array<float, 3>> offsets;
    offsets.reserve(route.size());
    for (const SlideShift& c : route) {
      offsets.push_back(
          {static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2])});
    }
    anim.setRoute(step, movers, offsets);
  }
}

std::string finalPlacementKey(const Separation& path) {
  const State& st = path.getState(path.getMoves());
  std::ostringstream os;
  for (unsigned int i = 0; i < path.getPieceNumber(); i++) {
    if (i != 0) {
      os << '|';
    }
    os << st.getX(i) << ',' << st.getY(i) << ',' << st.getZ(i) << ',' << st.getOrient(i);
  }
  return os.str();
}

}  // namespace bt::sliding
