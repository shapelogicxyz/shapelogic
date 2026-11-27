// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The level search of the slide search and what a table search is made
/// of (internal to the slide search, cleanup item 21.1): LevelSearch,
/// nameSpace (the tables and the name of a search), tableSearch and
/// tableProbe. They are templates over the key; each key width is compiled
/// in a file of its own (sliding_search_{64,128,192,256,512}.cpp), and the
/// other files that include this one see them through the extern templates
/// at its end.

#include "shapelogic/base/assert.h"
#include "shapelogic/base/fnv1a.h"
#include "shapelogic/base/log_line.h"
#include "shapelogic/base/save_cadence.h"
#include "shapelogic/base/search_memory.h"
#include "shapelogic/base/thread_pool.h"
#include "shapelogic/solve/search_sample.h"
#include "shapelogic/solve/slide_board.h"
#include "shapelogic/solve/slide_key.h"
#include "shapelogic/solve/slide_levels.h"
#include "shapelogic/solve/slide_space.h"
#include "shapelogic/solve/slide_store.h"
#include "shapelogic/solve/sliding_search.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace sl::sliding::detail {

// What one arrangement costs the level search in memory, in key sizes, as
// the memory budget is told in arrangements (SlideSearch::maxMemoryStates,
// memoryStates): its place in a level and its share of the pieces and
// merges that make the next.
inline constexpr std::uint64_t kLevelStateKeys = 3;
// The disk the making of a level is taken to need, for each byte the level
// before it came to, until the search has measured it on a level of its
// own (LevelSearch::noteWork); and the margin a measured need is given.
inline constexpr double kLevelWorkRatio = 5;
inline constexpr double kLevelWorkMargin = 1.25;

// The name of a search (nameSpace): FNV-1a (base/fnv1a.h), each value at
// the width its call names, and the start's key by its 64-bit words. The
// width at each call is the one its value had when the names were pinned
// (cleanup item 19.3); tests/unit/solve/test_search_names.cpp pins what
// comes out.
class Fingerprint : public Fnv1a {
 public:
  // A key: its 64-bit words, the least significant first.
  void key(std::uint64_t k) { u64(k); }
#ifdef __SIZEOF_INT128__
  void key(WideKey k) {
    u64(static_cast<std::uint64_t>(k));
    u64(static_cast<std::uint64_t>(k >> 64));
  }
#endif
  template <unsigned int W>
  void key(const BigKey<W>& k) {
    for (const std::uint64_t word : k.w) {
      u64(word);
    }
  }
};

// ---------------------------------------------------------------------------
// The full slide search: breadth first, one level at a time, a level being
// every arrangement a given number of moves from the start.
//
//  - Every level is a sorted KeySet (slide_levels.h), delta coded: in
//    memory while the memory budget allows, else a file in the search's
//    folder, so the search is limited by the disk rather than by memory.
//    Only the level being expanded and the one before are needed as the
//    next is made; the threads gather what the moves reach in sorted runs
//    (sets too), and a merge of the runs less those two levels, each
//    thread a range of keys, writes the next level.
//  - A move that one piece makes can always be made backwards, and so can a
//    nested group's unless the group closes round a piece on its way. An
//    arrangement reached by a move that can be made backwards is new unless
//    it is in this level or the one before: had it been reached earlier,
//    the arrangement it came from would have been reached by now too. So
//    only the arrangements reached by the other moves are looked for in the
//    older levels, kept merged into a few sorted sets.
//  - When the goal is one arrangement and a level has grown wide, the
//    search goes on from the goal as well, always growing the narrower end,
//    until the two ends meet (the Panex search's way): the path is as
//    short, and far fewer arrangements are visited. With nested slides the
//    goal end makes the moves backwards (SlideMover::movesBack), where the
//    tables have them (SlideSpace::backMoves), and each end looks for its
//    one-way moves in its own older levels.
//  - The older levels are kept for tracing the path back. Without nested
//    slides, when they outgrow the disk every other one goes (the gaps are
//    filled, when the path is traced, by a small search out from the
//    arrangement at hand); with nested slides every level is needed.
//  - A search stopped before its end is saved in its folder, and a search
//    of the same start carries it on.
//
// It finds a path with the fewest moves, the same every run (legacySearch
// finds one as short, though not always the same one).

template <class Key>
class LevelSearch {
 public:
  using Set = levels::KeySet<Key>;

  LevelSearch(const SlideSpace<Key>& space, SlideSearch& search, std::uint64_t problemKey,
              std::uint64_t searchKey)
      : s_(space),
        search_(search),
        problemKey_(problemKey),
        searchKey_(searchKey),
        folder_(search.workDir, searchKey),
        store_(folder_,
               search.maxMemoryStates != 0
                   ? search.maxMemoryStates * kLevelStateKeys * sizeof(Key)
                   : memoryBudget(search.memoryBytes, search.highMemory),
               search.memoryLevelBytes,
               search.maxMemoryStates != 0 ? nullptr : search.memoryShare,
               // with nested slides keys are looked up in the levels one at
               // a time: small chunks, so that one costs little
               levels::FileOptions{std::clamp(search.compression, 0, kMaxCompression),
                                   space.nested ? levels::kLookupChunkBytes : levels::kChunkBytes,
                                   nullptr}) {
    store_.failWritesAt(search.failWriteAt, search.failWriteOnce);
  }

  // The search; chain gets the path's arrangements, start first, when the
  // goal is found. search_.outcome says how it ended.
  void run(std::vector<Key>& chain) {
    if (search_.pool == nullptr) {
      ownPool_ = std::make_unique<ThreadPool>(
          search_.threads != 0 ? std::min(search_.threads, kMaxThreads) : hardwareThreads());
      pool_ = ownPool_.get();
    } else {
      pool_ = search_.pool;
    }
    threads_ = std::max(
        1U, search_.threads != 0 ? std::min(search_.threads, pool_->cores()) : pool_->cores());
    for (unsigned int i = 0; i <= threads_; i++) {
      movers_.push_back(std::make_unique<SlideMover<Key>>(s_));
    }
    search_.memoryStates = std::max<std::uint64_t>(store_.budget() / (kLevelStateKeys * sizeof(Key)), 1);
    search_.depth = 0;
    search_.diskBytes = 0;
    search_.oneWayRepeats = 0;
    search_.levelInterval = 1;
    search_.saved = false;
    search_.resumed = false;
    search_.savedUnread = false;
    search_.convertedFiles = 0;
    search_.unconvertedFiles = 0;
    search_.outcome = SlideOutcome::NoPath;
    goal_ = s_.singleGoal();
    if (!s_.nested && search_.keepEvery > 1) {
      interval_ = search_.keepEvery;
    }

    if (!start()) {
      // not even its first file written: more room is what helps (not for
      // a saved search that could not be read back)
      search_.diskHeldToFree = search_.outcome == SlideOutcome::Disk && !search_.savedUnread;
      return;
    }
    const SlideOutcome end = loop(chain);
    if (search_.phase != nullptr) {
      search_.phase->end();
    }
    search_.outcome = end;
    // a shared budget may have changed as the search ran: the limit it ended at
    search_.memoryStates =
        std::max<std::uint64_t>(store_.budget() / (kLevelStateKeys * sizeof(Key)), 1);
    search_.depth = std::uint64_t{fwd_.depth} + bwd_.depth;
    search_.levelInterval = interval_;
    search_.visited = visited_;
    search_.diskBytes = std::max(search_.diskBytes, store_.used().second);
    search_.levelWorkRatio = workMeasured_ ? workRatio_ : 0;
    // A search that stopped short is saved for a search of the same start
    // to carry on: when paused, when it filled its disk budget or the disk
    // (it stops with room left to save itself, so the user can raise the
    // budget or free space and carry on), and when a file could not be
    // written. By then the files of the level it was making are gone, so
    // there is room again for what a save writes: it is saved as it stands,
    // at its last whole level. Failing that, the files its last save while
    // it ran named are all still there (pinned_), and it is left as that
    // save has it. An abort throws the search away: it is not written
    // first.
    const bool stopped = end == SlideOutcome::Stopped;
    const bool writeFailed = end == SlideOutcome::Disk && store_.failure == SlideOutcome::Disk;
    const bool save = (stopped && search_.stop.kind() != StopKind::Abort) ||
                      (end == SlideOutcome::Disk && (diskChosen_ || !folder_.dir.empty()));
    if (writeFailed) {
      // more room on the disk is what helps, not a larger budget
      search_.diskHeldToFree = true;
    }
    folder_.keep = false;
    bool asLastSaved = false;
    if (save && saveState(stopped ? search_.stopProgress : nullptr)) {
      search_.saved = true;
      folder_.keep = true;
      // the files stay: the sets must not delete them as they go
      forEachLiveSet([](SetPtr<Key>& set) {
        if (set) {
          set->keepFile();
        }
      });
      pinned_.clear();
    } else if (save && !pinned_.empty()) {
      search_.saved = true;
      asLastSaved = true;
      folder_.keep = true;
      for (SetPtr<Key>& set : pinned_) {
        set->keepFile();
      }
      pinned_.clear();
    }
    const std::string where = folder_.dir.empty() ? std::string() : " in " + folder_.dir.string();
    const std::string deep = asLastSaved ? "as it was last saved while it ran, " + pinnedDeep_
                                         : deepText();
    if (end == SlideOutcome::Disk) {
      tellLog(save && search_.saved ? LogLevel::Warning : LogLevel::Error,
              search_.error + (search_.saved ? " The search is saved" + where + ", " + deep + "."
                                             : " The search could not be saved."));
    } else if (end == SlideOutcome::Memory) {
      tellLog(LogLevel::Warning, "The search reached its memory limit, " + deep +
                                     (search_.error.empty() ? "." : ": " + search_.error));
    } else if (stopped) {
      if (search_.stop.kind() == StopKind::Abort) {
        tellLog(LogLevel::Info, "The search was aborted, " + deep + ".");
      } else if (search_.saved) {
        tellLog(LogLevel::Info, "The search stopped and is saved" + where + ", " + deep + ".");
      } else if (!folder_.dir.empty()) {
        tellLog(LogLevel::Error, "The search stopped, and could not be saved" + where + ", " +
                                     deep + (store_.error.empty() ? "." : ": " + store_.error));
      }
    }
  }

 private:
  // One end of the search: its two newest levels, how deep it is, and the
  // older levels kept for tracing the path back, by level.
  struct Side {
    SetPtr<Key> prev;
    SetPtr<Key> cur;
    unsigned int depth = 0;
    std::vector<std::pair<unsigned int, SetPtr<Key>>> stored;
    // With nested slides: the older levels merged into a few sets, largest
    // first, to look in; and the level to merge in next.
    std::vector<SetPtr<Key>> visited;
    SetPtr<Key> pending;
  };

  enum class StepEnd : std::uint8_t { Next, Hit, Stopped, Failed };

  // A set of one key.
  SetPtr<Key> single(Key k) {
    SetPtr<Key> set = store_.make(sizeof(Key));
    if (!set || !set->add(k) || !store_.finish(set)) {
      if (store_.failure == SlideOutcome::NoPath) {
        store_.ioFailed();
      }
      return nullptr;
    }
    return set;
  }

