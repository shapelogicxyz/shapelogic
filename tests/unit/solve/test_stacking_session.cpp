// SPDX-License-Identifier: GPL-3.0-or-later
// Stacking solves in the solve session (new cases beside the port of the
// original's test_stacking.cpp): threads, pauses carried on from the saved
// search, abort, Solve starting afresh, the refusals and notes of
// cb_BtnCont and runStacking, the Stacking Solver on a puzzle the Panex
// search cannot hold, Just Count, and the progress.
#include "../model/model_test_helpers.h"
#include "file_size_limit.h"
#include "../scratch_dir.h"
#include "stacking_test_helpers.h"

#include "shapelogic/base/solve_state.h"
#include "shapelogic/base/stop.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/solution.h"
#include "shapelogic/model/stacking.h"
#include "shapelogic/solve/panex_search.h"
#include "shapelogic/solve/solve_progress.h"
#include "shapelogic/solve/solve_session.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace bt;
using namespace bt::stacking;
using bt::test::addSized;
using bt::test::ScratchDir;

namespace {

// A work folder of this process's own for the saved searches (never a
// fixed name, never the user's cache), removed at exit.
std::string workDir() {
  static const ScratchDir w("stacking_session");
  return w.path().string();
}

// The classic Panex swap of n discs a tower (test_stacking.cpp's).
Puzzle panexSwap(unsigned int n) {
  Puzzle puz{GridType(GridKind::Stacking)};
  RodSet& board = puz.getRodSet(0);
  board.rodCount = 3;
  board.growHeight = false;
  board.definedHeight = n;
  board.sizeMatters = false;
  board.panexColumns = true;
  Problem& pr = puz.getProblem(puz.addProblem());
  for (unsigned int side = 0; side < 2; side++) {
    for (unsigned int size = n; size >= 1; size--) {
      addSized(puz, pr, size);
    }
  }
  pr.setRodSetId(0);
  syncMaps(puz, pr);
  for (unsigned int i = 0; i < n; i++) {
    REQUIRE(placeDisk(puz, pr, false, i, 0).empty());
    REQUIRE(placeDisk(puz, pr, false, n + i, 2).empty());
    REQUIRE(placeDisk(puz, pr, true, n + i, 0).empty());
    REQUIRE(placeDisk(puz, pr, true, i, 2).empty());
  }
  REQUIRE(setupError(puz, pr).empty());
  return puz;
}

SolveOptions findSolutions(unsigned int threads = 0) {
  SolveOptions o;
  o.disassemble = true;
  o.threads = threads;
  o.stacking.workDir = workDir();
  return o;
}

// The frames of the problem's one saved solution, as text, to compare paths.
std::string framesOf(const Problem& pr) {
  REQUIRE(pr.getNumberOfSavedSolutions() == 1);
  const Separation* sep = pr.getSavedSolution(0).getDisassembly();
  REQUIRE(sep != nullptr);
  std::string out;
  for (unsigned int s = 0; s < sep->getNumStates(); s++) {
    const State& st = sep->getState(s);
    for (unsigned int i = 0; i < st.getPiecenumber(); i++) {
      out += std::to_string(st.getX(i)) + "," + std::to_string(st.getZ(i)) + " ";
    }
    out += '\n';
  }
  return out;
}

// The problem's one solution is a path of `moves` transfers, each one disc
// from one rod to another, that ends in the goal stacking.
void checkPath(const Puzzle& puz, const Problem& pr, unsigned int moves) {
  REQUIRE(pr.getNumberOfSavedSolutions() == 1);
  const Separation& path = *pr.getSavedSolution(0).getDisassembly();
  REQUIRE(logicalMoves(path) == moves);
  for (unsigned int m = 0; m < moves; m++) {
    const State& a = path.getState(m * kStepsPerMove);
    const State& b = path.getState((m + 1) * kStepsPerMove);
    unsigned int moved = 0;
    for (unsigned int i = 0; i < path.getPieceNumber(); i++) {
      if (a.getX(i) != b.getX(i)) {
        moved++;
      }
    }
    CHECK(moved == 1);
  }
  const BoardLayout goal = layoutBoard(puz, pr, true);
  const State& last = path.getState(path.getMoves());
  for (unsigned int i = 0; i < path.getPieceNumber(); i++) {
    CHECK(last.getX(i) == goal.disks[i].x);
    CHECK(last.getZ(i) == goal.disks[i].z);
  }
}

// Waits until the run is past `depth` moves (both ends together) or over.
void waitForDepth(const SolveSession& s, std::uint64_t depth) {
  while (isActive(s.state()) && s.progress().searchDepth < depth) {
    std::this_thread::sleep_for(std::chrono::microseconds(200));
  }
}

}  // namespace

