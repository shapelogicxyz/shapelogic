// SPDX-License-Identifier: GPL-3.0-or-later
// The slide search's saved searches: a stopped search saved and carried on,
// the save points, the disk it may take, damaged and older saved files,
// what the log tells of them (cleanup item 21.2 moved these cases here from
// test_sliding.cpp, which has the search's own).
#include "../model/model_test_helpers.h"
#include "../scratch_dir.h"
#include "file_size_limit.h"
#include "slide_levels_test_helpers.h"
#include "sliding_test_helpers.h"
#include "solve_session_test_access.h"

#include "shapelogic/base/memory_share.h"
#include "shapelogic/base/stop.h"
#include "shapelogic/base/thread_pool.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/io/puzzle_file.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/model/solution.h"
#include "shapelogic/solve/assembler.h"
#include "shapelogic/solve/disasm_to_moves.h"
#include "shapelogic/solve/slide_levels.h"
#include "shapelogic/solve/sliding_search.h"
#include "shapelogic/solve/solve_session.h"
#include "shapelogic/solve/solver_type.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace bt;
using namespace bt::sliding;
using namespace bt::test;

// A search stopped by its token is saved in its folder, and a search of the
// same start carries it on to the same path, counting what was searched
// once; without `resume` the saved search is thrown away.
TEST_CASE("sliding: a stopped search is saved and carried on", "[sliding][resume][parallel]") {
  const ScratchDir scratch("slideresume");
  const std::filesystem::path dir = scratch.path() / "work";
  const SlideFixture f = slideFixture(4, 5, kKlotski);

  SlideSearch straight;
  straight.threads = 2;
  const std::optional<Separation> whole = findSlidePath(f.puz, f.pr(), *f.start, straight);
  REQUIRE(whole.has_value());

  // stopped once it has visited a few thousand arrangements
  StopSource stop;
  std::atomic<std::uint64_t> progress{0};
  SlideSearch first;
  first.threads = 2;
  first.workDir = dir.string();
  first.progress = &progress;
  first.stop = stop.token();
  std::atomic<bool> done{false};
  std::thread watcher([&] {
    while (!done.load()) {
      if (progress.load() >= 4000) {
        stop.request(StopKind::Pause);
        return;
      }
      std::this_thread::yield();
    }
  });
  CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, first).has_value());
  done.store(true);
  watcher.join();
  REQUIRE(first.outcome == SlideOutcome::Stopped);
  CHECK(first.saved);
  CHECK(first.visited >= 4000);
  CHECK(first.visited < straight.visited);
  // the progress was told what the search kept (a level cut short is not)
  CHECK(progress.load() == first.visited);
  CHECK(std::filesystem::is_directory(dir));
  CHECK_FALSE(std::filesystem::is_empty(dir));

  SlideSearch second;
  second.threads = 3;
  second.workDir = dir.string();
  // carried on, it adds only what it visits now
  second.progress = &progress;
  const std::optional<Separation> rest = findSlidePath(f.puz, f.pr(), *f.start, second);
  REQUIRE(rest.has_value());
  CHECK(second.resumed);
  CHECK_FALSE(second.saved);
  CHECK(second.outcome == SlideOutcome::Found);
  CHECK(rest->getMoves() == 81);
  CHECK(placesAlong(*rest) == placesAlong(*whole));
  CHECK(second.visited == straight.visited);
  CHECK(progress.load() == straight.visited);
  // the folder goes with the search
  CHECK(std::filesystem::is_empty(dir));

  // saved again, and thrown away by a search that does not resume
  StopSource again;
  again.request(StopKind::Pause);
  SlideSearch third;
  third.workDir = dir.string();
  third.stop = again.token();
  CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, third).has_value());
  CHECK(third.outcome == SlideOutcome::Stopped);
  CHECK(third.saved);
  SlideSearch fresh;
  fresh.workDir = dir.string();
  fresh.resume = false;
  REQUIRE(findSlidePath(f.puz, f.pr(), *f.start, fresh).has_value());
  CHECK_FALSE(fresh.resumed);
  CHECK(fresh.visited == straight.visited);

  // discardSaved throws the problem's saved searches away
  StopSource once;
  once.request(StopKind::Pause);
  SlideSearch other;
  other.workDir = dir.string();
  other.stop = once.token();
  CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, other).has_value());
  CHECK(other.saved);
  CHECK(std::filesystem::exists(dir));
  // savedFolders names its folder (what the app's cache manager links to the puzzle)
  const std::vector<std::string> folders = savedFolders(f.puz, f.pr(), dir.string());
  REQUIRE(folders.size() == 1);
  CHECK(std::filesystem::path(folders[0]).parent_path() == dir);
  CHECK(std::filesystem::path(folders[0]).filename().string().starts_with("slide-"));
  CHECK(savedFolders(f.puz, f.pr(), "").empty());
  discardSaved(f.puz, f.pr(), dir.string());
  CHECK(savedFolders(f.puz, f.pr(), dir.string()).empty());
  CHECK(std::filesystem::is_empty(dir));
  std::filesystem::remove_all(dir);
}

// Cleanup 17.1. A search from the start alone ends when a thread meets the
// goal; the level is then gone through again for its first arrangement
// with a move to the goal, so that every run gives one path. That pass was
// on one thread, told nobody and heard no stop. Now it is on every thread,
// a part of the move (SlidePhase::Goal), and a stop in it leaves the search
// saved with the level to expand again.
TEST_CASE("sliding: the move to the goal is looked for on every thread, told, and can be stopped",
          "[sliding][resume][parallel]") {
  // Eight unit pieces told apart on a 3x3 tray, three of them to bring to
  // the far corners: the goal is no one arrangement, so the search runs
  // from the start alone, and its last level holds thousands.
  std::vector<SlideBlock> blocks;
  for (int i = 0; i < 8; i++) {
    blocks.push_back({1, 1, i % 3, i / 3, -1, -1});
  }
  blocks[0].goalX = 2;
  blocks[0].goalY = 2;
  blocks[2].goalX = 0;
  blocks[2].goalY = 2;
  blocks[6].goalX = 2;
  blocks[6].goalY = 0;
  auto fresh = [&blocks](unsigned int threads) {
    SlideSearch s;
    s.likePiecesApart = true;
    s.threads = threads;
    return std::pair<SlideFixture, SlideSearch>(slideFixture(3, 3, blocks), std::move(s));
  };

  // one path on any number of threads; and whenever the watcher catches
  // the part being told, never more done than there is
  std::vector<int> places;
  unsigned int moves = 0;
  for (const unsigned int threads : {1U, 2U, 7U}) {
    auto [f, search] = fresh(threads);
    SlidePhaseProgress phase;
    search.phase = &phase;
    std::atomic<bool> done{false};
    std::atomic<bool> over{false};
    std::thread watcher([&] {
      while (!done.load()) {
        if (phase.phase.load(std::memory_order_acquire) == SlidePhase::Goal &&
            phase.done.load() > phase.total.load() && phase.total.load() != 0) {
          over.store(true);
        }
      }
    });
    const std::optional<Separation> path = findSlidePath(f.puz, f.pr(), *f.start, search);
    done.store(true);
    watcher.join();
    REQUIRE(path.has_value());
    CHECK(search.outcome == SlideOutcome::Found);
    CHECK(oneMoverPerStep(*path));
    CHECK_FALSE(over.load());
    CHECK(phase.phase.load() == SlidePhase::None);
    if (places.empty()) {
      places = placesAlong(*path);
      moves = path->getMoves();
    } else {
      CHECK(placesAlong(*path) == places);
    }
  }
  CHECK(moves > 10);

  // Stopped while it looks, by a thread that asks when it sees the part
  // told: tried until a stop lands in it (a few milliseconds of the search;
  // alone on the machine the first try does, with twenty of these cases
  // side by side one of the first three did, in 80 runs). The search is
  // then saved, and carried on finds the same path.
  const ScratchDir scratch("slidegoalstop");
  bool landed = false;
  for (int attempt = 0; attempt < 60 && !landed; attempt++) {
    const std::filesystem::path dir = scratch.path() / ("work" + std::to_string(attempt));
    auto [f, search] = fresh(2);
    StopSource stop;
    SlidePhaseProgress phase;
    search.phase = &phase;
    search.workDir = dir.string();
    search.stop = stop.token();
    std::atomic<bool> done{false};
    std::atomic<bool> asked{false};
    std::thread watcher([&] {
      while (!done.load()) {
        if (phase.phase.load(std::memory_order_acquire) == SlidePhase::Goal) {
          stop.request(StopKind::Pause);
          asked.store(true);
          return;
        }
      }
    });
    const std::optional<Separation> path = findSlidePath(f.puz, f.pr(), *f.start, search);
    done.store(true);
    watcher.join();
    if (path.has_value()) {
      // the stop came after the last block was taken, or not at all
      CHECK(placesAlong(*path) == places);
      continue;
    }
    REQUIRE(asked.load());
    REQUIRE(search.outcome == SlideOutcome::Stopped);
    CHECK(search.saved);
    CHECK(phase.phase.load() == SlidePhase::None);
    landed = true;

    // (the same puzzle: a saved search is its problem's)
    SlideSearch second = fresh(3).second;
    second.workDir = dir.string();
    const std::optional<Separation> rest = findSlidePath(f.puz, f.pr(), *f.start, second);
    REQUIRE(rest.has_value());
    CHECK(second.resumed);
    CHECK(second.outcome == SlideOutcome::Found);
    CHECK(placesAlong(*rest) == places);
  }
  CHECK(landed);
}

// A paused search tells what it writes as it saves itself (its levels
// still in memory); an aborted one writes nothing, and its folder goes.
TEST_CASE("sliding: a paused search tells its save, an aborted one saves nothing",
          "[sliding][resume]") {
  const ScratchDir scratch("slidestop");
  const std::filesystem::path dir = scratch.path() / "work";
  const SlideFixture f = slideFixture(4, 5, kKlotski);

  StopSource pause;
  pause.request(StopKind::Pause);
  SlideStopProgress told;
  SlideSearch paused;
  paused.workDir = dir.string();
  paused.stop = pause.token();
  paused.stopProgress = &told;
  CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, paused).has_value());
  REQUIRE(paused.outcome == SlideOutcome::Stopped);
  CHECK(paused.saved);
  CHECK(told.saving.load());
  CHECK(told.totalBytes.load() > 0);
  CHECK(told.doneBytes.load() == told.totalBytes.load());
  CHECK_FALSE(std::filesystem::is_empty(dir));

  // an abort carries nothing on: the saved search is not written again, and goes
  StopSource abort;
  abort.request(StopKind::Abort);
  SlideStopProgress none;
  SlideSearch aborted;
  aborted.workDir = dir.string();
  aborted.stop = abort.token();
  aborted.stopProgress = &none;
  CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, aborted).has_value());
  CHECK(aborted.outcome == SlideOutcome::Stopped);
  CHECK_FALSE(aborted.saved);
  CHECK_FALSE(none.saving.load());
  CHECK(std::filesystem::is_empty(dir));
  std::filesystem::remove_all(dir);
}