  // The search's beginning: the saved search carried on, or the start.
  bool start() {
    if (search_.resume && folder_.exists()) {
      const unsigned int interval = interval_;
      const unsigned int fileNumber = store_.fileNumber;
      const auto began = std::chrono::steady_clock::now();
      restoreVisitedTime_ = {};
      const Resumed resumed = resume();
      if (search_.pickingUpSince != nullptr) {
        search_.pickingUpSince->store(0, std::memory_order_relaxed);
      }
      if (resumed == Resumed::Yes) {
        search_.resumed = true;
        // how long it took, and how much of that the visited sets: what a
        // save point followed by a continue costs a deep search (the level
        // that waited would have gone into them in the run that stopped)
        tellLog(LogLevel::Info,
                "Carried on the saved search in " + folder_.dir.string() + ": the start end " +
                    std::to_string(fwd_.depth) + " moves deep" +
                    (bidirectional_ ? ", the goal end " + std::to_string(bwd_.depth) : "") +
                    ", " + std::to_string(visited_.load()) + " arrangements visited. Taken up in " +
                    secondsText(std::chrono::steady_clock::now() - began) + ", of which " +
                    secondsText(restoreVisitedTime_) + " for the visited sets.");
        return true;
      }
      // Nothing of what resume took up stays: an end's depth and levels,
      // the visited sets (their files are kept: resume saw to that).
      fwd_ = Side{};
      bwd_ = Side{};
      bidirectional_ = false;
      interval_ = interval;
      visited_ = 0;
      search_.oneWayRepeats = 0;
      store_.fileNumber = fileNumber;
      if (resumed == Resumed::Stopped) {
        // Stopped while a level that waited went into the visited sets
        // (restoreVisited; cleanup item 18.4). Nothing was searched and
        // nothing is written: state.bin and every file it names are as
        // they were found, so the next run takes the same saved search up
        // (what was merged meanwhile is in files the state does not name,
        // or in memory: it goes). An abort throws the search away, as one
        // that comes while it runs does (run): the folder goes with it.
        const bool abort = search_.stop.kind() == StopKind::Abort;
        folder_.keep = !abort;
        search_.saved = !abort;
        search_.outcome = SlideOutcome::Stopped;
        tellLog(LogLevel::Info,
                abort ? "The search was aborted while its saved search was taken up."
                      : "The search stopped while its saved search in " +
                            folder_.place().string() +
                            " was taken up: it is kept as it was saved.");
        return false;
      }
      if (resumed == Resumed::No) {
        // A saved search is days of work, and what could not be read now
        // may be read later (a drive that dropped out, a file this version
        // does not know): it is never deleted for that. The search ends
        // here, with why, and the saved search stays as it is.
        folder_.keep = true;
        search_.savedUnread = true;
        search_.outcome = SlideOutcome::Disk;
        search_.error = "The saved search in " + folder_.place().string() +
                        " could not be carried on: " + resumeWhy_ + ". It is kept as it is.";
        tellLog(LogLevel::Error, search_.error);
        return false;
      }
      tellLog(LogLevel::Info, "The search folder " + folder_.place().string() + " (" +
                                  folderText(folder_.place()) +
                                  ") holds no saved search, only files of a search that was "
                                  "never saved: they are deleted, and the search begins at the "
                                  "start.");
      folder_.wipe();
    } else if (folder_.exists()) {
      tellLog(LogLevel::Info, "The search folder " + folder_.place().string() + " (" +
                                  folderText(folder_.place()) +
                                  ") held an earlier search, which this one does not carry on: "
                                  "it is deleted, and the search begins at the start.");
      folder_.wipe();
    }
    fwd_.cur = single(s_.startCanon);
    if (!fwd_.cur) {
      search_.outcome = store_.failure;
      search_.error = store_.error;
      return false;
    }
    fwd_.stored.emplace_back(0, fwd_.cur);
    return true;
  }

  // What resume found in the folder.
  enum class Resumed : std::uint8_t {
    Yes,      // the saved search, taken up
    Nothing,  // no saved search: files of a search that was never saved
    No,       // a saved search that cannot be carried on; resumeWhy_ says why
    Stopped,  // a saved search, left as it was: a stop came while it was taken up
  };

  // How a merge into the visited sets ended (insertVisited).
  enum class VisitedEnd : std::uint8_t {
    Whole,    // the level is in
    Stopped,  // the stop token ended it; the sets are as valid as before
    Failed,   // a set could not be read or written
  };

  // Takes up the saved search in the folder. One that cannot be carried on
  // (not this search's, a file that could not be read or is not whole) is
  // left as it was found, every file of it, with why in resumeWhy_.
  Resumed resume() {
    std::map<std::string, SetPtr<Key>> opened;
    auto fail = [this, &opened](std::string why) {
      resumeWhy_ = std::move(why);
      // the files stay: the sets must not delete them as they go
      for (auto& [name, set] : opened) {
        set->keepFile();
      }
      return Resumed::No;
    };
    std::error_code stateEc;
    if (!std::filesystem::exists(folder_.place() / "state.bin", stateEc) && !stateEc) {
      return Resumed::Nothing;
    }
    SavedSearch st;
    if (!readState(folder_.place(), st, true)) {
      return fail("its state.bin could not be read or is damaged");
    }
    if (st.searchKey != searchKey_ || st.problemKey != problemKey_) {
      return fail("it is the saved search of another problem or start");
    }
    if (st.nested != s_.nested) {
      return fail(st.nested ? "it was saved with nested slides, and this search has none"
                            : "it was saved without nested slides, and this search has them");
    }
    tellLog(LogLevel::Debug, "Reading the saved search in " + folder_.place().string() + " (" +
                                 folderText(folder_.place()) + "): its state.bin names " +
                                 savedText(st) + ".");
    // this search's own: taken up from here (start() says when it is)
    if (search_.pickingUpSince != nullptr) {
      search_.pickingUpSince->store(slideClockMs(), std::memory_order_relaxed);
    }
    if (!folder_.open()) {
      return fail("its folder could not be opened");
    }
    // why a set could not be opened: the file gone, not read, or not whole
    std::string setWhy;
    auto openSet = [&](const std::string& name, SetPtr<Key>& into) {
      if (name.empty()) {
        into.reset();
        return true;
      }
      const auto it = opened.find(name);
      if (it != opened.end()) {
        into = it->second;
        return true;
      }
      auto set = std::make_shared<Set>();
      if (!set->open(folder_.dir / name)) {
        const levels::SetFault fault = set->fault();
        const int error = set->faultError();
        set->keepFile();
        std::error_code ec;
        const bool there = std::filesystem::exists(folder_.dir / name, ec) && !ec;
        const std::uintmax_t bytes = there ? std::filesystem::file_size(folder_.dir / name, ec) : 0;
        const std::string size = " (" + std::to_string(ec ? 0 : bytes) + " bytes)";
        if (!there) {
          setWhy = name + " is missing";
        } else if (fault == levels::SetFault::Newer) {
          setWhy = name + " was written by a later version of the program, whose level files "
                          "this one does not read";
        } else if (fault == levels::SetFault::Unread) {
          // the system's own words tell a drive that dropped out from a
          // read it refused
          setWhy = name + " could not be read" + size +
                   (error != 0 ? ": " + std::generic_category().message(error) : "");
        } else {
          setWhy = name + " is not a whole level file" + size;
        }
        return false;
      }
      store_.adopt(set);
      opened.emplace(name, set);
      into = set;
      return true;
    };
    Side* sides[2] = {&fwd_, &bwd_};
    for (int i = 0; i < 2; i++) {
      Side& side = *sides[i];
      const SavedSide& saved = st.side[i];
      const std::string end = i == 0 ? "the start end" : "the goal end";
      side.depth = saved.depth;
      const std::string deep = " (" + std::to_string(saved.depth) + " moves deep)";
      if (!openSet(saved.prev, side.prev) || !openSet(saved.cur, side.cur)) {
        std::string why = setWhy;
        why.append(", one of the two newest levels of ").append(end).append(deep);
        return fail(std::move(why));
      }
      for (const auto& [level, name] : saved.stored) {
        SetPtr<Key> set;
        if (!openSet(name, set)) {
          std::string why = setWhy;
          why.append(", level ").append(std::to_string(level)).append(" of ").append(end).append(deep);
          return fail(std::move(why));
        }
        side.stored.emplace_back(level, set);
      }
    }
    for (int i = 0; i < 2; i++) {
      for (const std::string& name : i == 0 ? st.visitedSets : st.goalVisitedSets) {
        SetPtr<Key> set;
        if (!openSet(name, set)) {
          return fail(setWhy + ", one of the sets of what " +
                      (i == 0 ? "the start end" : "the goal end") + " has visited");
        }
        sides[i]->visited.push_back(set);
      }
    }
    if (!fwd_.cur || (st.bidirectional && !bwd_.cur)) {
      return fail("it names no newest level for one of its ends");
    }
    bidirectional_ = st.bidirectional;
    // a goal end needs the goal, and with nested slides the moves backwards
    if (bidirectional_ && (!goal_ || (s_.nested && !s_.backMoves))) {
      return fail("it was searched from both ends, which this search cannot be");
    }
    interval_ = st.interval;
    visited_ = st.visited;
    search_.oneWayRepeats = st.oneWayRepeats;
    store_.fileNumber = st.fileNumber;
    // files the state does not name are leftovers of the stopped run
    std::error_code ec;
    std::string leftovers;
    unsigned int removed = 0;
    for (const auto& e : std::filesystem::directory_iterator(folder_.dir, ec)) {
      const std::string name = e.path().filename().string();
      if (name != "state.bin" && opened.find(name) == opened.end()) {
        if (++removed <= 8) {
          leftovers.append(" ").append(name);
        }
        std::filesystem::remove(e.path(), ec);
      }
    }
    if (removed > 0) {
      tellLog(LogLevel::Debug,
              "Removed " + std::to_string(removed) + " file" + (removed == 1 ? "" : "s") +
                  " of the stopped run that state.bin does not name:" + leftovers +
                  (removed > 8 ? " …" : "") + ".");
    }
    if (!convertOlder(opened)) {
      return fail(resumeWhy_);
    }
    const auto visitedFrom = std::chrono::steady_clock::now();
    for (const Side* side : sides) {
      for (const SetPtr<Key>& set : side->visited) {
        (void)set->reload();
      }
    }
    VisitedEnd restored = restoreVisited(fwd_);
    if (restored == VisitedEnd::Whole) {
      restored = restoreVisited(bwd_);
    }
    restoreVisitedTime_ = std::chrono::steady_clock::now() - visitedFrom;
    if (restored == VisitedEnd::Stopped) {
      // The saved search stays as it is: every file state.bin names is
      // kept, the ones a merge here took out of the visited sets too
      // (`opened` holds them all); the merged sets are not among them.
      for (auto& [name, set] : opened) {
        set->keepFile();
      }
      return Resumed::Stopped;
    }
    if (restored == VisitedEnd::Failed) {
      // a level that waited to go into them is written there
      return fail(store_.failure == SlideOutcome::Disk
                      ? "its visited sets could not be written (the disk may be full)"
                      : "its visited sets could not be read back");
    }
    tellLog(LogLevel::Debug, "The visited sets of the saved search were read back in " +
                                 secondsText(restoreVisitedTime_) + ".");
    // chosen once the saved sets are open, so that they count as its own
    setDiskBudget();
    // what the state names stays whole until the first save of this run
    // (a search that only saves when it stops lets its levels go as ever)
    if (search_.autosave) {
      pin();
      // and the sets restoreVisited merged away, which the state names too
      for (const auto& [name, set] : opened) {
        if (std::ranges::find(pinned_, set) == pinned_.end()) {
          pinned_.push_back(set);
        }
      }
    }
    return Resumed::Yes;
  }