TEST_CASE("stacking session: one thread and four find the same path", "[stacking][parallel]") {
  std::string one;
  for (const unsigned int threads : {1U, 4U}) {
    Puzzle puz = panexSwap(4);
    Problem& pr = puz.getProblem(0);
    SolveOptions o = findSolutions(threads);
    o.stacking.solver = StackingSolver::Panex;
    SolveSession s(puz, pr, o);
    REQUIRE(s.start().has_value());
    // live preview pictures asked for all the while
    std::vector<SearchSnapshot> shots;
    while (isActive(s.state())) {
      if (std::optional<SearchSnapshot> shot = s.takeSample()) {
        shots.push_back(std::move(*shot));
      }
      s.requestSample();
      std::this_thread::yield();
    }
    s.wait();
    REQUIRE(s.state() == SolveState::Finished);
    CHECK(s.note().empty());
    CHECK(pr.getSolveState() == ProblemState::Solved);
    CHECK(pr.getNumAssemblies() == 1);
    CHECK(pr.getNumSolutions() == 1);
    checkPath(puz, pr, 128);
    // the placer puts the start stacking where the solution's assembly has it
    Stacking startSt;
    Stacking goalSt;
    std::vector<unsigned int> sizes;
    REQUIRE(searchInput(puz, pr, startSt, goalSt, sizes));
    CHECK(StackingPlacer(puz, pr).place(startSt) == startAssembly(puz, pr).getPlacements());
    // each picture a stacking: every disc on a rod, no two in one place
    const BoardLayout board = layoutBoard(puz, pr, false);
    for (const SearchSnapshot& shot : shots) {
      CHECK(shot.source == SampleSource::Search);
      REQUIRE(shot.placements.size() == board.disks.size());
      std::set<std::pair<int, int>> places;
      for (const Placement& pl : shot.placements) {
        CHECK(pl.transformation == 0);
        CHECK(std::ranges::find(board.rodX, pl.xpos) != board.rodX.end());
        places.emplace(pl.xpos, pl.zpos);
      }
      CHECK(places.size() == shot.placements.size());
    }
    // the solution's assembly is the start stacking
    const Assembly& a = pr.getSavedSolution(0).getAssembly();
    const BoardLayout lay = layoutBoard(puz, pr, false);
    REQUIRE(a.placementCount() == lay.disks.size());
    for (unsigned int i = 0; i < a.placementCount(); i++) {
      CHECK(a.getX(i) == lay.disks[i].x);
      CHECK(a.getZ(i) == lay.disks[i].z);
    }
    if (threads == 1) {
      one = framesOf(pr);
    } else {
      CHECK(framesOf(pr) == one);
    }
  }
}

TEST_CASE("stacking session: a paused Panex search carries on to a shortest path",
          "[stacking][parallel][slow]") {
  // A search carried on from its saved levels traces its path back through
  // them, as the original did, and may take another of the shortest paths
  // than one that kept every level in memory; wherever it was paused, it
  // takes the same one.
  std::string resumed;
  // paused at a sweep of depths (0: before the first level), carried on
  for (const std::uint64_t depth : {0ULL, 40ULL, 120ULL, 200ULL, 300ULL}) {
    INFO("paused at depth " << depth);
    Puzzle puz = panexSwap(5);
    Problem& pr = puz.getProblem(0);
    SolveSession s(puz, pr, findSolutions(4));
    REQUIRE(s.start().has_value());
    waitForDepth(s, depth);
    s.pause();
    s.wait();
    if (s.state() == SolveState::Paused) {
      // as the solve thread left it: solved, one start, no solution, a note
      CHECK(pr.getSolveState() == ProblemState::Solved);
      CHECK(pr.getNumAssemblies() == 1);
      CHECK(pr.getNumSolutions() == 0);
      CHECK(pr.getNumberOfSavedSolutions() == 0);
      CHECK(s.note().starts_with("Paused "));
      CHECK(s.note().ends_with(" moves deep. The search is saved: press Continue to carry on, "
                               "even after quitting ShapeLogic."));
      CHECK_FALSE(panex::savedSearch(puz, pr, workDir()).empty());
      REQUIRE(s.resume().has_value());
      s.wait();
    }
    REQUIRE(s.state() == SolveState::Finished);
    CHECK(s.note().empty());
    CHECK(pr.getNumSolutions() == 1);
    checkPath(puz, pr, 343);
    if (resumed.empty()) {
      resumed = framesOf(pr);
    } else {
      CHECK(framesOf(pr) == resumed);
    }
    // a finished search keeps nothing
    CHECK(panex::savedSearch(puz, pr, workDir()).empty());
  }
}

TEST_CASE("stacking session: a stopped search continues in a new session",
          "[stacking][parallel][slow]") {
  // as after a reload: the session is gone, the saved search is not
  Puzzle puz = panexSwap(5);
  Problem& pr = puz.getProblem(0);
  {
    SolveSession s(puz, pr, findSolutions(2));
    REQUIRE(s.start().has_value());
    waitForDepth(s, 100);
    // the session's end pauses a running solve (the original's destructor)
  }
  if (panex::savedSearch(puz, pr, workDir()).empty()) {
    SKIP("the search was through before the session ended");
  }
  const std::uint64_t ms = panex::savedMs(puz, pr, workDir());
  SolveSession again(puz, pr, findSolutions(2));
  REQUIRE(again.resume().has_value());
  again.wait();
  REQUIRE(again.state() == SolveState::Finished);
  CHECK(logicalMoves(*pr.getSavedSolution(0).getDisassembly()) == 343);
  // the time of both runs: what the saved search had used, and this run
  CHECK(pr.getUsedMs() >= ms);
}