// A save point stops the level search between two levels: the level under
// way is finished, nothing is searched twice when the search is carried
// on, and a run that is asked at once still gets a level further, so a
// search saved after every level reaches its end all the same (a stop
// cuts the level short, and a stop a level could never get through).
TEST_CASE("sliding: a save point stops a search between two levels",
          "[sliding][resume][parallel]") {
  const ScratchDir scratch("slidesavepoint");
  const std::filesystem::path dir = scratch.path() / "work";
  const SlideFixture f = slideFixture(4, 5, kKlotski);

  SlideSearch straight;
  straight.threads = 2;
  const std::optional<Separation> whole = findSlidePath(f.puz, f.pr(), *f.start, straight);
  REQUIRE(whole.has_value());

  // asked for before every run begins: each run searches one level
  StopSource point;
  point.request(StopKind::Pause);
  std::atomic<std::uint64_t> progress{0};
  std::uint64_t depth = 0;
  std::uint64_t visited = 0;
  unsigned int runs = 0;
  std::optional<Separation> path;
  for (;;) {
    SlideStopProgress told;
    SlideSearch run;
    run.threads = 2;
    run.workDir = dir.string();
    run.progress = &progress;
    run.savePoint = point.token();
    run.stopProgress = &told;
    path = findSlidePath(f.puz, f.pr(), *f.start, run);
    runs++;
    CHECK(run.resumed == (runs > 1));
    if (path.has_value()) {
      CHECK(run.outcome == SlideOutcome::Found);
      CHECK_FALSE(run.saved);
      visited = run.visited;
      break;
    }
    REQUIRE(run.outcome == SlideOutcome::Stopped);
    REQUIRE(run.saved);
    // it tells its save as a paused search does
    CHECK(told.saving.load());
    // exactly a level further, and what it visited is kept: no level was
    // cut short
    REQUIRE(run.depth == depth + 1);
    CHECK(run.visited > visited);
    CHECK(progress.load() == run.visited);
    depth = run.depth;
    visited = run.visited;
    REQUIRE(runs < 200);
  }
  CHECK(runs > 40);
  CHECK(path->getMoves() == 81);
  CHECK(placesAlong(*path) == placesAlong(*whole));
  // every arrangement was visited once, whatever the stops in between
  CHECK(visited == straight.visited);
  CHECK(progress.load() == straight.visited);
  CHECK(std::filesystem::is_empty(dir));

  // with nowhere to save, a save point lets the search run to its end
  SlideSearch nowhere;
  nowhere.threads = 2;
  nowhere.savePoint = point.token();
  const std::optional<Separation> through = findSlidePath(f.puz, f.pr(), *f.start, nowhere);
  REQUIRE(through.has_value());
  CHECK(nowhere.outcome == SlideOutcome::Found);
  CHECK(nowhere.visited == straight.visited);
  CHECK(placesAlong(*through) == placesAlong(*whole));

  // a stop asked for with it wins: the search stops at once, as paused
  StopSource stop;
  stop.request(StopKind::Pause);
  SlideSearch both;
  both.workDir = dir.string();
  both.savePoint = point.token();
  both.stop = stop.token();
  CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, both).has_value());
  CHECK(both.outcome == SlideOutcome::Stopped);
  CHECK(both.saved);
  CHECK(both.depth == 0);
  discardSaved(f.puz, f.pr(), dir.string());
  std::filesystem::remove_all(dir);
}

// A disk nearly full (here: a reserve larger than any disk) stops the
// search while it can still save itself, and once there is room again a
// search of the same start carries it on to the same path.
TEST_CASE("sliding: a search stops with room to save itself", "[sliding][resume]") {
  const ScratchDir scratch("slidereserve");
  const std::filesystem::path dir = scratch.path() / "work";
  const SlideFixture f = slideFixture(4, 5, kKlotski);

  SlideSearch straight;
  const std::optional<Separation> whole = findSlidePath(f.puz, f.pr(), *f.start, straight);
  REQUIRE(whole.has_value());

  SlideSearch full;
  full.workDir = dir.string();
  full.memoryLevelBytes = 1;
  full.diskReserve = std::uint64_t{1} << 62;
  CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, full).has_value());
  REQUIRE(full.outcome == SlideOutcome::Disk);
  CHECK(full.saved);
  CHECK(full.diskHeldToFree);
  CHECK(full.error.find("The disk is nearly full") != std::string::npos);
  CHECK_FALSE(std::filesystem::is_empty(dir));

  SlideSearch room;
  room.workDir = dir.string();
  room.memoryLevelBytes = 1;
  const std::optional<Separation> rest = findSlidePath(f.puz, f.pr(), *f.start, room);
  REQUIRE(rest.has_value());
  CHECK(room.resumed);
  CHECK(rest->getMoves() == 81);
  CHECK(placesAlong(*rest) == placesAlong(*whole));
  CHECK(room.visited == straight.visited);
  CHECK(std::filesystem::is_empty(dir));
  std::filesystem::remove_all(dir);
}

// A search with the stops of a grid cell as one is saved and carried on as
// any other; one saved with every stop told apart (or before there were
// loose stops) is carried on as it was begun.
TEST_CASE("sliding: a search with the stops of a grid cell as one is carried on",
          "[sliding][resume][parallel]") {
  const ScratchDir scratch("slideloose");
  const std::filesystem::path dir = scratch.path() / "work";
  auto gridSearch = [] {
    SlideSearch s;
    s.nested = true;
    s.grid = 3;
    s.threads = 2;
    return s;
  };
  auto stoppedAt = [&](const GridBoard& g, std::uint64_t depth, bool apart = false) {
    StopSource stop;
    SlideSearch first = gridSearch();
    first.gridStopsApart = apart;
    first.workDir = dir.string();
    first.stop = stop.token();
    first.onDepth = [&](std::uint64_t at) {
      if (at >= depth) {
        stop.request(StopKind::Pause);
      }
    };
    CHECK_FALSE(findSlidePath(g.puz, g.pr(), *g.start, first).has_value());
    CHECK(first.outcome == SlideOutcome::Stopped);
    CHECK(first.saved);
    CHECK(first.looseStops == !apart);
    return first.visited;
  };

  const GridBoard g = cupsAndUnits(true);
  SlideSearch straight = gridSearch();
  const std::optional<Separation> whole = findSlidePath(g.puz, g.pr(), *g.start, straight);
  REQUIRE(whole.has_value());
  const std::uint64_t before = stoppedAt(g, 3);
  CHECK(before < straight.visited);
  SlideSearch second = gridSearch();
  second.workDir = dir.string();
  const std::optional<Separation> rest = findSlidePath(g.puz, g.pr(), *g.start, second);
  REQUIRE(rest.has_value());
  CHECK(second.resumed);
  CHECK(second.looseStops);
  CHECK(rest->getMoves() == whole->getMoves());
  checkGridPath(g, *rest);
  // the level the path is found on ends where the threads happen to be, so
  // the two counts are only near each other; the search below, with no
  // path, counts every arrangement once
  CHECK(second.visited > before);

  // a search with no path: carried on, the arrangements of the whole one
  const GridBoard none = cupsAndUnits(false, 2);
  SlideSearch all = gridSearch();
  CHECK_FALSE(findSlidePath(none.puz, none.pr(), *none.start, all).has_value());
  REQUIRE(all.outcome == SlideOutcome::NoPath);
  CHECK(stoppedAt(none, 6) < all.visited);
  SlideSearch on = gridSearch();
  on.workDir = dir.string();
  CHECK_FALSE(findSlidePath(none.puz, none.pr(), *none.start, on).has_value());
  CHECK(on.resumed);
  CHECK(on.outcome == SlideOutcome::NoPath);
  CHECK(on.visited == all.visited);

  // The same with the stops told apart. (A search stopped between two
  // levels is saved with one still to go into those the one-way moves are
  // looked for in; carried on, it puts the level there.)
  SlideSearch allApart = gridSearch();
  allApart.gridStopsApart = true;
  CHECK_FALSE(findSlidePath(none.puz, none.pr(), *none.start, allApart).has_value());
  CHECK(stoppedAt(none, 6, true) < allApart.visited);
  SlideSearch onApart = gridSearch();
  onApart.gridStopsApart = true;
  onApart.workDir = dir.string();
  CHECK_FALSE(findSlidePath(none.puz, none.pr(), *none.start, onApart).has_value());
  CHECK(onApart.resumed);
  CHECK_FALSE(onApart.looseStops);
  CHECK(onApart.outcome == SlideOutcome::NoPath);
  CHECK(onApart.visited == allApart.visited);

  // saved with the stops told apart, it is carried on so
  StopSource once;
  once.request(StopKind::Pause);
  SlideSearch old = gridSearch();
  old.gridStopsApart = true;
  old.workDir = dir.string();
  old.stop = once.token();
  CHECK_FALSE(findSlidePath(g.puz, g.pr(), *g.start, old).has_value());
  CHECK(old.saved);
  SlideSearch later = gridSearch();
  later.workDir = dir.string();
  const std::optional<Separation> same = findSlidePath(g.puz, g.pr(), *g.start, later);
  REQUIRE(same.has_value());
  CHECK(later.resumed);
  CHECK_FALSE(later.looseStops);
  CHECK(same->getMoves() == whole->getMoves());
  // and the next search of the start has them as one again
  SlideSearch next = gridSearch();
  next.workDir = dir.string();
  REQUIRE(findSlidePath(g.puz, g.pr(), *g.start, next).has_value());
  CHECK_FALSE(next.resumed);
  CHECK(next.looseStops);
  std::filesystem::remove_all(dir);
}