  // The files of a search saved by an earlier version stand as they were
  // written, not compressed. They are read so, but each is written again
  // here as this version writes its files, under its own name, so that the
  // state stays true at every moment and a carry-on cut short goes on with
  // the files that are left: the smallest first, for the room each gives
  // back, several at once. One that cannot be written again (a full disk)
  // stays, read as it is. False only when a file written again could not be
  // put in its place or opened (resumeWhy_ says which).
  bool convertOlder(const std::map<std::string, SetPtr<Key>>& opened) {
    std::vector<SetPtr<Key>> older;
    std::uint64_t total = 0;
    for (const auto& [name, set] : opened) {
      if (set->older()) {
        older.push_back(set);
        total += set->bytes();
      }
    }
    if (older.empty()) {
      return true;
    }
    std::ranges::sort(older, [](const SetPtr<Key>& a, const SetPtr<Key>& b) {
      return a->bytes() != b->bytes() ? a->bytes() < b->bytes() : a->path() < b->path();
    });
    const auto began = std::chrono::steady_clock::now();
    tellLog(LogLevel::Info,
            "The saved search in " + folder_.dir.string() +
                " was written by an earlier version of the program, which did not compress "
                "its files: " +
                std::to_string(older.size()) + (older.size() == 1 ? " file" : " files") + ", " +
                bytesText(total) +
                ", are written again compressed before the search goes on. This is done "
                "once.");
    std::atomic<std::size_t> next{0};
    std::atomic<std::uint64_t> read{0};
    std::atomic<std::uint64_t> before{0};
    std::atomic<std::uint64_t> after{0};
    std::atomic<unsigned int> done{0};
    std::atomic<unsigned int> left{0};
    std::atomic<bool> full{false};
    std::atomic<bool> broken{false};
    std::mutex whyMutex;
    std::string why;
    pool_->run(threads_, [&](unsigned int th) {
      auto told = std::chrono::steady_clock::now();
      for (;;) {
        const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
        if (i >= older.size()) {
          return;
        }
        const SetPtr<Key>& set = older[i];
        if (full.load(std::memory_order_relaxed) || broken.load(std::memory_order_relaxed) ||
            search_.stop.stopRequested()) {
          left.fetch_add(1, std::memory_order_relaxed);
          continue;
        }
        const std::filesystem::path path = set->path();
        const std::uint64_t was = set->bytes();
        std::filesystem::path fresh = path;
        fresh += ".new";
        std::uint64_t counted = 0;
        Set made;
        const bool written =
            made.create(fresh, store_.fileOptions()) &&
            made.appendOlder(*set,
                             [&](std::uint64_t bytes) {
                               counted += bytes;
                               const std::uint64_t now =
                                   read.fetch_add(bytes, std::memory_order_relaxed) + bytes;
                               // one thread tells how far it is, now and then
                               if (th == 0 && std::chrono::steady_clock::now() - told >
                                                  std::chrono::seconds(30)) {
                                 told = std::chrono::steady_clock::now();
                                 tellLog(LogLevel::Info,
                                         "Written again compressed: " + bytesText(now) + " of " +
                                             bytesText(total) + " of the saved search's files.");
                               }
                               return !search_.stop.stopRequested() &&
                                      !broken.load(std::memory_order_relaxed);
                             }) &&
            made.finish();
        if (!written) {
          // stopped, or the disk is full: the file stays as it was
          made.remove();
          read.fetch_sub(counted, std::memory_order_relaxed);
          left.fetch_add(1, std::memory_order_relaxed);
          if (!search_.stop.stopRequested()) {
            full.store(true, std::memory_order_relaxed);
          }
          continue;
        }
        const std::uint64_t bytes = made.bytes();
        // both files closed, then the new one under the old one's name
        made.keepFile();
        set->keepFile();
        std::error_code ec;
        std::filesystem::rename(fresh, path, ec);
        const bool placed = !ec;
        if (!placed) {
          std::filesystem::remove(fresh, ec);
        }
        if (!set->open(path)) {
          const std::lock_guard<std::mutex> lock(whyMutex);
          why = path.filename().string() + " could not be opened after it was written again";
          broken.store(true, std::memory_order_relaxed);
          continue;
        }
        store_.refresh(set);
        if (placed) {
          before.fetch_add(was, std::memory_order_relaxed);
          after.fetch_add(bytes, std::memory_order_relaxed);
          done.fetch_add(1, std::memory_order_relaxed);
        } else {
          left.fetch_add(1, std::memory_order_relaxed);
          full.store(true, std::memory_order_relaxed);
        }
      }
    });
    if (broken.load()) {
      resumeWhy_ = why;
      return false;
    }
    search_.convertedFiles = done.load();
    search_.unconvertedFiles = left.load();
    if (done.load() > 0) {
      tellLog(LogLevel::Info,
              "Written again compressed: " + std::to_string(done.load()) +
                  (done.load() == 1 ? " file" : " files") + " of the saved search, " +
                  bytesText(before.load()) + " before and " + bytesText(after.load()) +
                  " now, in " + secondsText(std::chrono::steady_clock::now() - began) + ".");
    }
    if (left.load() > 0) {
      tellLog(search_.stop.stopRequested() ? LogLevel::Info : LogLevel::Warning,
              std::to_string(left.load()) + (left.load() == 1 ? " file" : " files") +
                  " of the saved search " + (left.load() == 1 ? "stays" : "stay") +
                  " as the earlier version wrote " + (left.load() == 1 ? "it" : "them") +
                  (search_.stop.stopRequested()
                       ? ": the search was stopped. The next carry-on goes on with them."
                       : ": there was no room on the disk to write " +
                             std::string(left.load() == 1 ? "it" : "them") +
                             " again. The search reads them as they are."));
    }
    return true;
  }

  // The visited sets of a search carried on are to hold every level older
  // than the two newest (ageOut), but a search saved between two levels
  // had one still waiting to go in, which the state does not name, and the
  // searches saved before the start went in lack that. A level is in whole
  // or not at all, so each kept level is looked for by its first
  // arrangement and goes in when it is not there: without it the one-way
  // moves that lead into it would be searched all over again.
  //
  // The merges are insertVisited's and hear the stop token as they do
  // while the search runs: Stopped leaves the sets valid, with the levels
  // put back so far, and the caller leaves the saved search as it is.
  VisitedEnd restoreVisited(Side& side) {
    if (!s_.nested) {
      return VisitedEnd::Whole;
    }
    for (const auto& [level, set] : side.stored) {
      if (level + 2 > side.depth || set->size() == 0) {
        continue;
      }
      if (!set->reload()) {
        return VisitedEnd::Failed;
      }
      const Key first = set->firstKey(0);
      char found = 0;
      for (const SetPtr<Key>& in : side.visited) {
        typename Set::Reader reader(*in, std::size_t{1} << 12);
        if (!in->reload() || !reader.ok() || !in->find(&first, 1, &found, reader)) {
          return VisitedEnd::Failed;
        }
      }
      if (found == 0) {
        if (const VisitedEnd end = insertVisited(set, side.visited); end != VisitedEnd::Whole) {
          return end;
        }
      } else if (set.use_count() == 1) {
        // only kept for tracing the path back: no index till then
        set->release();
        store_.refresh(set);
      }
    }
    return VisitedEnd::Whole;
  }

  // Every set the search stands on, by reference.
  template <class F>
  void forEachLiveSet(F f) {
    for (Side* side : {&fwd_, &bwd_}) {
      f(side->prev);
      f(side->cur);
      for (auto& [level, set] : side->stored) {
        f(set);
      }
      for (SetPtr<Key>& set : side->visited) {
        f(set);
      }
    }
  }

  // Writes the search's state to its folder, every set a file; false when
  // it cannot be.
  // `told`, when set, hears what the save writes (a stopped search's);
  // with `asPhase`, the search's phase does (an autosave between levels).
  bool saveState(SlideStopProgress* told = nullptr, bool asPhase = false) {
    if (!folder_.open()) {
      return false;
    }
    const auto began = std::chrono::steady_clock::now();
    SlidePhaseProgress* phase = asPhase && told == nullptr ? search_.phase : nullptr;
    if (told != nullptr || phase != nullptr) {
      // what is still in memory, each set once
      std::uint64_t total = 0;
      std::set<const Set*> seen;
      forEachLiveSet([&](SetPtr<Key>& set) {
        if (set && set->inMemory() && seen.insert(set.get()).second) {
          total += set->bytes();
        }
      });
      if (told != nullptr) {
        told->doneBytes.store(0, std::memory_order_relaxed);
        told->totalBytes.store(total, std::memory_order_relaxed);
        told->saving.store(true, std::memory_order_release);
      } else {
        phase->begin(SlidePhase::Saving, total);
      }
    }
    std::atomic<std::uint64_t>* counter = told != nullptr    ? &told->doneBytes
                                          : phase != nullptr ? &phase->done
                                                             : nullptr;
    bool ok = true;
    std::map<const Set*, SetPtr<Key>> written;
    forEachLiveSet([&](SetPtr<Key>& set) {
      if (!ok || !set) {
        return;
      }
      // a set shared between lists (a level kept and merged) is written once
      const auto it = written.find(set.get());
      if (it != written.end()) {
        set = it->second;
        return;
      }
      const Set* was = set.get();
      ok = store_.toFile(set, counter);
      written.emplace(was, set);
    });
    if (!ok) {
      return false;
    }
    SavedSearch st;
    st.problemKey = problemKey_;
    st.searchKey = searchKey_;
    st.visited = visited_;
    st.oneWayRepeats = search_.oneWayRepeats;
    st.interval = interval_;
    st.fileNumber = store_.fileNumber;
    st.nested = s_.nested;
    st.bidirectional = bidirectional_;
    auto nameOf = [](const SetPtr<Key>& set) {
      return set ? set->path().filename().string() : std::string();
    };
    Side* sides[2] = {&fwd_, &bwd_};
    for (int i = 0; i < 2; i++) {
      st.side[i].depth = sides[i]->depth;
      st.side[i].prev = nameOf(sides[i]->prev);
      st.side[i].cur = nameOf(sides[i]->cur);
      for (const auto& [level, set] : sides[i]->stored) {
        st.side[i].stored.emplace_back(level, nameOf(set));
      }
    }
    for (const SetPtr<Key>& set : fwd_.visited) {
      st.visitedSets.push_back(nameOf(set));
    }
    for (const SetPtr<Key>& set : bwd_.visited) {
      st.goalVisitedSets.push_back(nameOf(set));
    }
    if (!writeState(folder_.dir, st)) {
      tellLog(LogLevel::Debug, "The search's state could not be written in " + folder_.dir.string() + ".");
      return false;
    }
    tellLog(LogLevel::Debug, "Saved the search's state in " + folder_.dir.string() + " (" +
                                 secondsText(std::chrono::steady_clock::now() - began) + ", " +
                                 folderText(folder_.dir) + "): " + savedText(st) + ".");
    pin();
    return true;
  }

  // The files state.bin names stay until the next save replaces them,
  // whatever the search does with the sets meanwhile: a search that ends
  // badly is left as that save has it (run).
  void pin() {
    pinned_.clear();
    forEachLiveSet([&](SetPtr<Key>& set) {
      if (set) {
        pinned_.push_back(set);
      }
    });
    pinnedDeep_ = deepText();
  }

  // The disk the levels may take, chosen when the folder is made, from the
  // space free there (diskBudgetFor): a budget given is held to it, and
  // none given is half of it.
  void setDiskBudget() {
    if (!diskChosen_ && !folder_.dir.empty()) {
      // the files there already (a saved search carried on) are the
      // search's own: they count as free space it may take again
      const std::uint64_t own = store_.used().second;
      disk_ = diskBudgetFor(folder_.dir, search_.diskBudget, own);
      diskChosen_ = true;
      const std::string reason = diskBudgetReason(disk_);
      tellLog(LogLevel::Debug,
              "The disk the search may take in " + folder_.dir.string() + ": " +
                  bytesText(disk_.bytes) + (reason.empty() ? "" : " (" + reason + ")") +
                  (search_.diskBudget == 0 ? ", none asked for"
                                           : ", " + bytesText(search_.diskBudget) + " asked for") +
                  "; free there " + (disk_.free ? bytesText(*disk_.free) : "unknown") +
                  ", counting its own files " + bytesText(own) + ".");
    }
  }

  // A level cut short is searched again when the search is carried on: its
  // arrangements are not counted, here or in the progress.
  void uncount(std::uint64_t before) {
    const std::uint64_t now = visited_.exchange(before);
    if (search_.progress != nullptr) {
      search_.progress->fetch_sub(now - before, std::memory_order_relaxed);
    }
  }

  // A line for the application's log (SlideSearch::onLog).
  void tellLog(LogLevel level, const std::string& text) const {
    if (search_.onLog) {
      search_.onLog(level, text);
    }
  }

  // How far the search is, for the log.
  [[nodiscard]] std::string deepText() const {
    return std::to_string(fwd_.depth) +
           (bidirectional_ ? " + " + std::to_string(bwd_.depth) : "") + " moves deep, " +
           std::to_string(visited_.load()) + " arrangements visited";
  }