TEST_CASE("stacking session: abort throws the saved search away", "[stacking][parallel]") {
  Puzzle puz = panexSwap(5);
  Problem& pr = puz.getProblem(0);
  SolveSession s(puz, pr, findSolutions(2));
  REQUIRE(s.start().has_value());
  s.pause();
  s.wait();
  REQUIRE(s.state() == SolveState::Paused);
  REQUIRE_FALSE(panex::savedSearch(puz, pr, workDir()).empty());
  s.abort();
  CHECK(s.state() == SolveState::Idle);
  CHECK(pr.getSolveState() == ProblemState::Unsolved);
  CHECK(panex::savedSearch(puz, pr, workDir()).empty());

  // and while it runs
  REQUIRE(s.start().has_value());
  s.abort();
  CHECK(s.state() == SolveState::Idle);
  CHECK(pr.getSolveState() == ProblemState::Unsolved);
  CHECK(panex::savedSearch(puz, pr, workDir()).empty());
}

// An aborted search is thrown away, so it writes nothing first (cleanup
// item 4.3): before, it wrote its kept levels and its whole frontier (deep
// in a search, gigabytes) for the session to delete at once, and the
// engine answered nothing meanwhile.
TEST_CASE("stacking session: an aborted Panex search writes no save", "[stacking]") {
  Puzzle puz = panexSwap(4);
  Problem& pr = puz.getProblem(0);
  panex::discardSaved(puz, pr, workDir());
  panex::PanexSearch first;
  first.workDir = workDir();
  first.threads = 1;
  first.stopAtDepth = 20;
  REQUIRE_FALSE(panex::solve(puz, pr, first).has_value());
  REQUIRE(first.saved);
  const std::filesystem::path folder = panex::searchFolder(puz, pr, workDir());
  REQUIRE(std::filesystem::exists(folder / "state.bin"));

  // carried on, and aborted before its first level: not saved, the folder gone
  StopSource abort;
  abort.request(StopKind::Abort);
  panex::PanexSearch on;
  on.workDir = workDir();
  on.threads = 1;
  on.resume = true;
  on.stop = abort.token();
  CHECK_FALSE(panex::solve(puz, pr, on).has_value());
  CHECK(on.outcome == panex::Outcome::Stopped);
  CHECK_FALSE(on.saved);
  CHECK_FALSE(std::filesystem::exists(folder));

  // a pause, by contrast, saves it
  StopSource pause;
  pause.request(StopKind::Pause);
  panex::PanexSearch paused;
  paused.workDir = workDir();
  paused.threads = 1;
  paused.stop = pause.token();
  CHECK_FALSE(panex::solve(puz, pr, paused).has_value());
  CHECK(paused.outcome == panex::Outcome::Stopped);
  CHECK(paused.saved);
  CHECK(std::filesystem::exists(folder / "state.bin"));
  panex::discardSaved(puz, pr, workDir());
}