// A grid search saved with every position numbered, as the builds before
// the stops were numbered saved them, is carried on as it was begun. The
// A/B switch itself does not take up a search saved without it.
TEST_CASE("sliding: a grid search saved with every position numbered is carried on",
          "[sliding][resume]") {
  const ScratchDir scratch("slideplain");
  const std::filesystem::path dir = scratch.path() / "work";
  const GridBoard g = cupsAndUnits(true);
  for (const bool nested : {true, false}) {
    INFO("nested " << nested);
    // one thread, so that the level the path is found on counts the same
    // arrangements each time
    auto gridSearch = [&] {
      SlideSearch s;
      s.nested = nested;
      s.grid = 3;
      s.threads = 1;
      return s;
    };
    auto stopped = [&](bool plainKeys) {
      StopSource stop;
      SlideSearch first = gridSearch();
      first.plainKeys = plainKeys;
      first.workDir = dir.string();
      first.stop = stop.token();
      first.onDepth = [&](std::uint64_t at) {
        if (at >= 3) {
          stop.request(StopKind::Pause);
        }
      };
      CHECK_FALSE(findSlidePath(g.puz, g.pr(), *g.start, first).has_value());
      CHECK(first.outcome == SlideOutcome::Stopped);
      CHECK(first.saved);
    };
    SlideSearch straight = gridSearch();
    const std::optional<Separation> whole = findSlidePath(g.puz, g.pr(), *g.start, straight);

    stopped(true);
    SlideSearch later = gridSearch();
    later.workDir = dir.string();
    const std::optional<Separation> rest = findSlidePath(g.puz, g.pr(), *g.start, later);
    CHECK(later.resumed);
    CHECK(later.looseStops == straight.looseStops);
    CHECK(later.outcome == straight.outcome);
    CHECK(later.visited == straight.visited);
    REQUIRE(rest.has_value() == whole.has_value());
    if (rest) {
      CHECK(placesAlong(*rest) == placesAlong(*whole));
    }

    // saved with the stops numbered: the switch begins its own search
    stopped(false);
    SlideSearch other = gridSearch();
    other.plainKeys = true;
    other.workDir = dir.string();
    (void)findSlidePath(g.puz, g.pr(), *g.start, other);
    CHECK_FALSE(other.resumed);
    CHECK(other.visited == straight.visited);
    SlideSearch own = gridSearch();
    own.workDir = dir.string();
    (void)findSlidePath(g.puz, g.pr(), *g.start, own);
    CHECK(own.resumed);
    CHECK(own.visited == straight.visited);
  }

  // without a grid every position is a stop: the keys, and so the saved
  // searches, are the same with the switch and without
  const SlideFixture f = eightPuzzle();
  StopSource stop;
  SlideSearch first;
  first.plainKeys = true;
  first.workDir = dir.string();
  first.stop = stop.token();
  first.onDepth = [&](std::uint64_t at) {
    if (at >= 4) {
      stop.request(StopKind::Pause);
    }
  };
  CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, first).has_value());
  CHECK(first.saved);
  SlideSearch second;
  second.workDir = dir.string();
  CHECK(findSlidePath(f.puz, f.pr(), *f.start, second).has_value());
  CHECK(second.resumed);
  std::filesystem::remove_all(dir);
}

// A search from both ends with nested slides is saved with what each end
// has been through and carried on to the fewest moves, stopped wherever.
TEST_CASE("sliding: a search from both ends with nested slides is carried on",
          "[sliding][resume][parallel]") {
  const ScratchDir scratch("slideends");
  const std::filesystem::path dir = scratch.path() / "work";
  auto gridSearch = [] {
    SlideSearch s = nestedSearch(3);
    s.threads = 2;
    return s;
  };
  // stopped `depth` moves deep (both ends together) and saved
  auto stoppedAt = [&](const GridBoard& g, std::uint64_t depth, SlideSearch first) {
    StopSource stop;
    first.workDir = dir.string();
    first.stop = stop.token();
    first.onDepth = [&](std::uint64_t at) {
      if (at >= depth) {
        stop.request(StopKind::Pause);
      }
    };
    CHECK_FALSE(findSlidePath(g.puz, g.pr(), *g.start, first).has_value());
    CHECK(first.outcome == SlideOutcome::Stopped);
    CHECK(first.saved);
    return first;
  };

  const GridBoard g = settledBoard(37);
  bool fromGoal = false;
  SlideSearch straight = gridSearch();
  const std::optional<Separation> whole = searched(g, straight, fromGoal);
  REQUIRE(whole.has_value());
  REQUIRE(fromGoal);
  const unsigned int moves = whole->getMoves();
  REQUIRE(moves >= 14);
  for (const std::uint64_t depth : {1U, 2U, 5U, 8U, moves - 3U, moves - 1U}) {
    INFO("stopped " << depth << " moves deep");
    const SlideSearch first = stoppedAt(g, depth, gridSearch());
    CHECK_FALSE(first.looseStops);
    SlideSearch second = gridSearch();
    second.threads = 3;
    second.workDir = dir.string();
    const std::optional<Separation> rest = searched(g, second, fromGoal);
    REQUIRE(rest.has_value());
    CHECK(second.resumed);
    CHECK_FALSE(second.looseStops);
    CHECK(rest->getMoves() == moves);
    checkGridPath(g, *rest);
    if (depth + 6 < moves) {
      CHECK(fromGoal);
    }
    CHECK(std::filesystem::is_empty(dir));
  }

  // no path: carried on, the arrangements of the whole search
  const GridBoard none = swappedBlocks();
  SlideSearch all = gridSearch();
  CHECK_FALSE(searched(none, all, fromGoal).has_value());
  REQUIRE(all.outcome == SlideOutcome::NoPath);
  CHECK(fromGoal);
  for (const std::uint64_t depth : {2U, 3U, 6U}) {
    INFO("stopped " << depth << " moves deep");
    CHECK(stoppedAt(none, depth, gridSearch()).visited < all.visited);
    SlideSearch on = gridSearch();
    on.workDir = dir.string();
    CHECK_FALSE(findSlidePath(none.puz, none.pr(), *none.start, on).has_value());
    CHECK(on.resumed);
    CHECK(on.outcome == SlideOutcome::NoPath);
    CHECK(on.visited == all.visited);
  }

  // saved from both ends, it is carried on so by the A/B switch too
  (void)stoppedAt(g, 3, gridSearch());
  SlideSearch switched = gridSearch();
  switched.oneEnd = true;
  switched.workDir = dir.string();
  const std::optional<Separation> same = searched(g, switched, fromGoal);
  REQUIRE(same.has_value());
  CHECK(switched.resumed);
  CHECK(fromGoal);
  CHECK(same->getMoves() == moves);

  // Saved from the start alone with the stops of a cell as one (as the
  // builds before the two ends saved it), it is carried on as it was begun:
  // those arrangements have no moves backwards.
  SlideSearch alone = gridSearch();
  alone.oneEnd = true;
  CHECK(stoppedAt(g, 3, alone).looseStops);
  SlideSearch later = gridSearch();
  later.workDir = dir.string();
  const std::optional<Separation> loose = searched(g, later, fromGoal);
  REQUIRE(loose.has_value());
  CHECK(later.resumed);
  CHECK(later.looseStops);
  CHECK_FALSE(fromGoal);
  CHECK(loose->getMoves() == moves);
  checkGridPath(g, *loose);

  // Saved from the start alone with the stops apart, it takes up the goal
  // end as it is carried on.
  alone.gridStopsApart = true;
  CHECK_FALSE(stoppedAt(g, 3, alone).looseStops);
  SlideSearch joined = gridSearch();
  joined.workDir = dir.string();
  const std::optional<Separation> apart = searched(g, joined, fromGoal);
  REQUIRE(apart.has_value());
  CHECK(joined.resumed);
  CHECK_FALSE(joined.looseStops);
  CHECK(fromGoal);
  CHECK(apart->getMoves() == moves);
  checkGridPath(g, *apart);

  // and a search with nothing saved goes from both ends again
  SlideSearch next = gridSearch();
  next.workDir = dir.string();
  REQUIRE(searched(g, next, fromGoal).has_value());
  CHECK_FALSE(next.resumed);
  CHECK(fromGoal);
  std::filesystem::remove_all(dir);
}

// The Solver's run of a long search with nested slides from both ends: its
// levels on disk, saved now and then as it goes (autosave), at save points
// (the crash journal's) and when paused in the middle of a level, stopped
// when it fills its disk budget and carried on with a larger one, again
// and again. Every run carries the saved search on, never deeper than the
// path, and the path is the one a search with no stops finds.
TEST_CASE("sliding: a search from both ends with nested slides is carried on past its disk budget",
          "[sliding][resume][parallel]") {
  const ScratchDir scratch("slidebudget");
  const std::filesystem::path dir = scratch.path() / "work";
  const GridBoard g = settledBoard(37);

  // levels in memory up to `memory` bytes; autosaved after every level or,
  // with `autosave` off, at the stops alone
  auto carriedOn = [&](std::uint64_t memory, bool autosave) {
    INFO("memory " << memory << ", autosave " << autosave);
    auto runSearch = [&] {
      SlideSearch s = nestedSearch(3);
      s.threads = 2;
      s.workDir = dir.string();
      s.memoryLevelBytes = memory;
      s.autosave = autosave;
      s.autosaveFloorSeconds = 0;
      return s;
    };
    bool fromGoal = false;
    SlideSearch straight = runSearch();
    const std::optional<Separation> whole = searched(g, straight, fromGoal);
    REQUIRE(whole.has_value());
    REQUIRE(fromGoal);
    const unsigned int moves = whole->getMoves();
    REQUIRE(moves >= 14);
    CHECK(std::filesystem::is_empty(dir));
    if (straight.diskBytes == 0) {
      return;  // all of it in memory: no disk budget to reach
    }

    const std::uint64_t step = std::max<std::uint64_t>(straight.diskBytes / 16, 1);
    std::uint64_t budget = step;
    std::uint64_t depth = 0;
    unsigned int diskStops = 0;
    bool pointNext = false;
    bool sawPicking = false;
    std::optional<Separation> path;
    for (unsigned int runs = 1;; runs++) {
      INFO("run " << runs << ", disk budget " << budget << " of " << straight.diskBytes);
      REQUIRE(runs < 200);
      SlideSearch s = runSearch();
      s.diskBudget = budget;
      std::vector<LogLine> told;
      // taking the saved search up is told while it lasts (pickingUpSince): on
      // from its state read (still on when the visited sets are back), off
      // before the search says it was carried on
      std::atomic<std::int64_t> picking{0};
      s.pickingUpSince = &picking;
      s.onLog = [&](LogLevel level, const std::string& text) {
        if (text.starts_with("The visited sets of the saved search were read back in ")) {
          // since a moment ago, by the clock its time is told on
          CHECK(picking.load() != 0);
          CHECK(picking.load() <= slideClockMs());
          sawPicking = true;
        }
        if (text.starts_with("Carried on the saved search in ")) {
          CHECK(picking.load() == 0);
        }
        // a debug log's lines aside (the debug log's own test has those),
        // and the key the search runs in, told before each run
        if (level != LogLevel::Debug && !text.starts_with("The slide search runs ")) {
          told.push_back(LogLine{level, text});
        }
      };
      // after a disk stop, the run carried on with room for more stops at
      // a save point (the crash journal's) before the budget is reached
      StopSource point;
      if (pointNext) {
        point.request(StopKind::Pause);
        s.savePoint = point.token();
      }
      pointNext = false;
      // every third run paused in the middle of a level (Pause, File ▸ Save)
      StopSource pause;
      std::atomic<std::uint64_t> progress{0};
      std::atomic<bool> done{false};
      std::thread watcher;
      if (runs % 3 == 0) {
        s.stop = pause.token();
        s.progress = &progress;
        const std::uint64_t after = std::max<std::uint64_t>(straight.visited / 40, 1);
        watcher = std::thread([&, after] {
          while (!done.load()) {
            if (progress.load() >= after) {
              pause.request(StopKind::Pause);
              return;
            }
            std::this_thread::yield();
          }
        });
      }
      path = searched(g, s, fromGoal);
      done.store(true);
      if (watcher.joinable()) {
        watcher.join();
      }
      if (runs > 1) {
        CHECK(s.resumed);
        // the log tells how deep each end was carried on from
        REQUIRE_FALSE(told.empty());
        CHECK(told.front().level == LogLevel::Info);
        CHECK(told.front().text.starts_with("Carried on the saved search in "));
        // and what taking it up cost
        CHECK(told.front().text.find(" Taken up in ") != std::string::npos);
        CHECK(told.front().text.ends_with(" s for the visited sets."));
      }
      CHECK(picking.load() == 0);
      // both ends together never deeper than the path
      CHECK(s.depth <= moves);
      CHECK(s.depth >= depth);
      depth = s.depth;
      if (path.has_value()) {
        CHECK(s.outcome == SlideOutcome::Found);
        CHECK(s.visited == straight.visited);
        break;
      }
      REQUIRE(s.saved);
      if (s.outcome == SlideOutcome::Disk) {
        CHECK(std::any_of(told.begin(), told.end(), [](const LogLine& l) {
          return l.level == LogLevel::Warning && l.text.find("The search is saved") != std::string::npos;
        }));
        diskStops++;
        budget += 2 * step;
        pointNext = true;
      } else {
        REQUIRE(s.outcome == SlideOutcome::Stopped);
      }
    }
    CHECK(diskStops >= 3);
    CHECK(sawPicking);
    CHECK(path->getMoves() == moves);
    checkGridPath(g, *path);
    CHECK(std::filesystem::is_empty(dir));
  };
  for (const std::uint64_t memory : {1ULL, 512ULL}) {
    carriedOn(memory, true);
    carriedOn(memory, false);
  }
  std::filesystem::remove_all(dir);
}