  // The outcome when a set could not be made.
  SlideOutcome failed() {
    search_.error = store_.error;
    const SlideOutcome why = store_.failure.load();
    return why == SlideOutcome::NoPath ? SlideOutcome::Memory : why;
  }

  // The outcome when the search could not save itself as it ran: a failed
  // write, whatever kept the state from being written.
  SlideOutcome saveFailed() {
    if (store_.failure == SlideOutcome::NoPath) {
      store_.ioFailed();
    }
    return failed();
  }

  // An abort was asked for: what the trace and the merges, which let a
  // pause wait, end for (cleanup item 18.4).
  [[nodiscard]] bool aborted() const { return search_.stop.kind() == StopKind::Abort; }

  // How a step that could not be made ends: an abort is the stop it is
  // (the merges end for one without marking the store failed), anything
  // else a failure.
  [[nodiscard]] StepEnd stepFailed() const {
    return aborted() ? StepEnd::Stopped : StepEnd::Failed;
  }

  // Expands `cur` on every thread, with the moves made backwards when
  // `backward` (the goal end's, with nested slides); `extra` runs on one
  // thread first (the merge of the level that left memory into the visited
  // sets). A stop halts it when `mayStop`; without, an abort alone does
  // (the trace across thinned levels: a pause lets the path be given).
  Expanded<Key> expand(Set& cur, bool checkGoal, bool mayStop,
                       const std::function<VisitedEnd()>& extra, bool backward) {
    Expanded<Key> e;
    if (!cur.reload()) {
      store_.ioFailed();
      e.failed = true;
      return e;
    }
    const std::uint64_t cap =
        std::max<std::uint64_t>(kPiece * sizeof(Key), store_.budget() / 2 / (threads_ + 1));
    std::vector<std::unique_ptr<RunBuilder<Key>>> back;
    std::vector<std::unique_ptr<RunBuilder<Key>>> oneWay;
    for (unsigned int i = 0; i <= threads_; i++) {
      back.push_back(std::make_unique<RunBuilder<Key>>(store_, cap));
      oneWay.push_back(std::make_unique<RunBuilder<Key>>(store_, cap));
    }
    std::atomic<std::size_t> nextBlock{0};
    std::atomic<bool> hit{false};
    std::atomic<bool> halt{false};
    std::atomic<bool> failed{false};
    const bool withExtra = static_cast<bool>(extra);
    // With the merge into the visited sets beside it: once the other
    // threads have expanded everything, the move waits for that merge,
    // visiting nothing, which is told (Visited).
    std::atomic<bool> extraRunning{withExtra};
    std::atomic<unsigned int> expanding{threads_};
    pool_->run(threads_ + (withExtra ? 1 : 0), [&](unsigned int th) {
      if (th == 0 && withExtra) {
        switch (extra()) {
          case VisitedEnd::Whole:
            break;
          case VisitedEnd::Stopped:
            // as a stop the expanding threads hear: the level is cut short
            // and expanded again when the search is carried on
            halt.store(true);
            break;
          case VisitedEnd::Failed:
            failed.store(true);
            break;
        }
        extraRunning.store(false);
      }
      RunBuilder<Key>& b = *back[th];
      RunBuilder<Key>& o = *oneWay[th];
      SlideMover<Key>& mover = *movers_[th];
      typename Set::Reader reader(cur);
      std::vector<Key> keys;
      bool localFail = false;
      auto emit = [&](Key raw, bool oneWay) {
        const Key k = s_.canon(raw);
        if (checkGoal && s_.isGoal(k)) {
          return true;
        }
        const bool ok = oneWay ? o.emit(k) : b.emit(k);
        if (!ok) {
          localFail = true;
        }
        return !ok;
      };
      const std::size_t blocks = cur.blocks();
      while (!hit.load(std::memory_order_relaxed) && !halt.load(std::memory_order_relaxed) &&
             !failed.load(std::memory_order_relaxed)) {
        const std::size_t b0 = nextBlock.fetch_add(kBlocksPerGrab);
        if (b0 >= blocks) {
          break;
        }
        const std::size_t b1 = std::min(b0 + kBlocksPerGrab, blocks);
        std::uint64_t n = 0;
        for (std::size_t blk = b0; blk < b1; blk++) {
          if (!reader.block(blk, keys)) {
            store_.ioFailed();
            failed.store(true);
            break;
          }
          if (search_.sample != nullptr && !keys.empty() && search_.sample->wanted()) {
            putSample(search_.sample, s_.decode(keys.front()));
          }
          for (const Key k : keys) {
            if (backward ? mover.movesBack(k, emit) : mover.moves(k, emit)) {
              if (localFail) {
                failed.store(true);
              } else {
                hit.store(true);
              }
              break;
            }
          }
          n += keys.size();
          if (hit.load(std::memory_order_relaxed) || failed.load(std::memory_order_relaxed)) {
            break;
          }
        }
        visited_.fetch_add(n);
        if (search_.progress != nullptr) {
          search_.progress->fetch_add(n, std::memory_order_relaxed);
        }
        if (mayStop ? search_.stop.stopRequested() : aborted()) {
          halt.store(true);
        }
      }
      if (!hit.load() && !halt.load() && !failed.load()) {
        if (!b.finishAll() || !o.finishAll()) {
          failed.store(true);
        }
      }
      if (withExtra && th != 0 && expanding.fetch_sub(1) == 1 && extraRunning.load() &&
          search_.phase != nullptr && !hit.load() && !halt.load() && !failed.load()) {
        search_.phase->begin(SlidePhase::Visited, 0);
      }
    });
    e.hit = hit.load();
    e.halted = halt.load();
    e.failed = failed.load();
    if (e.hit || e.halted || e.failed) {
      return e;
    }
    for (unsigned int i = 0; i <= threads_; i++) {
      for (SetPtr<Key>& r : back[i]->runs) {
        e.runs.push_back(std::move(r));
      }
      for (SetPtr<Key>& r : oneWay[i]->runs) {
        e.oneWay.push_back(std::move(r));
      }
    }
    return e;
  }

  // The level after side.cur into next: everything one move from it that is
  // in neither it nor side.prev (nor, by a one-way move, in any older
  // level of the side). With `intersect`, the first arrangement of next
  // also in it (met). oneWayReached and oneWayNew count the level's one-way
  // moves. The goal end of a search with nested slides takes the moves
  // backwards: its levels are the arrangements so many moves before the
  // goal. The merges end for an abort (Stopped), not for a pause: a level
  // expanded is merged, so that a pause does not throw its expansion away.
  StepEnd step(Side& side, const SetPtr<Key>& intersect, bool checkGoal, bool mayStop,
               const std::function<VisitedEnd()>& extra, SetPtr<Key>& next, std::optional<Key>& met,
               std::uint64_t& oneWayReached, std::uint64_t& oneWayNew) {
    Expanded<Key> e = expand(*side.cur, checkGoal, mayStop, extra, &side == &bwd_ && s_.nested);
    if (e.failed) {
      return StepEnd::Failed;
    }
    if (e.hit) {
      return StepEnd::Hit;
    }
    if (e.halted) {
      return StepEnd::Stopped;
    }
    oneWayReached = 0;
    oneWayNew = 0;
    std::vector<SetPtr<Key>> runs = std::move(e.runs);
    // Told from here to the level's end: the merges, which visit nothing.
    // The last merge takes the runs and what the one-way merge leaves of
    // its own, at most all of them.
    SlidePhaseProgress* tell = search_.phase;
    const std::uint64_t oneWayKeys = keysOf(e.oneWay);
    if (tell != nullptr) {
      tell->begin(SlidePhase::Merging, keysOf(runs) + oneWayKeys);
    }
    if (!e.oneWay.empty()) {
      // The arrangements reached only by moves that cannot be made
      // backwards may be anywhere in the older levels.
      Merge<Key> one;
      one.inputs = std::move(e.oneWay);
      one.subtract = {side.cur, side.prev};
      one.filter = &side.visited;
      one.tell = tell;
      one.abort = &search_.stop;
      if (!runMerge(one, store_, *pool_, threads_)) {
        return stepFailed();
      }
      oneWayReached = one.produced + one.dropped;
      oneWayNew = one.produced;
      if (tell != nullptr) {
        tell->total.fetch_sub(oneWayKeys - std::min(oneWayKeys, one.produced),
                              std::memory_order_relaxed);
      }
      for (SetPtr<Key>& p : one.parts) {
        runs.push_back(std::move(p));
      }
    }
    Merge<Key> m;
    m.inputs = std::move(runs);
    m.subtract = {side.cur, side.prev};
    m.intersect = intersect;
    m.tell = tell;
    m.counted = true;
    m.abort = &search_.stop;
    // with loose stops the level is what dropCovered leaves of the parts
    m.kept = !s_.loose;
    if (!runMerge(m, store_, *pool_, threads_)) {
      return stepFailed();
    }
    met = m.met;
    if (m.parts.empty()) {
      next = store_.make(0);
      if (!next || !store_.finish(next)) {
        return stepFailed();
      }
      return StepEnd::Next;
    }
    if (s_.loose && !dropCovered(side, m.parts)) {
      return stepFailed();
    }
    next = joinParts(m.parts, store_);
    return next ? StepEnd::Next : stepFailed();
  }

  // Loose stops: the parts of a new level without the arrangements that an
  // arrangement searched before, or another of the level, stands for
  // (SlideSpace::covers): every move from them is a move from that one.
  // Without this a piece left in place by a move that depended on it would
  // be searched in place all over again, beside the arrangements that have
  // it loose.
  bool dropCovered(Side& side, std::vector<SetPtr<Key>>& parts) {
    std::vector<SetPtr<Key>> in = side.visited;
    for (const SetPtr<Key>& set : {side.prev, side.cur}) {
      if (set && set->size() != 0) {
        in.push_back(set);
      }
    }
    in.insert(in.end(), parts.begin(), parts.end());
    for (const SetPtr<Key>& set : in) {
      if (!set->reload()) {
        store_.ioFailed();
        return false;
      }
    }
    std::vector<std::pair<std::size_t, std::size_t>> blocks;
    for (std::size_t p = 0; p < parts.size(); p++) {
      for (std::size_t b = 0; b < parts[p]->blocks(); b++) {
        blocks.emplace_back(p, b);
      }
    }
    const std::uint64_t cap =
        std::max<std::uint64_t>(kPiece * sizeof(Key), store_.budget() / 2 / (threads_ + 1));
    std::vector<std::unique_ptr<RunBuilder<Key>>> drop;
    for (unsigned int i = 0; i <= threads_; i++) {
      drop.push_back(std::make_unique<RunBuilder<Key>>(store_, cap));
    }
    std::atomic<std::size_t> nextBlock{0};
    std::atomic<bool> failed{false};
    pool_->run(threads_, [&](unsigned int th) {
      RunBuilder<Key>& out = *drop[th];
      std::vector<std::unique_ptr<typename Set::Reader>> readers;
      readers.reserve(in.size());
      for (const SetPtr<Key>& set : in) {
        readers.push_back(std::make_unique<typename Set::Reader>(*set, std::size_t{1} << 12));
      }
      std::vector<std::unique_ptr<typename Set::Reader>> own;
      own.reserve(parts.size());
      for (const SetPtr<Key>& part : parts) {
        own.push_back(std::make_unique<typename Set::Reader>(*part));
      }
      std::vector<Key> keys;
      std::vector<Key> over;
      std::vector<std::uint64_t> occ;
      // each arrangement that stands for one of keys, and which
      std::vector<std::pair<Key, std::size_t>> cand;
      std::vector<Key> look;
      std::vector<char> found;
      while (!failed.load(std::memory_order_relaxed)) {
        const std::size_t at = nextBlock.fetch_add(1);
        if (at >= blocks.size()) {
          break;
        }
        // an abort ends it as a failure does; step tells the two apart
        if (aborted()) {
          failed.store(true);
          break;
        }
        if (!own[blocks[at].first]->block(blocks[at].second, keys)) {
          store_.ioFailed();
          failed.store(true);
          break;
        }
        cand.clear();
        for (std::size_t k = 0; k < keys.size(); k++) {
          over.clear();
          s_.covers(keys[k], occ, over);
          for (const Key c : over) {
            cand.emplace_back(c, k);
          }
        }
        if (cand.empty()) {
          continue;
        }
        std::sort(cand.begin(), cand.end());
        look.clear();
        for (const std::pair<Key, std::size_t>& c : cand) {
          if (look.empty() || look.back() != c.first) {
            look.push_back(c.first);
          }
        }
        found.assign(look.size(), 0);
        for (std::size_t f = 0; f < in.size(); f++) {
          if (!readers[f]->ok() ||
              !in[f]->find(look.data(), look.size(), found.data(), *readers[f])) {
            store_.ioFailed();
            failed.store(true);
            break;
          }
        }
        std::size_t l = 0;
        for (const std::pair<Key, std::size_t>& c : cand) {
          if (look[l] != c.first) {
            l++;
          }
          if (found[l] != 0 && !out.emit(keys[c.second])) {
            failed.store(true);
            break;
          }
        }
      }
      if (!failed.load() && !out.finishAll()) {
        failed.store(true);
      }
    });
    if (failed.load()) {
      return false;
    }
    Merge<Key> covered;
    for (unsigned int i = 0; i <= threads_; i++) {
      for (SetPtr<Key>& r : drop[i]->runs) {
        covered.inputs.push_back(std::move(r));
      }
    }
    if (covered.inputs.empty()) {
      return true;
    }
    covered.tell = search_.phase;
    covered.abort = &search_.stop;
    if (!runMerge(covered, store_, *pool_, threads_)) {
      return false;
    }
    const SetPtr<Key> gone = joinParts(covered.parts, store_);
    if (!gone) {
      return false;
    }
    Merge<Key> rest;
    rest.inputs = std::move(parts);
    rest.subtract = {gone};
    rest.kept = true;
    rest.tell = search_.phase;
    rest.abort = &search_.stop;
    if (!runMerge(rest, store_, *pool_, threads_)) {
      return false;
    }
    parts = std::move(rest.parts);
    return true;
  }