#ifndef _WIN32
// A stop whose save fails (the disk full, here once a file is open, as at
// the close of a file: cleanup item 4.1) keeps the saved search as its last
// good save left it (cleanup item 4.2): before, the end of the search took
// the folder for one that needed nothing saved and deleted it, that last
// save with it. The search ends as a failed write ends it, saying the disk
// may be full, and Continue carries the older save on to the path.
TEST_CASE("stacking session: a stop whose save fails keeps the last save", "[stacking]") {
  Puzzle puz = panexSwap(4);
  Problem& pr = puz.getProblem(0);
  auto firstRun = [&](const std::string& dir) {
    panex::discardSaved(puz, pr, dir);
    panex::PanexSearch first;
    first.workDir = dir;
    first.threads = 1;
    first.stopAtDepth = 20;
    REQUIRE_FALSE(panex::solve(puz, pr, first).has_value());
    REQUIRE(first.saved);
  };
  auto secondRun = [&](const std::string& dir, panex::PanexSearch& on) {
    on.workDir = dir;
    on.threads = 1;
    on.resume = true;
    on.stopAtDepth = 26;
    return panex::solve(puz, pr, on).has_value();
  };
  auto listing = [](const std::filesystem::path& folder) {
    std::map<std::string, std::string> files;
    for (const auto& e : std::filesystem::directory_iterator(folder)) {
      std::ifstream in(e.path(), std::ios::binary);
      files[e.path().filename().string()] =
          std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    return files;
  };

  // The same runs in another folder, to learn what the second one writes:
  // its levels must all fit under a limit that its state.bin does not.
  const std::filesystem::path probeDir = std::filesystem::path(workDir()) / "probe";
  std::filesystem::create_directories(probeDir);
  firstRun(probeDir.string());
  {
    panex::PanexSearch on;
    REQUIRE_FALSE(secondRun(probeDir.string(), on));
    REQUIRE(on.saved);
  }
  const std::filesystem::path probe = panex::searchFolder(puz, pr, probeDir.string());
  const std::uintmax_t stateBytes = std::filesystem::file_size(probe / "state.bin");
  std::uintmax_t levelBytes = 0;
  for (const auto& e : std::filesystem::directory_iterator(probe)) {
    if (e.path().extension() == ".lvl") {
      levelBytes = std::max(levelBytes, e.file_size());
    }
  }
  INFO("state.bin " << stateBytes << " bytes, the largest level " << levelBytes);
  REQUIRE(levelBytes + 1 < stateBytes);
  panex::discardSaved(puz, pr, probeDir.string());

  firstRun(workDir());
  const std::filesystem::path folder = panex::searchFolder(puz, pr, workDir());
  const std::map<std::string, std::string> saved = listing(folder);
  REQUIRE(saved.contains("state.bin"));
  panex::PanexSearch on;
  {
    const test::FileSizeLimit full(stateBytes - 1);
    CHECK_FALSE(secondRun(workDir(), on));
  }
  CHECK(on.outcome == panex::Outcome::Error);
  CHECK_FALSE(on.saved);
  CHECK(on.error == "Could not save the search in " + folder.string() + ". The disk may be full.");
  // the last save as it was: its state, and its levels with the new ones
  // the run wrote whole; no temporary file left
  REQUIRE(std::filesystem::exists(folder));
  const std::map<std::string, std::string> after = listing(folder);
  CHECK(after.at("state.bin") == saved.at("state.bin"));
  for (const auto& [name, bytes] : saved) {
    CHECK(after.at(name) == bytes);
  }
  for (const auto& [name, bytes] : after) {
    CHECK_FALSE(name.ends_with(".tmp"));
  }

  // the solve carries the older save on to the path
  SolveSession s(puz, pr, findSolutions(1));
  REQUIRE(s.resume().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  CHECK(pr.getNumSolutions() == 1);
  checkPath(puz, pr, 128);
  CHECK_FALSE(std::filesystem::exists(folder));
}
#endif

// A saved search's state is trusted no further than its file: a damaged or
// crafted state.bin (one imported from a .slsolve) claimed a layer of 2^60
// keys, which the search set out to make room for (an audit finding; a
// claim of a few terabytes had the process killed). And its keys were
// decoded as they came (cleanup item 16.4): one with more separators than
// the rods have, a run of 0xFF bytes, counted the rod down past 0, and the
// discs after it were written far outside the state on a worker's stack.
// Such a state is not carried on. Nor is the saved search thrown away for
// it, or for a state cut short or of a later version (decision 0041): the
// search ends with why, the folder as it was; the solve pauses, Continue
// tries again, and Solve clears the folder and searches from the beginning.
TEST_CASE("stacking session: a saved search that cannot be read is kept, not carried on",
          "[stacking]") {
  // state.bin: the magic (8 bytes), the fingerprint (8), the depths and
  // the interval (4 each), the stackings found and the time (8 each), met,
  // fromStart and fromGoal (4 each), meetAt (16): 72 bytes; then each
  // layer's count (8) and its keys (16 each, the high word first). A key
  // is five bits a symbol, the last one lowest: a disc's number, or 31
  // between two rods. The puzzle here has 8 discs on 3 rods of height 4.
  using Key = std::array<std::uint64_t, 2>;  // high, low
  const auto symbolsOf = [](std::uint64_t low) {
    std::array<unsigned int, 10> symbols{};
    for (unsigned int& v : symbols) {
      v = static_cast<unsigned int>(low & 31);
      low >>= 5;
    }
    return symbols;
  };
  const auto lowOf = [](const std::array<unsigned int, 10>& symbols) {
    std::uint64_t low = 0;
    for (std::size_t i = symbols.size(); i-- > 0;) {
      low = (low << 5) | symbols[i];
    }
    return low;
  };
  struct Damage {
    const char* name;
    const char* why;
    bool described;  // the header alone still describes the search
    std::function<void(Key&)> key;  // what it does to the first key, if anything
  };
  const char* const notOne = "it holds a stacking the puzzle does not have";
  const Damage damages[] = {
      {"a layer of 2^60 keys", "it names more stackings than the file holds", true, nullptr},
      {"cut short", "its state.bin is cut short", false, nullptr},
      {"a later version's", "was written by a later version of the program", false, nullptr},
      {"0xFF over the first key", notOne, true,
       [](Key& k) { k = {~std::uint64_t{0}, ~std::uint64_t{0}}; }},
      {"a first key of no rods", notOne, true, [](Key& k) { k = {0, 0}; }},
      {"a rod too many", notOne, true,
       [&](Key& k) {
         std::array<unsigned int, 10> symbols = symbolsOf(k[1]);
         *std::find_if(symbols.begin(), symbols.end(), [](unsigned int v) { return v != 31; }) = 31;
         k[1] = lowOf(symbols);
       }},
      {"a disc on two rods", notOne, true,
       [&](Key& k) {
         // the first disc becomes the last one
         std::array<unsigned int, 10> symbols = symbolsOf(k[1]);
         const auto disc = [](unsigned int v) { return v != 31; };
         *std::find_if(symbols.begin(), symbols.end(), disc) =
             *std::find_if(symbols.rbegin(), symbols.rend(), disc);
         k[1] = lowOf(symbols);
       }},
      {"a disc the puzzle does not have", notOne, true,
       [&](Key& k) {
         std::array<unsigned int, 10> symbols = symbolsOf(k[1]);
         *std::find_if(symbols.begin(), symbols.end(), [](unsigned int v) { return v != 31; }) = 8;
         k[1] = lowOf(symbols);
       }},
      {"every disc on one rod of four", notOne, true,
       [&](Key& k) { k = {0, lowOf({31, 31, 7, 6, 5, 4, 3, 2, 1, 0})}; }},
      {"a symbol above the last", notOne, true, [](Key& k) { k[1] |= std::uint64_t{1} << 50; }},
      {"a high word", notOne, true, [](Key& k) { k[0] = 1; }},
      {"the ends met at no stacking", notOne, true, nullptr},
  };
  for (const Damage& d : damages) {
    INFO(d.name);
    Puzzle puz = panexSwap(4);
    Problem& pr = puz.getProblem(0);
    panex::discardSaved(puz, pr, workDir());
    panex::PanexSearch first;
    first.workDir = workDir();
    first.threads = 1;
    first.stopAtDepth = 20;
    REQUIRE_FALSE(panex::solve(puz, pr, first).has_value());
    REQUIRE(first.saved);
    const std::filesystem::path folder = panex::searchFolder(puz, pr, workDir());
    const std::filesystem::path state = folder / "state.bin";
    if (d.name == std::string("cut short")) {
      std::filesystem::resize_file(state, 40);
    } else {
      std::fstream f(state, std::ios::in | std::ios::out | std::ios::binary);
      REQUIRE(f.good());
      const auto put = [&f](std::streamoff at, const void* bytes, std::size_t len) {
        f.seekp(at);
        f.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(len));
      };
      if (d.key) {
        // the first layer's first key, after its count
        std::uint64_t count = 0;
        Key key{};
        f.seekg(72);
        f.read(reinterpret_cast<char*>(&count), sizeof(count));
        f.read(reinterpret_cast<char*>(key.data()), sizeof(key));
        REQUIRE(f.good());
        REQUIRE(count > 0);
        const Key was = key;
        d.key(key);
        REQUIRE(key != was);
        put(80, key.data(), sizeof(key));
      } else if (std::string_view(d.why) == notOne) {
        // met, and where: a key of 0xFF bytes
        const std::uint32_t met = 1;
        const Key nowhere = {~std::uint64_t{0}, ~std::uint64_t{0}};
        put(44, &met, sizeof(met));
        put(56, nowhere.data(), sizeof(nowhere));
      } else if (d.described) {
        // the first layer's count
        const std::uint64_t claim = std::uint64_t{1} << 60;
        put(72, &claim, sizeof(claim));
      } else {
        put(7, "5", 1);
      }
      REQUIRE(f.good());
    }
    auto listing = [&] {
      std::map<std::string, std::uintmax_t> files;
      for (const auto& e : std::filesystem::directory_iterator(folder)) {
        files[e.path().filename().string()] = e.file_size();
      }
      return files;
    };
    const std::map<std::string, std::uintmax_t> before = listing();
    REQUIRE(before.size() >= 2);
    CHECK(panex::savedSearch(puz, pr, workDir()).empty() == !d.described);

    // not carried on, and not thrown away: twice
    for (int again = 0; again < 2; again++) {
      panex::PanexSearch on;
      on.workDir = workDir();
      on.threads = 1;
      on.resume = true;
      CHECK_FALSE(panex::solve(puz, pr, on).has_value());
      CHECK(on.outcome == panex::Outcome::Error);
      CHECK(on.savedUnread);
      CHECK_FALSE(on.saved);
      CHECK(on.found == 0);
      CHECK(on.error.starts_with("The saved search in "));
      CHECK(on.error.find(" could not be carried on: ") != std::string::npos);
      CHECK(on.error.find(d.why) != std::string::npos);
      CHECK(on.error.ends_with("It is kept as it is."));
      CHECK(listing() == before);
    }

    // the solve: Continue pauses with the saved search kept, and says so
    SolveSession s(puz, pr, findSolutions(1));
    for (int again = 0; again < 2; again++) {
      REQUIRE(s.resume().has_value());
      s.wait();
      REQUIRE(s.state() == SolveState::Paused);
      const std::string note = s.note();
      INFO(note);
      CHECK(note.find(" could not be carried on: ") != std::string::npos);
      CHECK(note.find(d.why) != std::string::npos);
      CHECK(note.ends_with("Continue tries to carry the saved search on again; Solve throws "
                           "it away and searches from the beginning."));
      CHECK(pr.getNumSolutions() == 0);
      CHECK(listing() == before);
    }
    // Solve throws it away and finds the path
    REQUIRE(s.start().has_value());
    s.wait();
    REQUIRE(s.state() == SolveState::Finished);
    CHECK(s.note().empty());
    CHECK(pr.getNumSolutions() == 1);
    checkPath(puz, pr, 128);
    CHECK_FALSE(std::filesystem::exists(folder));
  }

  // Levels in the folder and no state are no saved search (one that was
  // killed before it saved itself): cleared, and searched from the start.
  {
    Puzzle puz = panexSwap(4);
    Problem& pr = puz.getProblem(0);
    panex::PanexSearch first;
    first.workDir = workDir();
    first.threads = 1;
    first.stopAtDepth = 20;
    REQUIRE_FALSE(panex::solve(puz, pr, first).has_value());
    REQUIRE(first.saved);
    std::filesystem::remove(std::filesystem::path(panex::searchFolder(puz, pr, workDir())) /
                            "state.bin");
    panex::PanexSearch on;
    on.workDir = workDir();
    on.threads = 1;
    on.resume = true;
    const std::optional<Separation> path = panex::solve(puz, pr, on);
    REQUIRE(path.has_value());
    CHECK(on.outcome == panex::Outcome::Found);
    CHECK_FALSE(on.savedUnread);
    CHECK(logicalMoves(*path) == 128);
    CHECK(panex::savedSearch(puz, pr, workDir()).empty());
  }
}

// A search that ends in an error with a saved search still in its folder
// (here: a saved level damaged, so the path cannot be traced back) is not
// through: the solve pauses, the problem is not kept as solved without a
// solution, Continue tries again and Solve searches from the beginning.
TEST_CASE("stacking session: an error with a saved search pauses the solve", "[stacking]") {
  Puzzle puz = panexSwap(4);
  Problem& pr = puz.getProblem(0);
  panex::discardSaved(puz, pr, workDir());
  panex::PanexSearch first;
  first.workDir = workDir();
  first.threads = 1;
  first.stopAtDepth = 60;
  REQUIRE_FALSE(panex::solve(puz, pr, first).has_value());
  REQUIRE(first.saved);
  const std::filesystem::path folder = panex::searchFolder(puz, pr, workDir());
  unsigned int levels = 0;
  for (const auto& e : std::filesystem::directory_iterator(folder)) {
    if (e.path().extension() == ".lvl" && e.file_size() > 0) {
      // the same size, nothing a search would find
      const std::vector<char> nothing(e.file_size(), '\xff');
      std::ofstream out(e.path(), std::ios::binary | std::ios::trunc);
      out.write(nothing.data(), static_cast<std::streamsize>(nothing.size()));
      REQUIRE(out.good());
      levels++;
    }
  }
  REQUIRE(levels > 0);
  SolveSession s(puz, pr, findSolutions(1));
  for (int again = 0; again < 2; again++) {
    REQUIRE(s.resume().has_value());
    s.wait();
    REQUIRE(s.state() == SolveState::Paused);
    CHECK(s.note() ==
          "The Panex Solver could not trace its path back. The search is still saved as it "
          "last was: Continue carries it on; Solve throws it away and searches from the "
          "beginning.");
    CHECK(pr.getNumSolutions() == 0);
    CHECK_FALSE(panex::savedSearch(puz, pr, workDir()).empty());
  }
  REQUIRE(s.start().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  CHECK(s.note().empty());
  CHECK(pr.getNumSolutions() == 1);
  checkPath(puz, pr, 128);
  CHECK_FALSE(std::filesystem::exists(folder));
}

TEST_CASE("stacking session: Solve starts afresh, Continue carries on", "[stacking]") {
  Puzzle puz = panexSwap(4);
  Problem& pr = puz.getProblem(0);
  SolveSession s(puz, pr, findSolutions(1));
  REQUIRE(s.start().has_value());
  s.pause();
  s.wait();
  REQUIRE(s.state() == SolveState::Paused);
  REQUIRE_FALSE(panex::savedSearch(puz, pr, workDir()).empty());

  // Solve throws the saved search away (the GUI asked first) and solves
  REQUIRE(s.start().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  CHECK(pr.getNumSolutions() == 1);

  // Continue on a solved stacking problem starts a search too: there is no
  // saved one to carry on
  REQUIRE(s.resume().has_value());
  s.wait();
  CHECK(s.state() == SolveState::Finished);
  CHECK(pr.getNumSolutions() == 1);
  CHECK(pr.getNumAssemblies() == 1);
}

// A Panex search whose folder cannot be written pauses before it begins
// (decision 0028); Continue tries again, and with a folder it can write
// solves.
TEST_CASE("stacking session: a folder that cannot be written pauses the run",
          "[stacking][parallel]") {
  Puzzle puz = panexSwap(4);
  Problem& pr = puz.getProblem(0);
  const std::filesystem::path file = std::filesystem::path(workDir()) / "not-a-folder";
  { std::ofstream(file) << "x"; }
  SolveOptions o = findSolutions(1);
  o.stacking.workDir = file.string();
  SolveSession s(puz, pr, o);
  REQUIRE(s.start().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Paused);
  const std::optional<FolderFailure> f = s.folderFailure();
  REQUIRE(f);
  CHECK(f->folder == file.string());
  CHECK_FALSE(f->reason.empty());
  CHECK(pr.getNumberOfSavedSolutions() == 0);
  // tried again as it is: paused again
  REQUIRE(s.resume().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Paused);
  CHECK(s.folderFailure());
  // another folder
  o.stacking.workDir = workDir();
  REQUIRE(s.setOptions(o).has_value());
  REQUIRE(s.resume().has_value());
  s.wait();
  CHECK(s.state() == SolveState::Finished);
  CHECK_FALSE(s.folderFailure());
  CHECK(pr.getNumSolutions() == 1);
  std::filesystem::remove(file);
}

// A stacking run holds a lease on the shared memory while it runs, and a
// search that stops at its share while others hold leases says why.
TEST_CASE("stacking session: a run shares the memory while it runs", "[stacking][parallel]") {
  Puzzle puz = panexSwap(4);
  Problem& pr = puz.getProblem(0);
  SolveOptions o = findSolutions(1);
  o.memoryShare = std::make_shared<MemoryShare>(1000000000);
  o.settingsPlace = "Settings";
  // another solve's search, already running, and no room for two
  MemoryShare::Lease other = o.memoryShare->join();
  o.memoryShare->setTotal(2);
  SolveSession s(puz, pr, o);
  REQUIRE(s.start().has_value());
  s.wait();
  // through, at its limit, the search saved for Continue
  REQUIRE(s.state() == SolveState::Finished);
  CHECK(pr.getNumSolutions() == 0);
  CHECK_FALSE(panex::savedSearch(puz, pr, workDir()).empty());
  CHECK(o.memoryShare->holders() == 1);
  CHECK(s.note().find("Other solves running at the same time share the Search memory") !=
        std::string::npos);
  // alone, with the memory back, it carries on to the end
  other.release();
  o.memoryShare->setTotal(1000000000);
  REQUIRE(s.resume().has_value());
  s.wait();
  CHECK(s.state() == SolveState::Finished);
  CHECK(pr.getNumSolutions() == 1);
  CHECK(o.memoryShare->holders() == 0);
}

TEST_CASE("stacking session: what Solve refuses, and the notes", "[stacking]") {
  SECTION("no rod set") {
    Puzzle puz = panexSwap(3);
    puz.getProblem(0).clearRodSet();
    SolveSession s(puz, puz.getProblem(0), findSolutions());
    const Result<void> r = s.start();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == Errc::InvalidArgument);
    CHECK(r.error().message == "Choose a rod set with Set Start/Goal Rods");
  }
  SECTION("Find Solutions off") {
    Puzzle puz = panexSwap(3);
    SolveOptions o = findSolutions();
    o.disassemble = false;
    SolveSession s(puz, puz.getProblem(0), o);
    const Result<void> r = s.start();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().message == "Turn on Find Solutions");
  }
  SECTION("a start that breaks the rules") {
    Puzzle puz = panexSwap(3);
    (void)liftTop(puz, puz.getProblem(0), true, 0);
    SolveSession s(puz, puz.getProblem(0), findSolutions());
    const Result<void> r = s.start();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == Errc::InvalidArgument);
    CHECK(r.error().message == setupError(puz, puz.getProblem(0)));
  }
  SECTION("the Panex Solver without Panex columns") {
    Puzzle puz = panexSwap(3);
    puz.getRodSet(0).panexColumns = false;
    SolveOptions o = findSolutions();
    o.stacking.solver = StackingSolver::Panex;
    SolveSession s(puz, puz.getProblem(0), o);
    const Result<void> r = s.start();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == Errc::Unsupported);
    CHECK(r.error().message.find("Panex Style Columns") != std::string::npos);
    // the Stacking Solver takes it
    o.stacking.solver = StackingSolver::Shortest;
    REQUIRE(s.setOptions(o).has_value());
    REQUIRE(s.start().has_value());
    s.wait();
    CHECK(s.state() == SolveState::Finished);
  }
  SECTION("no path") {
    // Panex Jr without its pocket cannot move at all
    Puzzle puz{GridType(GridKind::Stacking)};
    RodSet& board = puz.getRodSet(0);
    board.rodCount = 2;
    board.growHeight = false;
    board.definedHeight = 3;
    board.sizeMatters = false;
    board.panexColumns = true;
    Problem& pr = puz.getProblem(puz.addProblem());
    for (unsigned int size = 3; size >= 1; size--) {
      addSized(puz, pr, size);
    }
    pr.setRodSetId(0);
    syncMaps(puz, pr);
    for (unsigned int id = 0; id < 3; id++) {
      REQUIRE(placeDisk(puz, pr, false, id, 0).empty());
      REQUIRE(placeDisk(puz, pr, true, id, 1).empty());
    }
    SolveSession s(puz, pr, findSolutions());
    REQUIRE(s.start().has_value());
    s.wait();
    CHECK(s.state() == SolveState::Finished);
    CHECK(s.note() == "Every reachable stacking was searched: this puzzle has no solution.");
    CHECK(pr.getNumAssemblies() == 1);
    CHECK(pr.getNumSolutions() == 0);
  }
}