// A saved search that cannot be carried on (a file of one of its levels
// damaged or gone) is kept as it is, every file of it, and nothing is
// searched: a saved search is days of work, and what could not be read now
// may be read later (decision 0041). With the file back the search is
// carried on; a search that does not carry on throws the saved one away and
// finds the path a search with no stops finds.
TEST_CASE("sliding: a saved search with a damaged level is kept, and nothing is searched",
          "[sliding][resume][parallel]") {
  const ScratchDir scratch("slidedamaged");
  const std::filesystem::path dir = scratch.path() / "work";
  const ScratchDir spareScratch("slidedamaged-spare");
  const std::filesystem::path spareDir = spareScratch.path() / "work";
  std::filesystem::remove_all(dir);
  std::filesystem::remove_all(spareDir);
  std::filesystem::create_directories(spareDir);
  const std::filesystem::path spare = spareDir / "spare.keys";
  auto runSearch = [&] {
    SlideSearch s = nestedSearch(3);
    s.threads = 2;
    s.workDir = dir.string();
    s.memoryLevelBytes = 1;
    return s;
  };
  const GridBoard g = settledBoard(37);
  bool fromGoal = false;
  SlideSearch straight = runSearch();
  const std::optional<Separation> whole = searched(g, straight, fromGoal);
  REQUIRE(whole.has_value());
  REQUIRE(fromGoal);
  const unsigned int moves = whole->getMoves();
  REQUIRE(moves >= 14);

  // stopped 10 moves deep (both ends together), saved
  auto stopped = [&] {
    StopSource stop;
    SlideSearch first = runSearch();
    first.stop = stop.token();
    first.onDepth = [&](std::uint64_t at) {
      if (at >= 10) {
        stop.request(StopKind::Pause);
      }
    };
    CHECK_FALSE(findSlidePath(g.puz, g.pr(), *g.start, first).has_value());
    REQUIRE(first.outcome == SlideOutcome::Stopped);
    REQUIRE(first.saved);
  };
  auto levelFiles = [&] {
    std::vector<std::filesystem::path> files;
    for (const auto& folder : std::filesystem::directory_iterator(dir)) {
      for (const auto& e : std::filesystem::directory_iterator(folder.path())) {
        if (e.path().extension() == ".keys") {
          files.push_back(e.path());
        }
      }
    }
    std::sort(files.begin(), files.end());
    return files;
  };
  // every file of the saved search, with its size
  auto listing = [&] {
    std::map<std::string, std::uintmax_t> files;
    for (const auto& folder : std::filesystem::directory_iterator(dir)) {
      for (const auto& e : std::filesystem::directory_iterator(folder.path())) {
        files[e.path().filename().string()] = e.file_size();
      }
    }
    return files;
  };

  stopped();
  const std::size_t count = levelFiles().size();
  REQUIRE(count >= 10);
  // every other file: the ends' newest levels, their kept levels and the
  // visited sets are among them
  unsigned int tried = 0;
  for (std::size_t i = 0; i < count; i += 2, tried++) {
    stopped();
    const std::vector<std::filesystem::path> files = levelFiles();
    REQUIRE(files.size() == count);
    const std::string name = files[i].filename().string();
    const bool gone = tried % 2 == 1;
    INFO((gone ? "gone: " : "cut short: ") << name);
    std::filesystem::copy_file(files[i], spare, std::filesystem::copy_options::overwrite_existing);
    if (gone) {
      std::filesystem::remove(files[i]);
    } else {
      std::filesystem::resize_file(files[i], std::filesystem::file_size(files[i]) / 2);
    }
    const std::map<std::string, std::uintmax_t> before = listing();
    // twice: the second try finds what the first found, and leaves it too
    for (int again = 0; again < 2; again++) {
      SlideSearch on = runSearch();
      std::vector<LogLine> told;
      on.onLog = [&told](LogLevel level, const std::string& text) {
        // a debug log's lines aside (what state.bin names comes first), and
        // the key the search runs in, told before it
        if (level != LogLevel::Debug && !text.starts_with("The slide search runs ")) {
          told.push_back(LogLine{level, text});
        }
      };
      const std::optional<Separation> path = searched(g, on, fromGoal);
      CHECK_FALSE(path.has_value());
      CHECK(on.outcome == SlideOutcome::Disk);
      CHECK(on.savedUnread);
      CHECK_FALSE(on.saved);
      CHECK_FALSE(on.resumed);
      CHECK_FALSE(on.diskHeldToFree);
      // the error tells which file and what is wrong with it, and that
      // the saved search is still there
      CHECK(on.error.find("could not be carried on") != std::string::npos);
      CHECK(on.error.find(name) != std::string::npos);
      CHECK(on.error.find(gone ? "is missing" : "is not a whole level file") !=
            std::string::npos);
      CHECK(on.error.find("It is kept as it is.") != std::string::npos);
      REQUIRE_FALSE(told.empty());
      CHECK(told.front().level == LogLevel::Error);
      CHECK(told.front().text == on.error);
      CHECK(listing() == before);
    }
    if (tried % 4 < 2) {
      // the file is back (a drive that had dropped out): carried on
      std::filesystem::copy_file(spare, files[i],
                                 std::filesystem::copy_options::overwrite_existing);
      SlideSearch back = runSearch();
      const std::optional<Separation> path = searched(g, back, fromGoal);
      REQUIRE(path.has_value());
      CHECK(back.resumed);
      CHECK_FALSE(back.savedUnread);
      CHECK(path->getMoves() == moves);
      checkGridPath(g, *path);
    } else {
      // a search that does not carry on throws the saved one away
      SlideSearch fresh = runSearch();
      fresh.resume = false;
      const std::optional<Separation> path = searched(g, fresh, fromGoal);
      REQUIRE(path.has_value());
      CHECK_FALSE(fresh.resumed);
      CHECK(path->getMoves() == moves);
      checkGridPath(g, *path);
      CHECK(fresh.visited == straight.visited);
    }
    CHECK(std::filesystem::is_empty(dir));
  }
  CHECK(tried >= 5);

  // A folder with files and no state is no saved search (a search that
  // was killed before it ever saved): cleared, and searched from the start.
  {
    stopped();
    const std::filesystem::path folder = levelFiles().front().parent_path();
    std::filesystem::remove(folder / "state.bin");
    SlideSearch on = runSearch();
    std::vector<LogLine> told;
    on.onLog = [&told](LogLevel level, const std::string& text) {
      told.push_back(LogLine{level, text});
    };
    const std::optional<Separation> path = searched(g, on, fromGoal);
    REQUIRE(path.has_value());
    CHECK_FALSE(on.resumed);
    CHECK_FALSE(on.savedUnread);
    CHECK(path->getMoves() == moves);
    CHECK(on.visited == straight.visited);
    CHECK(std::any_of(told.begin(), told.end(), [](const LogLine& l) {
      return l.level == LogLevel::Info &&
             l.text.find("holds no saved search") != std::string::npos;
    }));
    CHECK(std::filesystem::is_empty(dir));
  }
  // A state that is there and cannot be read is a saved search still.
  {
    stopped();
    const std::filesystem::path folder = levelFiles().front().parent_path();
    std::filesystem::resize_file(folder / "state.bin", 5);
    const std::map<std::string, std::uintmax_t> before = listing();
    SlideSearch on = runSearch();
    CHECK_FALSE(searched(g, on, fromGoal).has_value());
    CHECK(on.outcome == SlideOutcome::Disk);
    CHECK(on.savedUnread);
    CHECK(on.error.find("state.bin") != std::string::npos);
    CHECK(listing() == before);
    SlideSearch fresh = runSearch();
    fresh.resume = false;
    const std::optional<Separation> path = searched(g, fresh, fromGoal);
    REQUIRE(path.has_value());
    CHECK(path->getMoves() == moves);
    CHECK(std::filesystem::is_empty(dir));
  }
  std::filesystem::remove_all(dir);
  std::filesystem::remove_all(spareDir);
}