  // The level that left the two newest: kept for tracing the path back
  // when it is one the interval keeps (every level with nested slides,
  // which also go into the visited sets), else freed. The start is kept
  // from the beginning (start).
  void ageOut(Side& side, const SetPtr<Key>& old, unsigned int level) {
    if (!old) {
      return;
    }
    if (s_.nested) {
      if (level != 0) {
        side.stored.emplace_back(level, old);
      }
      // the start too: a one-way move may lead back to it
      side.pending = old;
    } else if (level != 0 && level % interval_ == 0) {
      // only kept for tracing the path back: no index till then
      old->release();
      store_.refresh(old);
      side.stored.emplace_back(level, old);
    }
  }

  // The keys insertVisited goes through to put `newest` into `sets`: the
  // merges it makes, each of two sets whose levels are disjoint.
  static std::uint64_t visitedWork(const Set& newest, const std::vector<SetPtr<Key>>& sets) {
    std::vector<std::uint64_t> sizes;
    sizes.reserve(sets.size() + 1);
    for (const SetPtr<Key>& set : sets) {
      sizes.push_back(set->size());
    }
    sizes.push_back(newest.size());
    std::uint64_t work = 0;
    while (sizes.size() >= 2 && sizes[sizes.size() - 2] <= 2 * sizes.back()) {
      const std::uint64_t merged = sizes[sizes.size() - 2] + sizes.back();
      work += merged;
      sizes.pop_back();
      sizes.back() = merged;
    }
    return work;
  }

  // Merges a level into an end's visited sets: the newest sets while one is
  // no more than twice the size of the one after it, so there are only a
  // few to look in (runs on one thread while the others expand).
  //
  // The stop token ends a merge (cleanup item 18.4: Pause and Abort waited
  // for it, minutes deep in a search): the set being made is let go, and
  // the two it was made of stay in the list, as they do when a merge fails.
  // The level is in all the same (it is the list's last set, or part of a
  // merge that was whole), only the list is longer than its rule keeps it:
  // a search saved so is carried on with it, and the next merges put the
  // rule right. A level that was to go in after this one never did:
  // restoreVisited puts it back when the search is carried on.
  VisitedEnd insertVisited(const SetPtr<Key>& newest, std::vector<SetPtr<Key>>& sets) {
    sets.push_back(newest);
    const bool watched = static_cast<bool>(search_.onVisitedMerge);
    while (sets.size() >= 2 && sets[sets.size() - 2]->size() <= 2 * sets.back()->size()) {
      // the two stay in the list until their merge is whole: a search that
      // fails or is stopped here is saved with what it has (run)
      const SetPtr<Key> a = sets[sets.size() - 2];
      const SetPtr<Key> b = sets.back();
      if (!a->reload() || !b->reload()) {
        store_.ioFailed();
        return VisitedEnd::Failed;
      }
      // one thread merges them while the others expand: compressed fast
      SetPtr<Key> m = store_.make(a->rawBytes() + b->rawBytes(), false, false);
      if (!m) {
        return VisitedEnd::Failed;
      }
      typename Set::Cursor ca(*a);
      typename Set::Cursor cb(*b);
      Key ka = 0;
      Key kb = 0;
      bool ha = ca.next(ka);
      bool hb = cb.next(kb);
      bool ok = true;
      // told as visitedWork counts it: the keys of both
      std::atomic<std::uint64_t>* tell =
          search_.phase != nullptr ? &search_.phase->visitedDone : nullptr;
      const std::uint64_t work = a->size() + b->size();
      std::uint64_t told = 0;
      std::uint64_t added = 0;
      bool stopped = false;
      while (ok && (ha || hb)) {
        ++added;
        if (tell != nullptr && (added & (kTellEvery - 1)) == 0) {
          tell->fetch_add(added - told, std::memory_order_relaxed);
          told = added;
        }
        // at the first key (a stop asked for before the merge began ends it
        // at once), then now and then; the tests' callback sees every key
        if (watched) {
          search_.onVisitedMerge(added);
        }
        if ((watched || (added & (kVisitedPollEvery - 1)) == 1) &&
            search_.stop.stopRequested()) {
          stopped = true;
          break;
        }
        if (!hb || (ha && ka < kb)) {
          ok = m->add(ka);
          ha = ca.next(ka);
        } else if (!ha || kb < ka) {
          ok = m->add(kb);
          hb = cb.next(kb);
        } else {
          ok = m->add(ka);
          ha = ca.next(ka);
          hb = cb.next(kb);
        }
      }
      if (stopped) {
        // m goes here, with its file when it has one: nothing names it
        tellLog(LogLevel::Debug,
                "A merge into the visited sets was stopped after " + std::to_string(added) +
                    " of " + std::to_string(work) + " arrangements: the sets stay as they were.");
        return VisitedEnd::Stopped;
      }
      if (!ok || ca.failed() || cb.failed() || !store_.finish(m)) {
        store_.ioFailed();
        return VisitedEnd::Failed;
      }
      if (tell != nullptr) {
        tell->fetch_add(work - std::min(work, told), std::memory_order_relaxed);
      }
      sets.resize(sets.size() - 2);
      sets.push_back(m);
    }
    // a level only kept for tracing the path needs no index till then
    for (Side* side : {&fwd_, &bwd_}) {
      for (auto& [level, set] : side->stored) {
        if (set.use_count() == 1) {
          set->release();
          store_.refresh(set);
        }
      }
    }
    return VisitedEnd::Whole;
  }

  // Keeps every other level kept for tracing back (the gaps are searched
  // when the path is traced): level 0 is a multiple of any interval.
  void thin() {
    interval_ *= 2;
    for (Side* side : {&fwd_, &bwd_}) {
      std::erase_if(side->stored, [&](const std::pair<unsigned int, SetPtr<Key>>& e) {
        return e.first % interval_ != 0;
      });
    }
  }

  // Bytes the levels kept for tracing back take on disk.
  std::uint64_t storedDiskBytes() {
    std::uint64_t bytes = 0;
    for (Side* side : {&fwd_, &bwd_}) {
      for (const auto& [level, set] : side->stored) {
        if (!set->inMemory()) {
          bytes += set->bytes();
        }
      }
    }
    return bytes;
  }

  // Takes note of what the making of a level of `level` bytes on the disk
  // took there at its most (`work`, the level itself among it): the
  // greatest of the last few levels, for each byte of the level, is what
  // the next is taken to need. A search from both ends alternates, and its
  // ends differ; a level's runs are fewer the more of the level fits in
  // memory: so not the last level alone.
  void noteWork(std::uint64_t level, std::uint64_t work) {
    if (level < search_.measureLevelBytes || level == 0) {
      return;
    }
    workRatios_[workLevels_++ % workRatios_.size()] =
        static_cast<double>(work) / static_cast<double>(level);
    const std::size_t have = std::min<std::size_t>(workLevels_, workRatios_.size());
    workRatio_ = *std::max_element(workRatios_.begin(),
                                   workRatios_.begin() + static_cast<std::ptrdiff_t>(have));
    workMeasured_ = true;
  }

  // The disk the level after `next` may take as it is made: nothing
  // measured yet, kLevelWorkRatio times this level's bytes, as the search
  // always took it to be; else the measured ratio with its margin, at
  // least twice the level (the parts and their join), times the bytes the
  // level to come is expected to have, growing as this one grew on the
  // one before.
  std::uint64_t nextLevelNeed(const Side& side, const Set& next) const {
    const auto level = static_cast<double>(next.bytes());
    if (!workMeasured_) {
      return static_cast<std::uint64_t>(kLevelWorkRatio * level);
    }
    double growth = 1;
    if (side.prev && side.prev->size() != 0) {
      growth = std::clamp(
          static_cast<double>(next.size()) / static_cast<double>(side.prev->size()), 1.0, 4.0);
    }
    return static_cast<std::uint64_t>(std::max(2.0, workRatio_ * kLevelWorkMargin) * growth *
                                      level);
  }

  // The sets in memory not worked on, as evict would write them: their
  // bytes, the largest first, until the sets take half the memory they may.
  std::uint64_t evictBytes() {
    std::vector<std::uint64_t> sizes;
    std::set<const Set*> seen;
    for (Side* side : {&fwd_, &bwd_}) {
      for (auto& [level, set] : side->stored) {
        if (set->inMemory() && set != side->cur && set != side->prev &&
            seen.insert(set.get()).second) {
          sizes.push_back(set->bytes());
        }
      }
      for (SetPtr<Key>& set : side->visited) {
        if (set->inMemory() && seen.insert(set.get()).second) {
          sizes.push_back(set->bytes());
        }
      }
    }
    std::sort(sizes.begin(), sizes.end(), std::greater<>());
    std::uint64_t used = store_.used().first;
    std::uint64_t bytes = 0;
    for (const std::uint64_t b : sizes) {
      if (used <= store_.setCap() / 2) {
        break;
      }
      bytes += b;
      used -= std::min(used, b);
    }
    return bytes;
  }

  // In-memory sets no longer worked on are written to files while the
  // sets take more than half the memory they may, so that the next level
  // has room (at a level's end, on one thread).
  bool evict() {
    bool told = false;
    for (;;) {
      if (store_.used().first <= store_.setCap() / 2) {
        return true;
      }
      // nowhere to write: the sets stay where they are
      if (!folder_.open()) {
        return true;
      }
      // told once: the bytes over the half, as the largest sets go first
      if (search_.phase != nullptr && !told) {
        search_.phase->begin(SlidePhase::Writing, evictBytes());
        told = true;
      }
      SetPtr<Key>* largest = nullptr;
      for (Side* side : {&fwd_, &bwd_}) {
        for (auto& [level, set] : side->stored) {
          if (set->inMemory() && set != side->cur && set != side->prev &&
              (largest == nullptr || set->bytes() > (*largest)->bytes())) {
            largest = &set;
          }
        }
        for (SetPtr<Key>& set : side->visited) {
          if (set->inMemory() && (largest == nullptr || set->bytes() > (*largest)->bytes())) {
            largest = &set;
          }
        }
      }
      if (largest == nullptr) {
        return true;
      }
      const Set* was = largest->get();
      if (!store_.toFile(*largest, search_.phase != nullptr ? &search_.phase->done : nullptr)) {
        return false;
      }
      // the same set may stand in a stored list and in the visited sets
      forEachLiveSet([&](SetPtr<Key>& set) {
        if (set.get() == was) {
          set = *largest;
        }
      });
    }
  }