TEST_CASE("stacking session: the Stacking Solver on a board the Panex search cannot hold",
          "[stacking]") {
  // nine rods: more than the Panex search packs
  Puzzle puz{GridType(GridKind::Stacking)};
  puz.getRodSet(0).rodCount = 9;
  Problem& pr = puz.getProblem(puz.addProblem());
  addSized(puz, pr, 2);
  addSized(puz, pr, 1);
  pr.setRodSetId(0);
  syncMaps(puz, pr);
  REQUIRE(placeDisk(puz, pr, false, 0, 0).empty());
  REQUIRE(placeDisk(puz, pr, false, 1, 0).empty());
  REQUIRE(placeDisk(puz, pr, true, 0, 8).empty());
  REQUIRE(placeDisk(puz, pr, true, 1, 8).empty());
  REQUIRE_FALSE(panex::unsupported(puz, pr, true).empty());

  SolveSession s(puz, pr, findSolutions());
  REQUIRE(s.start().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  CHECK(logicalMoves(*pr.getSavedSolution(0).getDisassembly()) == 3);
  CHECK_FALSE(s.progress().panexSearch);

  // the Panex Solver refuses it, with Panex columns too
  puz.getRodSet(0).panexColumns = true;
  REQUIRE(setupError(puz, pr).empty());
  SolveOptions o = findSolutions();
  o.stacking.solver = StackingSolver::Panex;
  REQUIRE(s.setOptions(o).has_value());
  const Result<void> r = s.start();
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error().message == "The Panex Solver handles at most 8 rods.");
}