// A debug log (the app's Settings ▸ General): a line for each level done,
// how deep both ends are and the disk; each save, with the files the state
// names; the saved search read back, what its state.bin names.
TEST_CASE("sliding: the debug log tells each level, each save and the search read back",
          "[sliding][resume][parallel]") {
  const ScratchDir scratch("slidedebuglog");
  const std::filesystem::path dir = scratch.path() / "work";
  auto runSearch = [&](std::vector<LogLine>& told) {
    SlideSearch s = nestedSearch(3);
    s.threads = 2;
    s.workDir = dir.string();
    s.memoryLevelBytes = 1;
    s.onLog = [&told](LogLevel level, const std::string& text) {
      told.push_back(LogLine{level, text});
    };
    return s;
  };
  auto debugWith = [](const std::vector<LogLine>& told, const std::string& text) {
    return static_cast<std::size_t>(std::count_if(told.begin(), told.end(), [&](const LogLine& l) {
      return l.level == LogLevel::Debug && l.text.find(text) != std::string::npos;
    }));
  };
  const GridBoard g = settledBoard(37);
  std::vector<LogLine> firstTold;
  StopSource stop;
  SlideSearch first = runSearch(firstTold);
  first.stop = stop.token();
  std::uint64_t levels = 0;
  first.onDepth = [&](std::uint64_t at) {
    levels = at;
    if (at >= 10) {
      stop.request(StopKind::Pause);
    }
  };
  CHECK_FALSE(findSlidePath(g.puz, g.pr(), *g.start, first).has_value());
  REQUIRE(first.saved);
  // a line for each level done: both ends' depths, the disk against its budget
  CHECK(debugWith(firstTold, " moves deep (the ") == levels);
  CHECK(debugWith(firstTold, "in all): the level has ") == levels);
  CHECK(debugWith(firstTold, "The disk the search may take in ") == 1);
  // the save, with the files its state names
  CHECK(debugWith(firstTold, "Saved the search's state in ") >= 1);
  CHECK(debugWith(firstTold, ".keys") >= 1);

  std::vector<LogLine> told;
  bool fromGoal = false;
  SlideSearch on = runSearch(told);
  CHECK(searched(g, on, fromGoal).has_value());
  CHECK(on.resumed);
  REQUIRE_FALSE(told.empty());
  // the key the search runs in; then what state.bin names, before the
  // search is carried on
  REQUIRE(told.size() >= 2);
  CHECK(told[0].level == LogLevel::Info);
  CHECK(told[0].text.starts_with("The slide search runs in 64-bit keys (its arrangements take "));
  CHECK(told[0].text.find("the narrowest key") != std::string::npos);
  CHECK(told[1].level == LogLevel::Debug);
  CHECK(told[1].text.find("Reading the saved search in ") != std::string::npos);
  CHECK(told[1].text.find("the start end ") != std::string::npos);
  CHECK(told[1].text.find(" arrangements visited, interval ") != std::string::npos);
  CHECK(debugWith(told, " moves deep (the ") >= 1);
  std::filesystem::remove_all(dir);
}

// What the Solver asks before Continue (decision 0033): a search saved with
// the stops of a cell as one goes on from the start alone, and where a new
// search would go from both ends that is worth saying.
TEST_CASE("sliding: a search saved from the start alone is told where both ends would do",
          "[sliding][resume][parallel]") {
  const ScratchDir scratch("slidealone");
  const std::filesystem::path dir = scratch.path() / "work";
  auto gridSearch = [] {
    SlideSearch s = nestedSearch(3);
    s.threads = 2;
    return s;
  };
  // stopped `depth` moves deep and saved
  auto stopped = [&](const GridBoard& b, SlideSearch first, std::uint64_t depth) {
    StopSource stop;
    first.workDir = dir.string();
    first.stop = stop.token();
    first.onDepth = [&](std::uint64_t at) {
      if (at >= depth) {
        stop.request(StopKind::Pause);
      }
    };
    CHECK_FALSE(findSlidePath(b.puz, b.pr(), *b.start, first).has_value());
    REQUIRE(first.outcome == SlideOutcome::Stopped);
    REQUIRE(first.saved);
    return first;
  };
  auto alone = [&](const GridBoard& b, SlideSearch s) {
    s.workDir = dir.string();
    return savedFromStartAlone(b.puz, b.pr(), *b.start, s);
  };
  auto discard = [&](const GridBoard& b) {
    discardSaved(b.puz, b.pr(), dir.string());
    CHECK(std::filesystem::is_empty(dir));
  };

  GridBoard g = settledBoard(37);
  // nothing saved; saved from both ends, as this build saves it
  CHECK_FALSE(alone(g, gridSearch()));
  CHECK_FALSE(stopped(g, gridSearch(), 3).looseStops);
  CHECK_FALSE(alone(g, gridSearch()));
  discard(g);

  // Saved from the start alone with the stops of a cell as one, as the
  // builds before the two ends saved it: with codes for the stops only
  // (decision 0032's) and for every position (v1.8.1's).
  for (const bool plain : {false, true}) {
    INFO("plain keys " << plain);
    SlideSearch first = gridSearch();
    first.oneEnd = true;
    first.plainKeys = plain;
    CHECK(stopped(g, first, 3).looseStops);
    CHECK(alone(g, gridSearch()));
    // not for a search that would go from the start alone anyway, or that
    // is not this saved one
    SlideSearch other = gridSearch();
    other.oneEnd = true;
    CHECK_FALSE(alone(g, other));
    other = gridSearch();
    other.gridStopsApart = true;
    CHECK_FALSE(alone(g, other));
    other = gridSearch();
    other.partialNested = true;
    CHECK_FALSE(alone(g, other));
    other = gridSearch();
    other.nested = false;
    CHECK_FALSE(alone(g, other));
    other = gridSearch();
    other.grid = 0;
    CHECK_FALSE(alone(g, other));
    other = gridSearch();
    other.resume = false;
    CHECK_FALSE(alone(g, other));
    other = gridSearch();
    other.workDir = (dir / "elsewhere").string();
    CHECK_FALSE(savedFromStartAlone(g.puz, g.pr(), *g.start, other));

    // the paused solve's question: of the starts it keeps for its next run
    SlidingOptions options;
    options.nested = true;
    options.grid = 3;
    options.workDir = dir.string();
    Problem& pr = g.puz.getProblem(0);
    CHECK_FALSE(pendingFromStartAlone(g.puz, pr, options));
    pr.addPending(PendingAssembly{*g.start, false, 0, 0});
    CHECK(pendingFromStartAlone(g.puz, pr, options));
    options.oneEnd = true;
    CHECK_FALSE(pendingFromStartAlone(g.puz, pr, options));
    (void)pr.takePending();

    // asking changed nothing: it is carried on as it was begun, to the
    // moves of the search from both ends
    bool fromGoal = false;
    SlideSearch whole = gridSearch();
    const std::optional<Separation> both = searched(g, whole, fromGoal);
    REQUIRE(both.has_value());
    SlideSearch on = gridSearch();
    on.workDir = dir.string();
    const std::optional<Separation> rest = searched(g, on, fromGoal);
    REQUIRE(rest.has_value());
    CHECK(on.resumed);
    CHECK(on.looseStops);
    CHECK_FALSE(fromGoal);
    CHECK(rest->getMoves() == both->getMoves());
    CHECK(std::filesystem::is_empty(dir));
    CHECK_FALSE(alone(g, gridSearch()));
  }

  // saved from the start alone with the stops apart: carried on, it takes
  // up the goal end
  SlideSearch apart = gridSearch();
  apart.oneEnd = true;
  apart.gridStopsApart = true;
  CHECK_FALSE(stopped(g, apart, 3).looseStops);
  CHECK_FALSE(alone(g, gridSearch()));
  discard(g);

  // a piece that may end anywhere: searched from the start alone, saved or
  // not
  const GridBoard many = cupsAndUnits(true);
  CHECK(stopped(many, gridSearch(), 1).looseStops);
  CHECK_FALSE(alone(many, gridSearch()));
  discard(many);
  std::filesystem::remove_all(dir);
}

// A search with packed keys, or in a key wider than 128 bits, is saved and
// carried on as any other; a search with other keys does not take it up.
TEST_CASE("sliding: a stopped search with packed or wide keys is saved and carried on",
          "[sliding][resume][parallel]") {
  const SlideFixture f = slideFixture(4, 5, kKlotski);
  const std::pair<SlidePacking, unsigned int> forms[] = {
      {SlidePacking::Always, 0},
      {SlidePacking::Never, 256},
      {SlidePacking::Auto, 192},
  };
  for (const auto& [packing, bits] : forms) {
    INFO("bits " << bits);
    const ScratchDir scratch("slidekeys");
    const std::filesystem::path dir = scratch.path() / "work";
    SlideSearch form;
    form.packing = packing;
    form.minKeyBits = bits;
    form.threads = 2;

    SlideSearch straight = form;
    const std::optional<Separation> whole = findSlidePath(f.puz, f.pr(), *f.start, straight);
    REQUIRE(whole.has_value());
    CHECK(straight.packedKeys == (packing != SlidePacking::Never));

    // stopped once it has visited a few thousand arrangements
    StopSource stop;
    std::atomic<std::uint64_t> progress{0};
    SlideSearch first = form;
    first.workDir = dir.string();
    first.progress = &progress;
    first.stop = stop.token();
    std::atomic<bool> done{false};
    std::thread watcher([&] {
      while (!done.load()) {
        if (progress.load() >= 4000) {
          stop.request(StopKind::Pause);
          return;
        }
        std::this_thread::yield();
      }
    });
    CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, first).has_value());
    done.store(true);
    watcher.join();
    REQUIRE(first.outcome == SlideOutcome::Stopped);
    CHECK(first.saved);
    CHECK(first.visited < straight.visited);

    // a search with a code for each piece in the narrow key is another search
    SlideSearch other;
    other.workDir = dir.string();
    REQUIRE(findSlidePath(f.puz, f.pr(), *f.start, other).has_value());
    CHECK_FALSE(other.resumed);
    CHECK(savedFolders(f.puz, f.pr(), dir.string()).size() == 1);

    SlideSearch second = form;
    second.threads = 3;
    second.workDir = dir.string();
    const std::optional<Separation> rest = findSlidePath(f.puz, f.pr(), *f.start, second);
    REQUIRE(rest.has_value());
    CHECK(second.resumed);
    CHECK(second.outcome == SlideOutcome::Found);
    CHECK(rest->getMoves() == 81);
    CHECK(placesAlong(*rest) == placesAlong(*whole));
    CHECK(second.visited == straight.visited);
    CHECK(second.keyWidth == straight.keyWidth);
    CHECK(second.packedKeys == straight.packedKeys);
    CHECK(std::filesystem::is_empty(dir));
    std::filesystem::remove_all(dir);
  }
}

#ifdef __SIZEOF_INT128__
namespace {

// A key set's file of this version written again as the versions up to 1.9
// wrote theirs.
template <class Key>
void asOlderVersion(const std::filesystem::path& p) {
  std::vector<Key> keys;
  {
    levels::KeySet<Key> set;
    REQUIRE(set.open(p));
    REQUIRE(set.load(keys));
    set.keepFile();
  }
  writeOlderSet(p, keys);
}

// Every level file of the saved searches in `dir` written again as the
// versions up to 1.9 wrote theirs (the search ran in keys of `keyWidth`
// bits); how many there were.
std::size_t asOlderVersion(const std::filesystem::path& dir, unsigned int keyWidth) {
  std::size_t files = 0;
  for (const auto& folder : std::filesystem::directory_iterator(dir)) {
    for (const auto& e : std::filesystem::directory_iterator(folder.path())) {
      if (e.path().extension() != ".keys") {
        continue;
      }
      if (keyWidth == 64) {
        asOlderVersion<std::uint64_t>(e.path());
      } else {
        REQUIRE(keyWidth == 128);
        asOlderVersion<Wide>(e.path());
      }
      files++;
    }
  }
  return files;
}

// The endings of the level files of the saved searches in `dir`.
std::map<std::string, std::string> levelEndings(const std::filesystem::path& dir) {
  std::map<std::string, std::string> endings;
  for (const auto& folder : std::filesystem::directory_iterator(dir)) {
    for (const auto& e : std::filesystem::directory_iterator(folder.path())) {
      if (e.path().extension() == ".keys") {
        endings[e.path().filename().string()] = fileEnding(e.path());
      }
    }
  }
  return endings;
}

}  // namespace