  // The main loop: a level at a time, from the narrower end.
  SlideOutcome loop(std::vector<Key>& chain) {
    const auto started = std::chrono::steady_clock::now();
    SaveCadence saves(std::chrono::seconds(search_.autosaveFloorSeconds), started);
    const std::uint64_t meetFrom = search_.meetFrom != 0 ? search_.meetFrom
                                   : s_.nested           ? kNestedMeetFrom
                                                         : kDefaultMeetFrom;
    const auto depthNow = [&]() { return std::uint64_t{fwd_.depth} + bwd_.depth; };
    if (search_.depthProgress != nullptr) {
      search_.depthProgress->store(depthNow(), std::memory_order_relaxed);
    }
    if (search_.onDepth) {
      search_.onDepth(depthNow());
    }
    for (;;) {
      // the move expanded next is told by the arrangements it visits
      if (search_.phase != nullptr) {
        search_.phase->end();
      }
      if (search_.stop.stopRequested()) {
        return SlideOutcome::Stopped;
      }
      Side& side = (bidirectional_ && bwd_.cur->size() < fwd_.cur->size()) ? bwd_ : fwd_;
      Side& other = (&side == &fwd_) ? bwd_ : fwd_;
      // the levels that left the two newest, either end's, into its
      // visited sets while this level is expanded
      std::function<VisitedEnd()> extra;
      if (fwd_.pending || bwd_.pending) {
        SetPtr<Key> ours;
        SetPtr<Key> theirs;
        ours.swap(fwd_.pending);
        theirs.swap(bwd_.pending);
        if (search_.phase != nullptr) {
          search_.phase->visitedDone.store(0, std::memory_order_relaxed);
          search_.phase->visitedTotal.store(
              (ours ? visitedWork(*ours, fwd_.visited) : 0) +
                  (theirs ? visitedWork(*theirs, bwd_.visited) : 0),
              std::memory_order_relaxed);
        }
        // stopped in the first, the second does not go in either: both
        // are levels kept (every level is, with nested slides), which
        // restoreVisited looks for when the search is carried on
        extra = [this, ours, theirs]() {
          VisitedEnd end = ours ? insertVisited(ours, fwd_.visited) : VisitedEnd::Whole;
          if (end == VisitedEnd::Whole && theirs) {
            end = insertVisited(theirs, bwd_.visited);
          }
          return end;
        };
      }
      SetPtr<Key> next;
      std::optional<Key> met;
      std::uint64_t oneWayReached = 0;
      std::uint64_t oneWayNew = 0;
      // a level cut short is expanded again when the search is carried on
      const std::uint64_t visitedBefore = visited_.load();
      const auto levelBegan = std::chrono::steady_clock::now();
      // what the files take before the level, and at their most from here
      (void)store_.takePeakDisk();
      const std::uint64_t diskBefore = store_.used().second;
      const StepEnd end = step(side, bidirectional_ ? other.cur : nullptr, !bidirectional_, true,
                               extra, next, met, oneWayReached, oneWayNew);
      if (end == StepEnd::Failed) {
        uncount(visitedBefore);
        return failed();
      }
      if (end == StepEnd::Stopped) {
        uncount(visitedBefore);
        return SlideOutcome::Stopped;
      }
      if (end == StepEnd::Hit) {
        switch (goalFound(chain)) {
          case GoalEnd::Found:
            return SlideOutcome::Found;
          case GoalEnd::Stopped:
            // as a stop while it was expanded: the level is expanded again
            uncount(visitedBefore);
            return SlideOutcome::Stopped;
          case GoalEnd::Failed:
            break;
        }
        // the path could not be read back: the level was not searched whole
        uncount(visitedBefore);
        return failed();
      }
      search_.oneWayRepeats += oneWayReached - oneWayNew;
      // prev <- cur <- next; the old prev leaves the newest two
      const SetPtr<Key> old = side.prev;
      side.prev = side.cur;
      side.cur = next;
      side.depth++;
      if (side.depth >= 2) {
        ageOut(side, old, side.depth - 2);
      }
      if (search_.depthProgress != nullptr) {
        search_.depthProgress->store(depthNow(), std::memory_order_relaxed);
      }
      if (search_.onDepth) {
        search_.onDepth(depthNow());
      }
      if (next->size() == 0) {
        // Everything this end can reach is searched without finding the
        // goal, or meeting the other end.
        return SlideOutcome::NoPath;
      }
      if (met) {
        if (meetingFound(*met, chain)) {
          return SlideOutcome::Found;
        }
        // an abort while the path was traced is the stop it is
        return aborted() ? SlideOutcome::Stopped : failed();
      }

      // The disk. Making a level takes far more than the level: its runs,
      // with everything the moves reach many times over, and the stages of
      // their merge. What this level took at its most, besides what the
      // search held before it, for each byte it came to, says what the
      // next will take (noteWork): that much must be free, in the budget
      // and on the disk, before the next is begun. When it is not, or the
      // levels kept for tracing back take half the budget, every other one
      // of those goes (not with nested slides, which need them all); then
      // what the search takes in all must fit the budget.
      std::pair<std::uint64_t, std::uint64_t> used = store_.used();
      const std::uint64_t peak = store_.takePeakDisk();
      search_.diskBytes = std::max(search_.diskBytes, peak);
      if (!folder_.dir.empty()) {
        setDiskBudget();
      }
      const std::uint64_t work = peak > diskBefore ? peak - diskBefore : 0;
      if (!next->inMemory()) {
        noteWork(next->bytes(), work);
      }
      const std::uint64_t need = nextLevelNeed(side, *next);
      unsigned int thinned = 0;
      if (diskChosen_ && !s_.nested) {
        const std::uint64_t level = next->bytes();
        const std::uint64_t storedBefore = storedDiskBytes();
        const std::optional<std::uint64_t> freeBefore = availableDiskBytes(folder_.dir);
        const auto tight = [&]() {
          const std::uint64_t stored = storedDiskBytes();
          if (stored > disk_.bytes / 2 || stored + need > disk_.bytes) {
            return true;
          }
          // the disk itself, with what the thinning so far gives back
          return freeBefore && *freeBefore + (storedBefore - stored) <
                                   search_.diskReserve + used.first + need;
        };
        while (interval_ < (1U << 30) && storedDiskBytes() > level && tight()) {
          thin();
          thinned++;
        }
        if (thinned > 0) {
          releaseFreeMemory();
          // The levels thinned out are files the last save names, which
          // stay until the next (pin): saved again now, so that the room
          // they take is there for the level to come.
          if (!pinned_.empty() && !saveState(nullptr, true)) {
            return saveFailed();
          }
          used = store_.used();
        }
      }
      const std::optional<std::uint64_t> freeNow =
          folder_.dir.empty() ? std::nullopt : availableDiskBytes(folder_.dir);
      {
        const Side& otherEnd = &side == &fwd_ ? bwd_ : fwd_;
        std::size_t kept = 0;
        std::size_t visitedSets = 0;
        for (const Side* s : {&fwd_, &bwd_}) {
          kept += s->stored.size();
          visitedSets += s->visited.size();
        }
        char line[512];
        std::snprintf(
            line, sizeof(line),
            "The %s end is %u moves deep (the %s end %u, %llu in all): the level has %zu "
            "arrangements, %llu visited in all; one-way %llu reached, %llu new; %zu levels kept "
            "(every %u), %zu visited sets; ",
            &side == &fwd_ ? "start" : "goal", side.depth, &side == &fwd_ ? "goal" : "start",
            otherEnd.depth, static_cast<unsigned long long>(depthNow()), next->size(),
            static_cast<unsigned long long>(visited_.load()),
            static_cast<unsigned long long>(oneWayReached),
            static_cast<unsigned long long>(oneWayNew), kept, interval_, visitedSets);
        char ratio[64];
        std::snprintf(ratio, sizeof(ratio), "%.1f", workRatio_);
        tellLog(LogLevel::Debug,
                std::string(line) + "disk " + bytesText(used.second) +
                    (diskChosen_ ? " of " + bytesText(disk_.bytes) : std::string()) + ", free " +
                    (freeNow ? bytesText(*freeNow) : std::string("unknown")) + ", memory " +
                    bytesText(used.first) + "; the level is " + bytesText(next->bytes()) +
                    (next->inMemory() ? " in memory" : " on the disk") + ", and making it took " +
                    bytesText(work) + " of the disk at its most; the next may take " +
                    bytesText(need) + " (" +
                    (workMeasured_ ? std::string(ratio) + " times a level's bytes at the most of "
                                                          "the last levels, and a margin"
                                   : "nothing measured yet") +
                    "); the level took " +
                    secondsText(std::chrono::steady_clock::now() - levelBegan) + ".");
      }
      if (thinned > 0) {
        tellLog(LogLevel::Debug,
                "The levels kept for tracing back were thinned out " + std::to_string(thinned) +
                    (thinned == 1 ? " time" : " times") +
                    " to leave room on the disk for the next level: every " +
                    std::to_string(interval_) + "th level is kept now, " +
                    bytesText(storedDiskBytes()) + " of them.");
      }
      if (diskChosen_ && used.second > disk_.bytes) {
        tellLog(LogLevel::Debug, "The search takes " + bytesText(used.second) +
                                     " on the disk, more than the " + bytesText(disk_.bytes) +
                                     " it may (the levels kept for tracing back " +
                                     bytesText(storedDiskBytes()) + ", the newest level " +
                                     bytesText(next->bytes()) + ").");
        char gb[32];
        std::snprintf(gb, sizeof(gb), "%.1f GB", static_cast<double>(disk_.bytes) / 1e9);
        const std::string reason = diskBudgetReason(disk_);
        search_.diskHeldToFree = disk_.heldToFree;
        search_.error = "The search filled the disk space it may use (" + std::string(gb) +
                        " in " + folder_.dir.parent_path().string() +
                        (reason.empty() ? "" : ", " + reason) + ").";
        return SlideOutcome::Disk;
      }
      // The disk itself: other programs take from it too, so the search
      // stops while there is still room to save itself (its sets in memory
      // written out) and to make the next level.
      if (diskChosen_) {
        const std::optional<std::uint64_t> free = availableDiskBytes(folder_.dir);
        const std::uint64_t want = search_.diskReserve + used.first + need;
        if (free && *free < want) {
          tellLog(LogLevel::Debug,
                  "The disk has " + bytesText(*free) + " free, less than the " + bytesText(want) +
                      " the search needs to save itself and make the next level (a reserve of " +
                      bytesText(search_.diskReserve) + ", " + bytesText(used.first) +
                      " in memory, and " + bytesText(need) + " for the level to come, whose "
                      "runs and merges take several times what the level comes to).");
          char gb[32];
          std::snprintf(gb, sizeof(gb), "%.1f GB", static_cast<double>(*free) / 1e9);
          search_.diskHeldToFree = true;
          search_.error = "The disk is nearly full (" + std::string(gb) + " free in " +
                          folder_.dir.parent_path().string() +
                          "): the search stopped while it could still save itself.";
          return SlideOutcome::Disk;
        }
      }
      if (!evict()) {
        return failed();
      }

      // From the goal too, once a level is wide and the goal is one
      // arrangement: the goal cannot be any nearer than the levels so far.
      // With nested slides only where the moves can be made backwards.
      if (!bidirectional_ && goal_ && !search_.oneEnd && (!s_.nested || s_.backMoves) &&
          fwd_.cur->size() >= meetFrom) {
        bwd_.cur = single(*goal_);
        if (!bwd_.cur) {
          return failed();
        }
        bwd_.stored.emplace_back(0, bwd_.cur);
        bidirectional_ = true;
        tellLog(LogLevel::Debug, "The search goes on from the goal too: the start end's level " +
                                     std::to_string(fwd_.depth) + " has " +
                                     std::to_string(fwd_.cur->size()) + " arrangements.");
      }

      if (search_.onLevel) {
        char line[320];
        std::snprintf(
            line, sizeof(line),
            "%s level %u: %zu arrangements, one-way %llu, %llu new, %zu sets, interval %u, "
            "disk %.1f MB, memory %.1f MB, %.1f s, made in %.1f MB",
            &side == &fwd_ ? "start" : "goal", side.depth, next->size(),
            static_cast<unsigned long long>(oneWayReached),
            static_cast<unsigned long long>(oneWayNew), side.visited.size(), interval_,
            static_cast<double>(used.second) / 1e6, static_cast<double>(used.first) / 1e6,
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(),
            static_cast<double>(work) / 1e6);
        search_.onLevel(line);
      }
      releaseFreeMemory();

      // A save point asked for: here, with a level just done and none
      // begun, the search stops and is saved (run) without redoing
      // anything when it is carried on. Only after a level of this run, so
      // that a run always gets further than it began.
      if (folder_.hasBase() && search_.savePoint.stopRequested()) {
        return SlideOutcome::Stopped;
      }

      // An autosave, so a crash loses no more than this.
      if (search_.autosave && folder_.hasBase() && (search_.autosaveEveryLevel || saves.due())) {
        const auto saveBegan = SaveCadence::Clock::now();
        if (!saveState(nullptr, true)) {
          return saveFailed();
        }
        saves.saved(saveBegan);
      }
    }
  }