TEST_CASE("stacking session: Just Count keeps no solution", "[stacking]") {
  Puzzle puz = test::loadFixture("test_stacking_hanoi.xmpuzzle");
  Problem& pr = puz.getProblem(0);
  SolveOptions o = findSolutions();
  o.justCount = true;
  SolveSession s(puz, pr, o);
  REQUIRE(s.start().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  CHECK(pr.getNumAssemblies() == 1);
  CHECK(pr.getNumSolutions() == 1);
  CHECK(pr.getNumberOfSavedSolutions() == 0);
  // the stackings it went through are recorded (recordSearchStats)
  CHECK(pr.getSearched() > 0);
}

TEST_CASE("stacking session: the progress of a Panex search", "[stacking][parallel]") {
  Puzzle puz = panexSwap(5);
  Problem& pr = puz.getProblem(0);
  SolveOptions o = findSolutions(2);
  o.stacking.solver = StackingSolver::Panex;
  SolveSession s(puz, pr, o);
  REQUIRE(s.start().has_value());
  CHECK(isActive(s.state()));
  bool sawOther = false;
  while (isActive(s.state())) {
    const SolveProgress p = s.progress();
    if (p.stage == SolveStage::Other) {
      sawOther = true;
      CHECK(p.panexSearch);
    }
    std::this_thread::sleep_for(std::chrono::microseconds(500));
  }
  s.wait();
  const SolveProgress p = s.progress();
  CHECK(p.stage == SolveStage::Done);
  // both ends met 343 moves apart, and the depth shown is the moves (the
  // original's GUI showed 342: an odd path met before its depth moved on)
  CHECK(p.searchDepth == 343);
  CHECK(p.searched == pr.getSearched());
  CHECK(pr.getSearchDepth() == p.searchDepth);
  // the run may be over before a look sees it searching
  (void)sawOther;
}

TEST_CASE("stacking session: the Stacking Solver on the Panex search says so",
          "[stacking][parallel]") {
  // the Stacking Solver runs on the Panex search wherever it can hold the
  // puzzle: that search saves itself as it pauses and traces its path
  Puzzle puz = panexSwap(3);
  Problem& pr = puz.getProblem(0);
  REQUIRE(panex::unsupported(puz, pr, true).empty());
  SolveOptions o = findSolutions(2);
  REQUIRE(o.stacking.solver == StackingSolver::Shortest);
  REQUIRE(panex::runsOnPanexSearch(puz, pr, false));
  SolveSession s(puz, pr, o);
  REQUIRE(s.start().has_value());
  while (isActive(s.state())) {
    CHECK(s.progress().panexSearch);
    std::this_thread::sleep_for(std::chrono::microseconds(500));
  }
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  CHECK(s.progress().panexSearch);
}

// One solve of a problem at a time works in its search folder (decision
// 0035): while another holds the problem's lock, Solve and Continue are
// refused; the lock is the run's while it goes.
TEST_CASE("stacking session: a problem another solve works on is refused", "[stacking][lock]") {
  Puzzle puz = panexSwap(5);
  Problem& pr = puz.getProblem(0);
  const std::string lockFile = panex::solveLockPath(puz, pr, workDir());
  REQUIRE_FALSE(lockFile.empty());
  CHECK(panex::solveLockPath(puz, pr, "").empty());

  SolveSession s(puz, pr, findSolutions(1));
  FileLock other;
  REQUIRE(other.tryAcquire(lockFile) == FileLock::Outcome::Held);
  for (const bool again : {false, true}) {
    const Result<void> r = again ? s.resume() : s.start();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == Errc::IoError);
    CHECK(r.error().message == kSolvedElsewhere);
    CHECK(s.state() == SolveState::Idle);
  }
  other.release();

  REQUIRE(s.start().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  CHECK(pr.getNumSolutions() == 1);
  CHECK_FALSE(std::filesystem::exists(lockFile));
  // the Stacking Solver's own search keeps nothing on disk: no lock
  SolveOptions plain = findSolutions(1);
  plain.stacking.workDir.clear();
  REQUIRE(other.tryAcquire(lockFile) == FileLock::Outcome::Held);
  REQUIRE(s.setOptions(plain).has_value());
  REQUIRE(s.start().has_value());
  s.wait();
  CHECK(s.state() == SolveState::Finished);
}