// A search saved by a version up to 1.9 (its level files not compressed)
// is carried on by this one: every file of it is written again as this
// version writes its files, under its own name, and the search goes on to
// the path a search with no stops finds. With nested slides (the visited
// sets) and without (the levels kept), from both ends and from one.
TEST_CASE("sliding: a search saved by an earlier version is carried on, its files compressed",
          "[sliding][resume][parallel][slow]") {
  const ScratchDir scratch("slideolder");
  const std::filesystem::path dir = scratch.path() / "work";
  const GridBoard g = settledBoard(37);
  const SlideFixture f = slideFixture(4, 5, kKlotski);
  struct Kind {
    const char* name;
    std::function<SlideSearch()> make;
    std::function<std::optional<Separation>(SlideSearch&)> run;
    std::uint64_t stopAt;
  };
  const std::vector<Kind> kinds = {
      {"nested slides, both ends",
       [&] {
         SlideSearch s = nestedSearch(3);
         s.threads = 2;
         s.workDir = dir.string();
         s.memoryLevelBytes = 1;
         return s;
       },
       [&](SlideSearch& s) { return findSlidePath(g.puz, g.pr(), *g.start, s); }, 10},
      {"the levels kept, one end",
       [&] {
         SlideSearch s;
         s.oneEnd = true;
         s.threads = 3;
         s.workDir = dir.string();
         s.memoryLevelBytes = 1;
         return s;
       },
       [&](SlideSearch& s) { return findSlidePath(f.puz, f.pr(), *f.start, s); }, 40},
      {"the levels kept, both ends",
       [&] {
         SlideSearch s;
         s.threads = 3;
         s.workDir = dir.string();
         s.memoryLevelBytes = 1;
         return s;
       },
       [&](SlideSearch& s) { return findSlidePath(f.puz, f.pr(), *f.start, s); }, 40},
  };
  const std::string older(levels::kOlderSetMagic, sizeof(levels::kOlderSetMagic));
  const std::string current(levels::kSetMagic, sizeof(levels::kSetMagic));
  for (const Kind& kind : kinds) {
    INFO(kind.name);
    SlideSearch straight = kind.make();
    const std::optional<Separation> whole = kind.run(straight);
    REQUIRE(whole.has_value());
    REQUIRE(whole->getMoves() > kind.stopAt);
    REQUIRE(std::filesystem::is_empty(dir));
    // stopped part of the way, saved; then its files as an earlier
    // version wrote them
    auto savedByOlder = [&] {
      StopSource stop;
      SlideSearch first = kind.make();
      first.stop = stop.token();
      first.onDepth = [&](std::uint64_t at) {
        if (at >= kind.stopAt) {
          stop.request(StopKind::Pause);
        }
      };
      CHECK_FALSE(kind.run(first).has_value());
      REQUIRE(first.outcome == SlideOutcome::Stopped);
      REQUIRE(first.saved);
      const std::size_t files = asOlderVersion(dir, first.keyWidth);
      for (const auto& [name, ending] : levelEndings(dir)) {
        REQUIRE(ending == older);
      }
      return files;
    };
    // what the search stopped there and carried on comes to, its files as
    // this version wrote them
    std::uint64_t visited = 0;
    {
      StopSource stop;
      SlideSearch first = kind.make();
      first.stop = stop.token();
      first.onDepth = [&](std::uint64_t at) {
        if (at >= kind.stopAt) {
          stop.request(StopKind::Pause);
        }
      };
      CHECK_FALSE(kind.run(first).has_value());
      REQUIRE(first.saved);
      SlideSearch on = kind.make();
      const std::optional<Separation> path = kind.run(on);
      REQUIRE(path.has_value());
      REQUIRE(on.resumed);
      CHECK(on.convertedFiles == 0);
      visited = on.visited;
      REQUIRE(std::filesystem::is_empty(dir));
    }

    // carried on to the end
    {
      const std::size_t files = savedByOlder();
      REQUIRE(files >= 3);
      SlideSearch on = kind.make();
      std::vector<LogLine> told;
      on.onLog = [&told](LogLevel level, const std::string& text) {
        told.push_back(LogLine{level, text});
      };
      const std::optional<Separation> path = kind.run(on);
      REQUIRE(path.has_value());
      CHECK(on.resumed);
      CHECK(on.convertedFiles == files);
      CHECK(on.unconvertedFiles == 0);
      CHECK(placesAlong(*path) == placesAlong(*whole));
      CHECK(on.visited == visited);
      CHECK(std::any_of(told.begin(), told.end(), [](const LogLine& l) {
        return l.level == LogLevel::Info &&
               l.text.find("was written by an earlier version of the program") !=
                   std::string::npos;
      }));
      CHECK(std::none_of(told.begin(), told.end(), [](const LogLine& l) {
        return l.level == LogLevel::Warning || l.level == LogLevel::Error;
      }));
      CHECK(std::filesystem::is_empty(dir));
    }

    // carried on for one level and saved again: every file in the folder
    // is this version's, under the names the state had
    {
      const std::size_t files = savedByOlder();
      const std::map<std::string, std::string> before = levelEndings(dir);
      StopSource point;
      point.request(StopKind::Pause);
      SlideSearch second = kind.make();
      second.savePoint = point.token();
      CHECK_FALSE(kind.run(second).has_value());
      REQUIRE(second.outcome == SlideOutcome::Stopped);
      REQUIRE(second.saved);
      CHECK(second.resumed);
      CHECK(second.convertedFiles == files);
      const std::map<std::string, std::string> after = levelEndings(dir);
      REQUIRE_FALSE(after.empty());
      for (const auto& [name, ending] : after) {
        INFO(name);
        CHECK(ending == current);
      }
      SlideSearch on = kind.make();
      const std::optional<Separation> path = kind.run(on);
      REQUIRE(path.has_value());
      CHECK(on.resumed);
      CHECK(on.convertedFiles == 0);
      CHECK(placesAlong(*path) == placesAlong(*whole));
      CHECK(std::filesystem::is_empty(dir));
    }

    // stopped before the files are written again: they stay as they
    // were, the search is saved as it was, and the next carry-on writes
    // them (with nested slides a level that waited to go into the visited
    // sets has gone in: a new file, this version's, for some of the old)
    {
      const std::size_t files = savedByOlder();
      const std::map<std::string, std::string> before = levelEndings(dir);
      StopSource stop;
      stop.request(StopKind::Pause);
      SlideSearch second = kind.make();
      second.stop = stop.token();
      CHECK_FALSE(kind.run(second).has_value());
      REQUIRE(second.outcome == SlideOutcome::Stopped);
      REQUIRE(second.saved);
      CHECK(second.convertedFiles == 0);
      CHECK(second.unconvertedFiles == files);
      std::size_t left = 0;
      for (const auto& [name, ending] : levelEndings(dir)) {
        INFO(name);
        if (before.contains(name)) {
          CHECK(ending == older);
          left++;
        } else {
          CHECK(ending == current);
        }
      }
      REQUIRE(left > 0);
      SlideSearch on = kind.make();
      const std::optional<Separation> path = kind.run(on);
      REQUIRE(path.has_value());
      CHECK(on.resumed);
      CHECK(on.convertedFiles == left);
      CHECK(placesAlong(*path) == placesAlong(*whole));
      CHECK(std::filesystem::is_empty(dir));
    }

    // no room to write them again (every write fails): they are read as
    // they are, nothing of the saved search is lost, and the next carry-on
    // with room writes them
    {
      const std::size_t files = savedByOlder();
      SlideSearch second = kind.make();
      second.failWriteAt = 1;
      std::vector<LogLine> told;
      second.onLog = [&told](LogLevel level, const std::string& text) {
        told.push_back(LogLine{level, text});
      };
      CHECK_FALSE(kind.run(second).has_value());
      CHECK(second.outcome == SlideOutcome::Disk);
      // carried on and stopped at its first write; or, with nested slides,
      // not carried on at all (a level waits to be written into the
      // visited sets) and kept
      CHECK(second.resumed != second.savedUnread);
      CHECK(second.convertedFiles == 0);
      if (second.resumed) {
        CHECK(second.unconvertedFiles == files);
      }
      CHECK(std::any_of(told.begin(), told.end(), [](const LogLine& l) {
        return l.level == LogLevel::Warning &&
               l.text.find("The search reads them as they are") != std::string::npos;
      }));
      SlideSearch on = kind.make();
      const std::optional<Separation> path = kind.run(on);
      REQUIRE(path.has_value());
      CHECK(on.resumed);
      CHECK(on.convertedFiles > 0);
      CHECK(on.unconvertedFiles == 0);
      CHECK(placesAlong(*path) == placesAlong(*whole));
      CHECK(std::filesystem::is_empty(dir));
    }
  }
  std::filesystem::remove_all(dir);
}

#endif

// The files of the levels compressed well, fast or not at all: the same
// search, level for level, and a search saved with one is carried on with
// another.
TEST_CASE("sliding: the levels' files compressed or not, the same search",
          "[sliding][resume][parallel]") {
  const ScratchDir scratch("slidecompress");
  const std::filesystem::path dir = scratch.path() / "work";
  const SlideFixture f = slideFixture(4, 5, kKlotski);
  SlideSearch straight;
  straight.oneEnd = true;
  const std::optional<Separation> whole = findSlidePath(f.puz, f.pr(), *f.start, straight);
  REQUIRE(whole.has_value());
  CHECK(straight.diskBytes == 0);

  std::uint64_t plain = 0;
  for (const int compression : {0, 1, 9}) {
    INFO("compression " << compression);
    SlideSearch s;
    s.oneEnd = true;
    s.threads = 3;
    s.workDir = dir.string();
    s.memoryLevelBytes = 1;
    s.compression = compression;
    const std::optional<Separation> path = findSlidePath(f.puz, f.pr(), *f.start, s);
    REQUIRE(path.has_value());
    CHECK(placesAlong(*path) == placesAlong(*whole));
    CHECK(s.visited == straight.visited);
    CHECK(s.diskBytes > 0);
    if (compression == 0) {
      plain = s.diskBytes;
    } else {
      CHECK(s.diskBytes < plain);
    }
    CHECK(std::filesystem::is_empty(dir));
  }

  // stopped between two levels with its files as they are, carried on
  // with them compressed
  StopSource point;
  point.request(StopKind::Pause);
  SlideSearch first;
  first.oneEnd = true;
  first.workDir = dir.string();
  first.memoryLevelBytes = 1;
  first.compression = 0;
  first.savePoint = point.token();
  CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, first).has_value());
  REQUIRE(first.outcome == SlideOutcome::Stopped);
  REQUIRE(first.saved);
  SlideSearch second;
  second.oneEnd = true;
  second.workDir = dir.string();
  second.memoryLevelBytes = 1;
  const std::optional<Separation> rest = findSlidePath(f.puz, f.pr(), *f.start, second);
  REQUIRE(rest.has_value());
  CHECK(second.resumed);
  CHECK(placesAlong(*rest) == placesAlong(*whole));
  CHECK(second.visited == straight.visited);
  CHECK(std::filesystem::is_empty(dir));
  std::filesystem::remove_all(dir);
}