  // The level `level` of a side, when the search still has it.
  SetPtr<Key> levelAt(const Side& side, unsigned int level) const {
    if (level == side.depth) {
      return side.cur;
    }
    if (level + 1 == side.depth) {
      return side.prev;
    }
    for (const auto& [l, set] : side.stored) {
      if (l == level) {
        return set;
      }
    }
    return nullptr;
  }

  // k with one loose piece at each stop of its class, and with one piece
  // that stands at a stop of a class loose in it: added to out.
  void looseVariants(Key k, std::vector<Key>& out) const {
    for (unsigned int i = 0; i < s_.n; i++) {
      const PosTable& t = s_.t[i];
      if (t.members.empty()) {
        continue;
      }
      const std::size_t p = idx(s_.posOf(k, i));
      if (p >= t.real) {
        for (const int stop : t.members[p - t.real]) {
          out.push_back(s_.canon(s_.with(k, i, stop)));
        }
      } else if (t.cls[p] >= 0) {
        out.push_back(s_.canon(s_.with(k, i, asInt(t.real) + t.cls[p])));
      }
    }
  }

  // An arrangement of `before` one move from x, the first in the order the
  // moves are tried, by moving backwards from x; failing that (x reached
  // only by moves that cannot be made backwards) the first arrangement of
  // `before` that reaches x. With towardGoal, `before` is a level of the
  // goal end of a search with nested slides, a move nearer the goal: the
  // arrangement is one that x's own moves lead to, one-way or not.
  bool stepBack(Key x, const SetPtr<Key>& before, Key& y, bool towardGoal) {
    if (!before->reload()) {
      store_.ioFailed();
      return false;
    }
    SlideMover<Key>& mover = *movers_[0];
    std::vector<Key> cand;
    mover.moves(x, [&](Key raw, bool oneWay) {
      if (!oneWay || s_.loose || towardGoal) {
        cand.push_back(s_.canon(raw));
      }
      return false;
    });
    if (s_.loose) {
      // A move told one way does not come back as it went: the arrangement
      // before may differ from what x's own moves give by a piece that is
      // loose in the one and in place in the other. Those are tried too,
      // and each one found is checked by its moves.
      const std::size_t told = cand.size();
      looseVariants(x, cand);
      for (std::size_t i = 0; i < told; i++) {
        looseVariants(cand[i], cand);
      }
    }
    std::vector<Key> sorted(cand);
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    std::vector<char> found(sorted.size(), 0);
    typename Set::Reader reader(*before, std::size_t{1} << 12);
    if (!reader.ok() || !before->find(sorted.data(), sorted.size(), found.data(), reader)) {
      store_.ioFailed();
      return false;
    }
    for (const Key c : cand) {
      const auto at = static_cast<std::size_t>(
          std::lower_bound(sorted.begin(), sorted.end(), c) - sorted.begin());
      if (found[at] == 0) {
        continue;
      }
      if (s_.loose && !mover.moves(c, [&](Key raw, bool) { return s_.canon(raw) == x; })) {
        found[at] = 0;
        continue;
      }
      y = c;
      return true;
    }
    // a level of the goal end is made of the arrangements that move into
    // the one before it
    SL_ASSERT(!towardGoal);
    // Try every arrangement of the level before, keeping the first.
    std::mutex bestMutex;
    std::optional<Key> best;
    std::atomic<bool> readFailed{false};
    std::atomic<bool> gaveUp{false};
    std::atomic<std::size_t> nextBlock{0};
    const std::size_t blocks = before->blocks();
    pool_->run(threads_, [&](unsigned int th) {
      SlideMover<Key>& m = *movers_[th];
      typename Set::Reader r(*before);
      std::vector<Key> keys;
      for (;;) {
        const std::size_t b = nextBlock.fetch_add(1);
        if (b >= blocks || readFailed.load()) {
          break;
        }
        // an abort: a block left out may hold the first, so none is given
        if (aborted()) {
          gaveUp.store(true);
          break;
        }
        {
          const std::lock_guard<std::mutex> lock(bestMutex);
          if (best && before->firstKey(b) > *best) {
            break;
          }
        }
        if (!r.block(b, keys)) {
          readFailed.store(true);
          break;
        }
        for (const Key k : keys) {
          if (m.moves(k, [&](Key raw, bool) { return s_.canon(raw) == x; })) {
            const std::lock_guard<std::mutex> lock(bestMutex);
            if (!best || k < *best) {
              best = k;
            }
            break;
          }
        }
      }
    });
    if (readFailed.load()) {
      store_.ioFailed();
      return false;
    }
    if (gaveUp.load()) {
      return false;
    }
    // there always is one; without it there is no path to give
    SL_ASSERT(best.has_value());
    y = *best;
    return true;
  }

  // The path from x, `level` moves from this end, back to the end: each
  // arrangement of the level before, through the levels kept, searching
  // out from x across the gaps where levels were thinned. False when it
  // could not be read back, and when an abort ended it (aborted(): the
  // search across a gap is as long as the levels it crosses were; a pause
  // waits for the path).
  bool traceSide(Side& side, Key x, unsigned int level, std::vector<Key>& chain) {
    const bool towardGoal = &side == &bwd_ && s_.nested;
    while (level > 0) {
      if (const SetPtr<Key> before = levelAt(side, level - 1)) {
        Key y = 0;
        if (!stepBack(x, before, y, towardGoal)) {
          return false;
        }
        chain.push_back(y);
        x = y;
        level--;
        continue;
      }
      // The nearest level kept below: a search out from x as many moves
      // as the gap is wide finds, at its last level, an arrangement of
      // that level; then back through the search's levels to x.
      unsigned int c = 0;
      for (const auto& [l, set] : side.stored) {
        if (l < level - 1 && l > c) {
          c = l;
        }
      }
      const SetPtr<Key> target = levelAt(side, c);
      SL_ASSERT(target != nullptr);
      const unsigned int r = level - c;
      // a search of its own, as long as the levels it crosses were: said
      // in a debug log (and where the tests stop a trace)
      tellLog(LogLevel::Debug,
              "The path is traced across " + std::to_string(r) +
                  " moves whose levels were thinned out (from level " + std::to_string(level) +
                  " back to level " + std::to_string(c) +
                  "): they are searched again, out from the arrangement found.");
      Side ball;
      ball.cur = single(x);
      if (!ball.cur) {
        return false;
      }
      std::vector<SetPtr<Key>> ballLevels{ball.cur};
      std::optional<Key> met;
      for (unsigned int i = 1; i <= r; i++) {
        SetPtr<Key> next;
        std::uint64_t reached = 0;
        std::uint64_t fresh = 0;
        const StepEnd end = step(ball, i == r ? target : nullptr, false, false, {}, next, met,
                                 reached, fresh);
        if (end != StepEnd::Next) {
          return false;
        }
        ball.prev = ball.cur;
        ball.cur = next;
        ball.depth++;
        ballLevels.push_back(next);
        if (search_.traced != nullptr) {
          search_.traced->fetch_add(1, std::memory_order_relaxed);
        }
      }
      SL_ASSERT(met.has_value());
      // from the meeting arrangement back through the search to x
      std::vector<Key> back;
      Key z = *met;
      for (unsigned int i = r - 1; i >= 1; i--) {
        Key y = 0;
        if (!stepBack(z, ballLevels[i], y, false)) {
          return false;
        }
        back.push_back(y);
        z = y;
      }
      chain.insert(chain.end(), back.rbegin(), back.rend());
      chain.push_back(*met);
      x = *met;
      level = c;
    }
    return true;
  }

  // How goalFound ended.
  enum class GoalEnd : std::uint8_t { Found, Stopped, Failed };

  // The goal is one move from the level being expanded: the first
  // arrangement of that level it is one move from (so that every run finds
  // the same path), then the path back from there. The threads stopped at
  // the first hit any of them made, so the level is gone through again, on
  // every thread: they take its blocks in order, the lowest arrangement
  // with a move to the goal is kept, and a thread ends at a block that
  // begins above it (as stepBack looks for an arrangement before). Told as
  // a part of the move (SlidePhase::Goal), and a stop is heard at each
  // block: the level is then expanded again when the search carries on.
  GoalEnd goalFound(std::vector<Key>& chain) {
    if (!fwd_.cur->reload()) {
      store_.ioFailed();
      return GoalEnd::Failed;
    }
    Set& level = *fwd_.cur;
    if (search_.phase != nullptr) {
      search_.phase->begin(SlidePhase::Goal, level.size());
    }
    std::mutex bestMutex;
    std::optional<Key> best;
    std::atomic<bool> readFailed{false};
    std::atomic<bool> halt{false};
    std::atomic<std::size_t> nextBlock{0};
    const std::size_t blocks = level.blocks();
    pool_->run(threads_, [&](unsigned int th) {
      SlideMover<Key>& m = *movers_[th];
      typename Set::Reader r(level);
      std::vector<Key> keys;
      for (;;) {
        const std::size_t b = nextBlock.fetch_add(1);
        if (b >= blocks || readFailed.load() || halt.load()) {
          break;
        }
        {
          const std::lock_guard<std::mutex> lock(bestMutex);
          if (best && level.firstKey(b) > *best) {
            break;
          }
        }
        if (!r.block(b, keys)) {
          readFailed.store(true);
          break;
        }
        std::uint64_t n = 0;
        for (const Key k : keys) {
          n++;
          if (m.moves(k, [&](Key raw, bool) { return s_.isGoal(s_.canon(raw)); })) {
            const std::lock_guard<std::mutex> lock(bestMutex);
            if (!best || k < *best) {
              best = k;
            }
            break;
          }
        }
        if (search_.phase != nullptr) {
          search_.phase->done.fetch_add(n, std::memory_order_relaxed);
        }
        if (search_.stop.stopRequested()) {
          halt.store(true);
        }
      }
    });
    if (search_.phase != nullptr) {
      search_.phase->end();
    }
    if (readFailed.load()) {
      store_.ioFailed();
      return GoalEnd::Failed;
    }
    // a block left out by a stop may hold a lower arrangement than the one
    // kept: no path is given from a level not gone through
    if (halt.load()) {
      return GoalEnd::Stopped;
    }
    // some thread found one while the level was expanded
    SL_ASSERT(best.has_value());
    const Key from = *best;
    // the goal it moves to: the first, in the order its moves are tried
    Key goalKey = 0;
    const bool reaches = movers_[0]->moves(from, [&](Key raw, bool) {
      const Key canon = s_.canon(raw);
      if (!s_.isGoal(canon)) {
        return false;
      }
      goalKey = canon;
      return true;
    });
    SL_ASSERT(reaches);
    chain.push_back(goalKey);
    chain.push_back(from);
    if (!traceSide(fwd_, from, fwd_.depth, chain)) {
      // no path is given: what the trace put in the chain so far is not one
      // (the caller takes a chain that is not empty for the path)
      chain.clear();
      return aborted() ? GoalEnd::Stopped : GoalEnd::Failed;
    }
    std::reverse(chain.begin(), chain.end());
    return GoalEnd::Found;
  }