// A write that fails while a level is made (a disk that fills) does not
// cost the search: the files of the level being made go, there is room
// again, and the search is saved at its last whole level for a search of
// the same start to carry on, wherever in its work the write failed.
TEST_CASE("sliding: a search that cannot write a file is saved at its last whole level",
          "[sliding][resume][parallel][slow]") {
  const ScratchDir scratch("slidewrite");
  const std::filesystem::path dir = scratch.path() / "work";
  const SlideFixture f = slideFixture(4, 5, kKlotski);
  SlideSearch straight;
  const std::optional<Separation> whole = findSlidePath(f.puz, f.pr(), *f.start, straight);
  REQUIRE(whole.has_value());

  unsigned int failures = 0;
  unsigned int finished = 0;
  for (std::uint64_t at = 3000; finished == 0; at += at / 2) {
    INFO("the write past " << at << " bytes fails");
    REQUIRE(at < (std::uint64_t{1} << 40));
    std::vector<LogLine> told;
    SlideSearch first;
    first.threads = 2;
    first.workDir = dir.string();
    first.memoryLevelBytes = 1;
    first.failWriteAt = at;
    first.failWriteOnce = true;
    first.onLog = [&told](LogLevel level, const std::string& text) {
      if (level != LogLevel::Debug) {
        told.push_back(LogLine{level, text});
      }
    };
    const std::optional<Separation> path = findSlidePath(f.puz, f.pr(), *f.start, first);
    if (path.has_value()) {
      // the search wrote less than that: nothing failed
      CHECK(first.outcome == SlideOutcome::Found);
      CHECK(first.visited == straight.visited);
      finished++;
    } else {
      failures++;
      REQUIRE(first.outcome == SlideOutcome::Disk);
      REQUIRE(first.saved);
      CHECK(first.diskHeldToFree);
      CHECK(first.error.find("Could not write the search's files") != std::string::npos);
      CHECK(first.visited < straight.visited);
      CHECK(std::any_of(told.begin(), told.end(), [](const LogLine& l) {
        return l.level == LogLevel::Warning &&
               l.text.find("The search is saved") != std::string::npos;
      }));
      CHECK_FALSE(std::filesystem::is_empty(dir));

      SlideSearch second;
      second.threads = 3;
      second.workDir = dir.string();
      second.memoryLevelBytes = 1;
      const std::optional<Separation> rest = findSlidePath(f.puz, f.pr(), *f.start, second);
      REQUIRE(rest.has_value());
      CHECK(second.resumed);
      CHECK(second.outcome == SlideOutcome::Found);
      CHECK(rest->getMoves() == 81);
      CHECK(placesAlong(*rest) == placesAlong(*whole));
      CHECK(second.visited == straight.visited);
    }
    CHECK(std::filesystem::is_empty(dir));
  }
  // the write failed at many places of the search's work before it got through
  CHECK(failures >= 8);
  std::filesystem::remove_all(dir);
}

// When the disk stays full, so that the search cannot save itself either,
// it is left as it was last saved while it ran: the files that save named
// are all still there. Without such a save there is nothing to keep.
TEST_CASE("sliding: a search that cannot save itself is left as it was last saved",
          "[sliding][resume][parallel]") {
  const ScratchDir scratch("slidefull");
  const std::filesystem::path dir = scratch.path() / "work";
  const SlideFixture f = slideFixture(4, 5, kKlotski);
  SlideSearch straight;
  const std::optional<Separation> whole = findSlidePath(f.puz, f.pr(), *f.start, straight);
  REQUIRE(whole.has_value());

  // its levels in memory, so that only its saves write, one after every
  // level whatever a save takes (with SaveCadence's wait, 50 times the
  // last save, a machine with a slower disk ended the search before its
  // second save and never filled the disk); the disk fills in the middle
  // of one of them
  {
    std::vector<LogLine> told;
    SlideSearch first;
    first.threads = 2;
    first.workDir = dir.string();
    first.autosaveFloorSeconds = 0;
    first.autosaveEveryLevel = true;
    first.failWriteAt = 60000;
    first.onLog = [&told](LogLevel level, const std::string& text) {
      if (level != LogLevel::Debug) {
        told.push_back(LogLine{level, text});
      }
    };
    CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, first).has_value());
    REQUIRE(first.outcome == SlideOutcome::Disk);
    REQUIRE(first.saved);
    CHECK(first.diskHeldToFree);
    CHECK(std::any_of(told.begin(), told.end(), [](const LogLine& l) {
      return l.level == LogLevel::Warning &&
             l.text.find("The search is saved") != std::string::npos &&
             l.text.find("as it was last saved while it ran") != std::string::npos;
    }));
    CHECK_FALSE(std::filesystem::is_empty(dir));

    SlideSearch second;
    second.threads = 3;
    second.workDir = dir.string();
    const std::optional<Separation> rest = findSlidePath(f.puz, f.pr(), *f.start, second);
    REQUIRE(rest.has_value());
    CHECK(second.resumed);
    CHECK(rest->getMoves() == 81);
    CHECK(placesAlong(*rest) == placesAlong(*whole));
    CHECK(second.visited == straight.visited);
    CHECK(std::filesystem::is_empty(dir));
  }

  // the disk full from the first save on: no save to go back to, and the
  // levels in memory cannot be written, so nothing is kept
  {
    SlideSearch first;
    first.threads = 2;
    first.workDir = dir.string();
    first.autosaveFloorSeconds = 0;
    first.failWriteAt = 1;
    CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, first).has_value());
    REQUIRE(first.outcome == SlideOutcome::Disk);
    CHECK_FALSE(first.saved);
    CHECK(first.error.find("Could not write the search's files") != std::string::npos);
    CHECK(std::filesystem::is_empty(dir));
  }
  // and when not even the first file can be written
  {
    SlideSearch first;
    first.workDir = dir.string();
    first.memoryLevelBytes = 1;
    first.failWriteAt = 1;
    CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, first).has_value());
    REQUIRE(first.outcome == SlideOutcome::Disk);
    CHECK_FALSE(first.saved);
    CHECK(std::filesystem::is_empty(dir));
  }
  std::filesystem::remove_all(dir);
}

#ifndef _WIN32
// A save whose write fails only once its file is open (the disk full when
// the bytes still buffered go out at the close) is a failed save (cleanup
// item 4.1): the last good state.bin stays as it was, never replaced by a
// file cut short, and the search is left as that save has it, to be
// carried on to the same path. Before, the error at the close was lost:
// the cut file took state.bin's place, the run said it was saved, and the
// next Continue could not read it.
TEST_CASE("sliding: a state.bin whose write fails leaves the last good one",
          "[sliding][resume][parallel]") {
  const ScratchDir scratch("slidestatefull");
  const std::filesystem::path dir = scratch.path() / "work";
  const SlideFixture f = slideFixture(4, 5, kKlotski);
  SlideSearch straight;
  const std::optional<Separation> whole = findSlidePath(f.puz, f.pr(), *f.start, straight);
  REQUIRE(whole.has_value());

  auto stateFile = [&dir] {
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
      if (e.path().filename() == "state.bin") {
        return e.path();
      }
    }
    return std::filesystem::path();
  };
  auto bytesOf = [](const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  };

  // Every set in a file, so that a save writes its state.bin alone, and a
  // save after every level. After a few levels the disk fills (files may
  // hold 16 bytes, fewer than any state.bin) and a save point is asked
  // for: the save of the stop is the write that fails.
  constexpr unsigned int kFullAt = 12;
  std::optional<test::FileSizeLimit> full;
  std::string lastGood;
  StopSource point;
  unsigned int levels = 0;
  std::vector<LogLine> told;
  SlideSearch first;
  first.threads = 2;
  first.workDir = dir.string();
  first.memoryLevelBytes = 1;
  first.autosaveFloorSeconds = 0;
  first.autosaveEveryLevel = true;
  first.savePoint = point.token();
  first.onLevel = [&](const std::string&) {
    if (++levels == kFullAt) {
      lastGood = bytesOf(stateFile());
      full.emplace(16);
      point.request(StopKind::Pause);
    }
  };
  first.onLog = [&told](LogLevel level, const std::string& text) {
    told.push_back(LogLine{level, text});
  };
  CHECK_FALSE(findSlidePath(f.puz, f.pr(), *f.start, first).has_value());
  full.reset();
  REQUIRE(levels == kFullAt);
  REQUIRE(first.outcome == SlideOutcome::Stopped);
  REQUIRE(lastGood.size() > 16);
  // the save failed, and the search is left as the one before it saved it
  CHECK(first.saved);
  CHECK(std::any_of(told.begin(), told.end(), [](const LogLine& l) {
    return l.text.find("The search's state could not be written") != std::string::npos;
  }));
  CHECK(std::any_of(told.begin(), told.end(), [](const LogLine& l) {
    return l.text.find("as it was last saved while it ran") != std::string::npos;
  }));
  const std::filesystem::path state = stateFile();
  REQUIRE_FALSE(state.empty());
  CHECK(bytesOf(state) == lastGood);
  // and the failed save's temporary file is gone
  for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
    CHECK(e.path().extension() != ".tmp");
  }

  // carried on from that save to the same path
  SlideSearch second;
  second.threads = 3;
  second.workDir = dir.string();
  const std::optional<Separation> rest = findSlidePath(f.puz, f.pr(), *f.start, second);
  REQUIRE(rest.has_value());
  CHECK(second.resumed);
  CHECK(rest->getMoves() == 81);
  CHECK(placesAlong(*rest) == placesAlong(*whole));
  CHECK(second.visited == straight.visited);
  CHECK(std::filesystem::is_empty(dir));
  std::filesystem::remove_all(dir);
}

#endif

// What making a level took on the disk at its most is measured, for what
// the next will take; and the levels kept for tracing the path back are
// thinned once they take half the disk the search may use, the files of
// those thinned out gone at once though the last save named them.
TEST_CASE("sliding: the levels kept are thinned at half the disk budget",
          "[sliding][parallel]") {
  const ScratchDir scratch("slidethin");
  const std::filesystem::path dir = scratch.path() / "work";
  const SlideFixture f = slideFixture(4, 5, kKlotski);
  auto search = [&] {
    SlideSearch s;
    s.oneEnd = true;
    s.threads = 2;
    s.workDir = dir.string();
    s.memoryLevelBytes = 1;
    s.autosaveFloorSeconds = 0;
    return s;
  };
  SlideSearch all = search();
  all.measureLevelBytes = 1;
  std::string levelLine;
  all.onLog = [&levelLine](LogLevel level, const std::string& text) {
    if (level == LogLevel::Debug && text.find("making it took") != std::string::npos) {
      levelLine = text;
    }
  };
  const std::optional<Separation> whole = findSlidePath(f.puz, f.pr(), *f.start, all);
  REQUIRE(whole.has_value());
  CHECK(all.levelInterval == 1);
  // a level's runs and merges take more than the level
  CHECK(all.levelWorkRatio > 1);
  CHECK(levelLine.find("times a level's bytes") != std::string::npos);
  // too small to measure unless told: the search goes by the fixed share
  SlideSearch unmeasured = search();
  REQUIRE(findSlidePath(f.puz, f.pr(), *f.start, unmeasured).has_value());
  CHECK(unmeasured.levelWorkRatio == 0);

  SlideSearch tight = search();
  tight.diskBudget = all.diskBytes;
  const std::optional<Separation> path = findSlidePath(f.puz, f.pr(), *f.start, tight);
  REQUIRE(path.has_value());
  CHECK(tight.outcome == SlideOutcome::Found);
  CHECK(tight.levelInterval >= 2);
  // as short a path; the levels searched again across the gaps count too
  CHECK(path->getMoves() == whole->getMoves());
  CHECK(tight.visited >= all.visited);
  CHECK(tight.diskBytes < all.diskBytes);
  CHECK(std::filesystem::is_empty(dir));
  std::filesystem::remove_all(dir);
}

namespace {

// The lines a search writes for the log, from whichever thread.
struct SlideLog {
  std::mutex m;
  std::vector<std::string> lines;
  void watch(SlideSearch& s) {
    s.onLog = [this](LogLevel, const std::string& line) {
      const std::lock_guard<std::mutex> lock(m);
      lines.push_back(line);
    };
  }
  [[nodiscard]] bool says(const std::string& text) {
    const std::lock_guard<std::mutex> lock(m);
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) {
      return line.find(text) != std::string::npos;
    });
  }
};

// Every file under a folder with its bytes: a saved search as it stands.
std::map<std::string, std::string> folderFiles(const std::filesystem::path& dir) {
  std::map<std::string, std::string> files;
  for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
    if (e.is_regular_file()) {
      std::ifstream in(e.path(), std::ios::binary);
      std::ostringstream bytes;
      bytes << in.rdbuf();
      files[std::filesystem::relative(e.path(), dir).string()] = bytes.str();
    }
  }
  return files;
}

}  // namespace

// Cleanup item 18.4. With nested slides the level that leaves the two
// newest is merged into the visited sets by one thread while the others
// expand the next: deep in a search that merge takes minutes, and it heard
// no stop, so Pause and Abort (and every call of the window behind them)
// waited for it. It looks at the token now. The set being made is let go,
// the sets it was made of stay, and a level that never went in is put back
// when the search is carried on. The tests stop it at a chosen arrangement
// of a chosen merge (SlideSearch::onVisitedMerge), early and late in the
// search, in the first of a level's two merges and in the second: stopped,
// saved and carried on, the search finds the path of a search never
// stopped, through the same arrangements.
TEST_CASE("sliding: a stop ends a merge into the visited sets and the search is carried on",
          "[sliding][resume][parallel]") {
  const ScratchDir scratch("slidevisitedstop");
  const std::filesystem::path dir = scratch.path() / "work";
  const GridBoard g = settledBoard(37);
  auto gridSearch = [] {
    SlideSearch s = nestedSearch(3);
    s.threads = 2;
    return s;
  };

  // a search never stopped: its path, and the arrangements its merges take
  bool fromGoal = false;
  SlideSearch straight = gridSearch();
  std::uint64_t taken = 0;
  std::uint64_t merges = 0;
  straight.onVisitedMerge = [&](std::uint64_t at) {
    taken++;
    merges += at == 1 ? 1 : 0;
  };
  const std::optional<Separation> whole = searched(g, straight, fromGoal);
  REQUIRE(whole.has_value());
  REQUIRE(fromGoal);
  REQUIRE(merges >= 8);
  REQUIRE(taken >= 200);

  for (const std::uint64_t at :
       {std::uint64_t{1}, std::uint64_t{2}, taken / 50, taken / 9, taken / 4, taken / 2,
        taken - taken / 5, taken - 1, taken}) {
    INFO("stopped at arrangement " << at << " of " << taken << " merged");
    const StopSource stop;
    SlideSearch first = gridSearch();
    first.workDir = dir.string();
    first.stop = stop.token();
    std::uint64_t seen = 0;
    first.onVisitedMerge = [&](std::uint64_t) {
      if (++seen == at) {
        stop.request(StopKind::Pause);
      }
    };
    SlideLog log;
    log.watch(first);
    CHECK_FALSE(findSlidePath(g.puz, g.pr(), *g.start, first).has_value());
    REQUIRE(first.outcome == SlideOutcome::Stopped);
    CHECK(first.saved);
    // the merge ended at that arrangement: it took no other
    CHECK(seen == at);
    CHECK(log.says("A merge into the visited sets was stopped"));

    SlideSearch second = gridSearch();
    second.threads = 3;
    second.workDir = dir.string();
    const std::optional<Separation> rest = searched(g, second, fromGoal);
    REQUIRE(rest.has_value());
    CHECK(second.resumed);
    CHECK(rest->getMoves() == whole->getMoves());
    CHECK(placesAlong(*rest) == placesAlong(*whole));
    checkGridPath(g, *rest);
    CHECK(second.visited == straight.visited);
    // the folder goes with the search
    CHECK(std::filesystem::is_empty(dir));
  }

  // an abort ends the merge as well, and nothing is saved
  {
    const StopSource stop;
    SlideSearch gone = gridSearch();
    gone.workDir = dir.string();
    gone.stop = stop.token();
    std::uint64_t seen = 0;
    gone.onVisitedMerge = [&](std::uint64_t) {
      if (++seen == taken / 3) {
        stop.request(StopKind::Abort);
      }
    };
    CHECK_FALSE(findSlidePath(g.puz, g.pr(), *g.start, gone).has_value());
    CHECK(gone.outcome == SlideOutcome::Stopped);
    CHECK_FALSE(gone.saved);
    CHECK(seen == taken / 3);
    CHECK(std::filesystem::is_empty(dir));
  }
  std::filesystem::remove_all(dir);
}

// A search saved between two levels has a level still waiting to go into
// the visited sets, which goes in when the search is taken up again
// (restoreVisited): the same merge, as long, before anything is searched.
// A stop there leaves the saved search exactly as it was, every file of
// it, to be taken up by the next run; an abort throws it away.
TEST_CASE("sliding: a stop while a saved search is taken up leaves it as it was saved",
          "[sliding][resume][parallel]") {
  const ScratchDir scratch("slidetakeupstop");
  const std::filesystem::path dir = scratch.path() / "work";
  const GridBoard g = settledBoard(37);
  auto gridSearch = [] {
    SlideSearch s = nestedSearch(3);
    s.threads = 2;
    return s;
  };
  bool fromGoal = false;
  SlideSearch straight = gridSearch();
  const std::optional<Separation> whole = searched(g, straight, fromGoal);
  REQUIRE(whole.has_value());
  const unsigned int moves = whole->getMoves();

  // stopped `depth` moves deep, between two levels, and saved
  auto savedAt = [&](std::uint64_t depth) {
    const StopSource stop;
    SlideSearch first = gridSearch();
    first.workDir = dir.string();
    first.stop = stop.token();
    first.onDepth = [&](std::uint64_t at) {
      if (at >= depth) {
        stop.request(StopKind::Pause);
      }
    };
    CHECK_FALSE(findSlidePath(g.puz, g.pr(), *g.start, first).has_value());
    REQUIRE(first.outcome == SlideOutcome::Stopped);
    REQUIRE(first.saved);
  };

  int inTakeUp = 0;
  for (const std::uint64_t depth : {4U, 6U, 9U, moves - 2U}) {
    INFO("saved " << depth << " moves deep");
    savedAt(depth);
    const std::map<std::string, std::string> saved = folderFiles(dir);
    REQUIRE(saved.size() >= 3);

    // taken up and stopped at the first arrangement a merge takes
    const StopSource stop;
    SlideSearch second = gridSearch();
    second.workDir = dir.string();
    second.stop = stop.token();
    std::uint64_t seen = 0;
    bool searching = false;
    second.onVisitedMerge = [&](std::uint64_t) {
      seen++;
      stop.request(StopKind::Pause);
    };
    second.onDepth = [&](std::uint64_t) { searching = true; };
    SlideLog log;
    log.watch(second);
    CHECK_FALSE(findSlidePath(g.puz, g.pr(), *g.start, second).has_value());
    REQUIRE(second.outcome == SlideOutcome::Stopped);
    CHECK(second.saved);
    CHECK(seen == 1);
    if (!searching) {
      // the merge was the taking up's: nothing was searched, nothing written
      inTakeUp++;
      CHECK_FALSE(second.resumed);
      CHECK(log.says("was taken up: it is kept as it was saved"));
      CHECK(folderFiles(dir) == saved);
    }

    // and the next run takes it up, to the path of a search never stopped
    SlideSearch third = gridSearch();
    third.threads = 3;
    third.workDir = dir.string();
    const std::optional<Separation> rest = searched(g, third, fromGoal);
    REQUIRE(rest.has_value());
    CHECK(third.resumed);
    CHECK(rest->getMoves() == moves);
    CHECK(placesAlong(*rest) == placesAlong(*whole));
    checkGridPath(g, *rest);
    CHECK(std::filesystem::is_empty(dir));
  }
  // most of them had a level waiting that the taking up merged (where it
  // went in without a merge, the stop came in the search's next one)
  CHECK(inTakeUp >= 2);

  // an abort there: the saved search goes, as when a running search is aborted
  savedAt(6);
  const StopSource stop;
  SlideSearch gone = gridSearch();
  gone.workDir = dir.string();
  gone.stop = stop.token();
  bool searching = false;
  gone.onVisitedMerge = [&](std::uint64_t) { stop.request(StopKind::Abort); };
  gone.onDepth = [&](std::uint64_t) { searching = true; };
  SlideLog log;
  log.watch(gone);
  CHECK_FALSE(findSlidePath(g.puz, g.pr(), *g.start, gone).has_value());
  CHECK(gone.outcome == SlideOutcome::Stopped);
  CHECK_FALSE(gone.saved);
  CHECK_FALSE(searching);
  CHECK(log.says("aborted while its saved search was taken up"));
  CHECK(std::filesystem::is_empty(dir));
  std::filesystem::remove_all(dir);
}