  // The two ends met at m: the path from the start to m, then on to the
  // goal.
  bool meetingFound(Key m, std::vector<Key>& chain) {
    std::vector<Key> toStart{m};
    if (!traceSide(fwd_, m, fwd_.depth, toStart)) {
      return false;
    }
    std::vector<Key> toGoal{m};
    if (!traceSide(bwd_, m, bwd_.depth, toGoal)) {
      return false;
    }
    chain.assign(toStart.rbegin(), toStart.rend());
    chain.insert(chain.end(), toGoal.begin() + 1, toGoal.end());
    return true;
  }

  const SlideSpace<Key>& s_;
  SlideSearch& search_;
  std::uint64_t problemKey_;
  std::uint64_t searchKey_;
  /// How long resume() took over the visited sets (restoreVisited), for the log.
  std::chrono::steady_clock::duration restoreVisitedTime_{};
  SearchFolder folder_;
  SetStore<Key> store_;
  std::unique_ptr<ThreadPool> ownPool_;
  ThreadPool* pool_ = nullptr;
  unsigned int threads_ = 1;
  // One mover per thread, and one more for the thread that merges the
  // visited sets while the others expand.
  std::vector<std::unique_ptr<SlideMover<Key>>> movers_;
  std::optional<Key> goal_;
  Side fwd_;
  Side bwd_;
  bool bidirectional_ = false;
  unsigned int interval_ = 1;
  std::atomic<std::uint64_t> visited_{0};
  // The sets the last save named (see pin), and how far the search was.
  std::vector<SetPtr<Key>> pinned_;
  std::string pinnedDeep_;
  // The most disk the making of a level took, besides what the search held
  // before it, for each byte the level came to: the greatest of the last
  // few levels that went to the disk (kLevelWorkRatio until one has).
  double workRatio_ = kLevelWorkRatio;
  std::array<double, 4> workRatios_{};
  unsigned int workLevels_ = 0;
  bool workMeasured_ = false;
  // Why the saved search could not be carried on (resume), for the log.
  std::string resumeWhy_;
  DiskBudget disk_;
  bool diskChosen_ = false;
};

// Mixed into the name of a search with loose stops, of one whose keys hold
// codes for the stops only, and of one whose keys keep the copies of a
// piece as one number (tableSearch).
inline constexpr unsigned int kLooseFormat = 0x4c53;
inline constexpr unsigned int kCompactFormat = 0x434b;
inline constexpr unsigned int kPackedFormat = 0x504b;

// What tableSearch did: searched; nothing, because the key cannot hold the
// search (or with savedOnly none is saved), which a wider key may; nothing,
// because the search asked for is not one these tables can make in any key.
enum class Tabled { Searched, NoFit, NotThis };

// The tables of a search and what it goes by.
template <class Key>
struct NamedSpace {
  SlideSpace<Key> space;
  std::uint64_t problemKey = 0;
  // Names the search: the folder of a saved one.
  std::uint64_t name = 0;
};

// Which tables a search is made with (SlideSpace::build): the stops of a
// grid cell as one, a code for every position, the copies of a piece as one
// number (eachKey says when); and whether only a search that can go from
// the goal as well will do (nested slides: see findSlidePath).
struct TableForm {
  bool loose = false;
  bool plainKeys = false;
  bool pack = false;
  bool bothEnds = false;
};

// What a search works on, as nameSpace and its callers pass it about.
struct SlideBoard {
  const Puzzle& puzzle;
  const Problem& problem;
  const Voxel& tray;
  const std::vector<Cell3List>& cells;
  const std::vector<Cell3List>& outlines;
  // What of the options names the search: all that nameSpace sees of them.
  const SlideNaming& naming;
  const SlideState& initial;
  const GoalCheck& goals;
  const StopGrid& stops;
};

// Builds the tables of a search and names it. `form` chooses the tables.
// Nothing when they are made, else why not (NoFit or NotThis). The options
// come as the board's SlideNaming and in no other way (cleanup item 19.1):
// what an option adds to a name here, Continue's refusal compares.
template <class Key>
std::optional<Tabled> nameSpace(const SlideBoard& b, const TableForm& form, NamedSpace<Key>& out) {
  const SlideNaming& naming = b.naming;
  SlideSpace<Key>& space = out.space;
  if (!space.build(b.tray, b.cells, b.outlines, naming.nested, naming.partialNested, b.initial,
                   b.goals, naming.likePiecesApart, naming.verticalMoves, b.stops, form.loose,
                   form.plainKeys, form.pack)) {
    return Tabled::NoFit;
  }
  if (form.bothEnds && !(space.backMoves && space.singleGoal())) {
    return Tabled::NotThis;
  }

  // Names the search: the problem, the start and everything the search
  // depends on, so a saved search is carried on only by its own.
  out.problemKey = problemKeyOf(b.puzzle, b.problem);
  Fingerprint f;
  f.u64(out.problemKey);
  f.u32(static_cast<std::uint32_t>(sizeof(Key)));
  f.flag(naming.nested);
  f.flag(space.partial);
  f.flag(space.vertical);
  // without a grid what the coarse step of 1 mixed, so the searches saved
  // before the grid are carried on
  f.i32(b.stops.size > 1 ? b.stops.size : 1);
  if (b.stops.size > 1) {
    for (const std::vector<std::array<int, 2>>& v : b.stops.offsets) {
      f.u32(static_cast<std::uint32_t>(v.size()));
      for (const std::array<int, 2>& o : v) {
        f.i32(o[0]);
        f.i32(o[1]);
      }
    }
  }
  f.flag(naming.likePiecesApart);
  f.u32(space.n);
  f.key(space.startKey);
  for (unsigned int i = 0; i < space.n; i++) {
    for (const Cell3& c : b.cells[i]) {
      f.i32(c.x);
      f.i32(c.y);
      f.i32(c.z);
    }
    const Place& p = b.initial.places[i];
    f.i32(p.x);
    f.i32(p.y);
    f.i32(p.z);
    f.u8(p.trans);
    for (const char g : space.t[i].atGoal) {
      f.u8(static_cast<std::uint8_t>(g));
    }
  }
  // only with loose stops, and only with codes for the stops alone, so
  // the searches saved without are carried on (findSlidePath)
  if (space.loose) {
    f.u32(kLooseFormat);
  }
  if (space.compact) {
    f.u32(kCompactFormat);
  }
  if (space.packed) {
    f.u32(kPackedFormat);
  }
  out.name = f.value();
  return std::nullopt;
}

// The line for the application's log that names the search a start
// layout runs: the key its arrangements fit (eachKey) and the form of its
// tables.
inline std::string keyNote(unsigned int width, unsigned int bits, bool packed,
                           const TableForm& form) {
  std::string text = "The slide search runs in " + std::to_string(width) +
                     "-bit keys (its arrangements take " + std::to_string(bits) + " bits" +
                     (packed ? ", the copies of a piece kept as one number" : "") + ")";
  if (width == 64) {
    text += ": the narrowest key, an arrangement in one machine word";
  } else {
    text += ": an arrangement in " + std::to_string(width / 64) +
            " machine words, so it costs more to sort and to keep than in a narrower key, "
            "and fewer arrangements fit in the search memory";
  }
  if (form.bothEnds) {
    text += "; searched from the start and the goal at once";
  } else if (form.loose) {
    text += "; the stops inside a grid cell searched as one";
  }
  return text + ".";
}

// The slide search over position tables: a move is a flood fill over the
// piece's own positions against a bit mask of the cells the others cover.
// It goes level by level (LevelSearch). `form` chooses the search
// (nameSpace). With `savedOnly` only a saved search is carried on.
template <class Key>
Tabled tableSearch(const SlideBoard& b, const TableForm& form, bool savedOnly,
                   SlideSearch& search, std::vector<SlideState>& path) {
  NamedSpace<Key> named;
  if (const std::optional<Tabled> no = nameSpace(b, form, named)) {
    return *no;
  }
  const SlideSpace<Key>& space = named.space;
  if (savedOnly && !SearchFolder(search.workDir, named.name).exists()) {
    return Tabled::NoFit;
  }
  search.looseStops = space.loose;
  search.keyWidth = static_cast<unsigned int>(8 * sizeof(Key));
  search.keyBits = space.keyBits;
  search.packedKeys = space.packed;
  if (search.onLog) {
    search.onLog(LogLevel::Info, keyNote(search.keyWidth, space.keyBits, space.packed, form));
  }

  std::vector<Key> chain;
  LevelSearch<Key> level(space, search, named.problemKey, named.name);
  level.run(chain);
  if (!chain.empty()) {
    chainToPath(space, chain, path);
  }
  return Tabled::Searched;
}

// What tableSearch would do with the same arguments, nothing searched:
// Searched where it would search (with `savedOnly`, carry a saved search
// on).
template <class Key>
Tabled tableProbe(const SlideBoard& b, const TableForm& form, bool savedOnly,
                  const SlideSearch& search) {
  NamedSpace<Key> named;
  if (const std::optional<Tabled> no = nameSpace(b, form, named)) {
    return *no;
  }
  if (savedOnly && !SearchFolder(search.workDir, named.name).exists()) {
    return Tabled::NoFit;
  }
  return Tabled::Searched;
}

// The 128-bit key: the compiler's own where it has one.
#ifdef __SIZEOF_INT128__
using Key128 = WideKey;
#else
using Key128 = BigKey<2>;
#endif

// Each key's searches are compiled in its own file (sliding_search_<bits>.cpp).
extern template Tabled tableSearch<std::uint64_t>(const SlideBoard&, const TableForm&, bool,
                                            SlideSearch&, std::vector<SlideState>&);
extern template Tabled tableProbe<std::uint64_t>(const SlideBoard&, const TableForm&, bool,
                                           const SlideSearch&);
extern template std::optional<Tabled> nameSpace<std::uint64_t>(const SlideBoard&, const TableForm&,
                                                         NamedSpace<std::uint64_t>&);
extern template Tabled tableSearch<Key128>(const SlideBoard&, const TableForm&, bool,
                                            SlideSearch&, std::vector<SlideState>&);
extern template Tabled tableProbe<Key128>(const SlideBoard&, const TableForm&, bool,
                                           const SlideSearch&);
extern template std::optional<Tabled> nameSpace<Key128>(const SlideBoard&, const TableForm&,
                                                         NamedSpace<Key128>&);
extern template Tabled tableSearch<BigKey<3>>(const SlideBoard&, const TableForm&, bool,
                                            SlideSearch&, std::vector<SlideState>&);
extern template Tabled tableProbe<BigKey<3>>(const SlideBoard&, const TableForm&, bool,
                                           const SlideSearch&);
extern template std::optional<Tabled> nameSpace<BigKey<3>>(const SlideBoard&, const TableForm&,
                                                         NamedSpace<BigKey<3>>&);
extern template Tabled tableSearch<BigKey<4>>(const SlideBoard&, const TableForm&, bool,
                                            SlideSearch&, std::vector<SlideState>&);
extern template Tabled tableProbe<BigKey<4>>(const SlideBoard&, const TableForm&, bool,
                                           const SlideSearch&);
extern template std::optional<Tabled> nameSpace<BigKey<4>>(const SlideBoard&, const TableForm&,
                                                         NamedSpace<BigKey<4>>&);
extern template Tabled tableSearch<BigKey<8>>(const SlideBoard&, const TableForm&, bool,
                                            SlideSearch&, std::vector<SlideState>&);
extern template Tabled tableProbe<BigKey<8>>(const SlideBoard&, const TableForm&, bool,
                                           const SlideSearch&);
extern template std::optional<Tabled> nameSpace<BigKey<8>>(const SlideBoard&, const TableForm&,
                                                         NamedSpace<BigKey<8>>&);

}  // namespace sl::sliding::detail
