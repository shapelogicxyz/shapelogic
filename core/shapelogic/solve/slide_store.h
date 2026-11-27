// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The slide search's sets and saved searches (internal to the slide
/// search, cleanup item 21.1): the search's folder and its name, SetStore
/// (every level, run and merged set, in memory or in a file), the merges
/// and RunBuilder; the state file, which slide_saved.cpp reads and writes.

#include "shapelogic/base/memory_share.h"
#include "shapelogic/base/thread_pool.h"
#include "shapelogic/solve/slide_levels.h"
#include "shapelogic/solve/sliding_search.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <utility>
#include <vector>

namespace bt {
class Problem;
class Puzzle;
}  // namespace bt

namespace bt::sliding::detail {

// Runs f(i) for every i in [0, n) on up to `threads` threads of the pool,
// each index once (the original's onThreads gave each index a thread of
// its own; the pool may lend fewer).
template <class F>
void forEachIndex(ThreadPool& pool, unsigned int threads, std::size_t n, F f) {
  std::atomic<std::size_t> next{0};
  pool.run(std::max(1U, std::min<unsigned int>(threads, static_cast<unsigned int>(n))),
           [&](unsigned int) {
             for (std::size_t i = next.fetch_add(1); i < n; i = next.fetch_add(1)) {
               f(i);
             }
           });
}

// Gives memory freed by a search level back to the system: the allocator
// keeps large blocks for reuse otherwise, and a level is large.
void releaseFreeMemory();

// Names a problem's saved searches: the problem's uid, the tray with its
// start and goal marks, and the pieces with their counts. Nothing of the
// start layout, so that the searches of every start of the problem are
// found together. The uid (decision 0044) keeps apart the searches of two
// problems alike in every cell; a problem without one (paused by a version
// before uids) is named as those versions named it.
std::uint64_t problemKeyOf(const Puzzle& puzzle, const Problem& problem);

// A saved search's folder in the work folder, by the search's name
// (nameSpace).
std::string folderName(std::uint64_t key);

// The folder of one search's files in the work folder, named after the
// search so that a saved search is found again: made the first time it is
// needed, removed with everything in it when the search ends unless the
// search is saved there (keep). With no work folder there is none.
class SearchFolder {
 public:
  SearchFolder(const std::string& workDir, std::uint64_t key) {
    if (!workDir.empty()) {
      base_ = workDir;
      name_ = folderName(key);
    }
  }
  ~SearchFolder() {
    if (!dir.empty() && !keep) {
      std::error_code ec;
      std::filesystem::remove_all(dir, ec);
    }
  }
  SearchFolder(const SearchFolder&) = delete;
  SearchFolder& operator=(const SearchFolder&) = delete;

  // Whether a folder of this search's name is there already (a saved search).
  [[nodiscard]] bool exists() const {
    std::error_code ec;
    return !base_.empty() && std::filesystem::is_directory(base_ / name_, ec);
  }
  [[nodiscard]] std::filesystem::path place() const { return base_ / name_; }
  [[nodiscard]] bool hasBase() const { return !base_.empty(); }

  // Makes the folder the first time it is needed; false when it cannot be.
  bool open() {
    if (!dir.empty()) {
      return true;
    }
    if (tried_ || base_.empty()) {
      return false;
    }
    tried_ = true;
    std::error_code ec;
    std::filesystem::create_directories(base_ / name_, ec);
    if (std::filesystem::is_directory(base_ / name_, ec)) {
      dir = base_ / name_;
    }
    return !dir.empty();
  }

  // Empties the folder (a saved search that cannot be carried on).
  void wipe() {
    std::error_code ec;
    std::filesystem::remove_all(base_ / name_, ec);
    dir.clear();
    tried_ = false;
  }

  // Empty till open() succeeds.
  std::filesystem::path dir;
  bool keep = false;

 private:
  std::filesystem::path base_;
  std::string name_;
  bool tried_ = false;
};

// ---------------------------------------------------------------------------
// The level search's sets: every level, run and merged set of the search is
// a sorted KeySet, in memory while the memory budget allows and in a file
// of the search's folder otherwise. The store decides where each new set
// goes and keeps count of what the live sets take.

template <class Key>
using SetPtr = std::shared_ptr<levels::KeySet<Key>>;

template <class Key>
class SetStore {
 public:
  using Set = levels::KeySet<Key>;

  // budget: the memory the sets and the threads' pieces may take together;
  // setCap: the memory the sets alone may take before they go to files (0:
  // the budget). With `share`, the budget is the search's share of it,
  // read each time it is asked for (decision 0028). `files` says how the
  // sets kept are written; the runs of a level being made get the fastest
  // compression there is, when the files are compressed at all.
  SetStore(SearchFolder& folder, std::uint64_t budget, std::uint64_t setCap,
           const MemoryShare* share, const levels::FileOptions& files)
      : folder_(folder), budget_(budget), setCap_(setCap), share_(share), files_(files) {}

  // The tests' full disk (SlideSearch::failWriteAt).
  void failWritesAt(std::uint64_t bytes, bool once) {
    gate_.failAt = bytes;
    gate_.once = once;
    files_.gate = bytes != 0 ? &gate_ : nullptr;
  }

  // Bytes the live sets take in memory, and on disk.
  std::pair<std::uint64_t, std::uint64_t> used() {
    const std::lock_guard<std::mutex> lock(m_);
    return usedLocked();
  }

  // A new set for about `estimate` bytes of keys: in memory when the budget
  // allows, else in a file (always a file with `file`). Null when neither
  // can be; failure says why. `kept` false for a set read once and deleted
  // (a run, a stage of a merge) and for one a single thread writes while
  // the search waits for it: its file is compressed fast, not well.
  SetPtr<Key> make(std::uint64_t estimate, bool file = false, bool kept = true) {
    const std::lock_guard<std::mutex> lock(m_);
    const std::uint64_t mem = usedLocked().first;
    const std::uint64_t held = mem + pieces.load(std::memory_order_relaxed) + estimate;
    const std::uint64_t budgetNow = budget();
    bool toMemory = false;
    if (!file && mem + estimate <= setCap() && held <= budgetNow) {
      toMemory = true;
    } else if (folder_.open()) {
      toMemory = false;
    } else if (!file && held <= budgetNow) {
      // nowhere to write: memory until the budget
      toMemory = true;
    } else {
      failure = SlideOutcome::Memory;
      return nullptr;
    }
    std::filesystem::path p;
    if (!toMemory) {
      char name[32];
      std::snprintf(name, sizeof(name), "%06u.keys", fileNumber++);
      p = folder_.dir / name;
    }
    auto set = std::make_shared<Set>();
    levels::FileOptions options = files_;
    if (!kept) {
      options.compression = std::min(options.compression, levels::kRunCompression);
    }
    if (!set->create(p, options)) {
      ioFailed();
      return nullptr;
    }
    // counted as its estimate until it is finished
    live_.push_back({set, toMemory ? estimate : 0, 0});
    return set;
  }

  // How the files of the sets kept are written.
  [[nodiscard]] const levels::FileOptions& fileOptions() const { return files_; }

  // Takes in a set opened from a saved search.
  void adopt(const SetPtr<Key>& set) {
    const std::lock_guard<std::mutex> lock(m_);
    live_.push_back({set, set->memoryBytes(), set->bytes()});
  }

  // Finishes a set this store made and takes note of what it came to. The
  // store reads a set only here and in refresh, from the thread that owns
  // it: the other threads' sets are being written meanwhile.
  bool finish(const SetPtr<Key>& set) {
    if (!set->finish()) {
      ioFailed();
      return false;
    }
    refresh(set);
    return true;
  }

  // Takes note of a finished set's size again (after release or reload).
  // The most the files took is kept (peakDisk): a set counts from here on,
  // and the sets it was made from are still there.
  void refresh(const SetPtr<Key>& set) {
    const std::lock_guard<std::mutex> lock(m_);
    bool grew = false;
    for (Entry& e : live_) {
      if (e.set.lock() == set) {
        e.memory = set->memoryBytes();
        const std::uint64_t disk = set->inMemory() ? 0 : set->bytes();
        grew = disk > e.disk;
        e.disk = disk;
        break;
      }
    }
    if (grew) {
      peakDisk_ = std::max(peakDisk_, usedLocked().second);
    }
  }

  // The most the finished files took since the last call, and at least
  // what they take now: what a level needed on the disk as it was made.
  std::uint64_t takePeakDisk() {
    const std::lock_guard<std::mutex> lock(m_);
    const std::uint64_t now = usedLocked().second;
    const std::uint64_t peak = std::max(peakDisk_, now);
    peakDisk_ = now;
    return peak;
  }

  // The set written to a file instead (a saved search keeps only files);
  // false when it cannot be. `written`, when set, counts its bytes as they
  // go, by the share of its keys copied. One thread writes it while the
  // search waits (a level leaving memory, a pause, a save), so its file is
  // compressed fast, not well: of a long search little was ever in memory.
  bool toFile(SetPtr<Key>& set, std::atomic<std::uint64_t>* written = nullptr) {
    if (!set || !set->inMemory()) {
      return true;
    }
    const std::uint64_t bytes = set->bytes();
    const double perKey =
        static_cast<double>(bytes) / static_cast<double>(std::max<std::size_t>(set->size(), 1));
    std::uint64_t copied = 0;
    std::uint64_t told = 0;
    SetPtr<Key> copy = make(bytes, true, false);
    if (!copy) {
      return false;
    }
    typename Set::Cursor c(*set);
    Key k = 0;
    while (c.next(k)) {
      if (!copy->add(k)) {
        ioFailed();
        return false;
      }
      if (written != nullptr && (++copied & 0xFFFFU) == 0) {
        const auto now = std::min(bytes, static_cast<std::uint64_t>(perKey * static_cast<double>(copied)));
        written->fetch_add(now - told, std::memory_order_relaxed);
        told = now;
      }
    }
    if (c.failed() || !finish(copy)) {
      ioFailed();
      return false;
    }
    if (written != nullptr) {
      written->fetch_add(bytes - told, std::memory_order_relaxed);
    }
    set = copy;
    return true;
  }

  // The write failed: the disk is full, or the folder gone.
  void ioFailed() {
    const std::lock_guard<std::mutex> lock(errorMutex_);
    failure = SlideOutcome::Disk;
    error = folder_.dir.empty()
                ? std::string("The search could not keep its levels.")
                : "Could not write the search's files in " + folder_.dir.string() +
                      ". The disk may be full.";
  }

  [[nodiscard]] std::uint64_t budget() const {
    return share_ != nullptr ? std::max<std::uint64_t>(share_->share(), 1) : budget_;
  }
  [[nodiscard]] std::uint64_t setCap() const {
    const std::uint64_t b = budget();
    return setCap_ == 0 ? b : std::min(setCap_, b);
  }

  // Bytes of sorted pieces the expanding threads hold at the moment.
  std::atomic<std::uint64_t> pieces{0};
  // Numbers the files of the folder.
  unsigned int fileNumber = 0;
  // Why a set could not be made: Memory or Disk (NoPath for no failure).
  std::atomic<SlideOutcome> failure{SlideOutcome::NoPath};
  std::string error;

 private:
  // A live set and what it takes, as last noted.
  struct Entry {
    std::weak_ptr<Set> set;
    std::uint64_t memory = 0;
    std::uint64_t disk = 0;
  };

  std::pair<std::uint64_t, std::uint64_t> usedLocked() {
    std::uint64_t mem = 0;
    std::uint64_t disk = 0;
    std::size_t w = 0;
    for (std::size_t i = 0; i < live_.size(); i++) {
      if (live_[i].set.expired()) {
        continue;
      }
      live_[w++] = live_[i];
      mem += live_[i].memory;
      disk += live_[i].disk;
    }
    live_.resize(w);
    return {mem, disk};
  }

  SearchFolder& folder_;
  std::uint64_t budget_;
  std::uint64_t setCap_;
  const MemoryShare* share_;
  levels::FileOptions files_;
  levels::WriteGate gate_;
  std::mutex m_;
  std::mutex errorMutex_;
  std::vector<Entry> live_;
  std::uint64_t peakDisk_ = 0;
};

// Sets merged into one sorted stream, written as parts: a merge of
// `inputs` less what is in any of `subtract`; the first key also in
// `intersect` is reported (met); keys found in any of `filter` are dropped
// (the one-way arrangements found in the levels on disk). Each of the
// threads merges a range of keys, cut where the inputs' block index says,
// into a part of its own; the parts are sorted and disjoint, in order.
// The inputs are let go as soon as they are merged (a run's file goes with
// the last hold on it), so that a level's runs and the stages of its merge
// are not all on the disk at once. `kept` when the parts are to stay (a
// level of the search): their files are compressed well, not fast, by
// every thread at once.
template <class Key>
struct Merge {
  std::vector<SetPtr<Key>> inputs;
  std::vector<SetPtr<Key>> subtract;
  SetPtr<Key> intersect;
  const std::vector<SetPtr<Key>>* filter = nullptr;
  bool kept = false;
  // When set, the keys taken from the inputs are counted in its done (the
  // Solver's progress of the level being merged); unless `counted`, the
  // inputs' keys are added to its total first.
  SlidePhaseProgress* tell = nullptr;
  bool counted = false;
  // When set, the merge ends as soon as this token asks for an abort
  // (cleanup item 18.4): runMerge is then false with nothing of the store
  // marked failed, and what it made is let go. A pause is not heard: the
  // level's merge is finished, so that its expansion is not thrown away.
  const StopToken* abort = nullptr;

  std::vector<SetPtr<Key>> parts;
  std::optional<Key> met;
  std::uint64_t produced = 0;
  std::uint64_t dropped = 0;
};

// Inputs a merge takes at once: more are merged in stages first, so that a
// level of very many runs does not open them all together.
inline constexpr std::size_t kMaxMergeInputs = 32;
// Keys a merge thread looks up in the filter sets at a time.
inline constexpr std::size_t kFilterBatch = std::size_t{1} << 16;
// Keys a merge counts before it tells them (SlidePhaseProgress): often
// enough for the Solver's percentage, seldom enough to cost nothing. A
// level's merge looks for an abort as often.
inline constexpr std::uint64_t kTellEvery = std::uint64_t{1} << 16;
// Keys the merge into the visited sets takes between two looks at the stop
// token (cleanup item 18.4): one thread's merge, minutes long deep in a
// search, that Pause and Abort waited for.
inline constexpr std::uint64_t kVisitedPollEvery = std::uint64_t{1} << 12;

// The keys of sets.
template <class Key>
std::uint64_t keysOf(const std::vector<SetPtr<Key>>& sets) {
  std::uint64_t n = 0;
  for (const SetPtr<Key>& s : sets) {
    n += s ? s->size() : 0;
  }
  return n;
}

template <class Key>
bool runMerge(Merge<Key>& m, SetStore<Key>& store, ThreadPool& pool, unsigned int threads,
              unsigned int maxParts = 0) {
  using Set = levels::KeySet<Key>;
  // What is told: the keys of the last pass (the inputs as they are, until
  // stages make fewer of them), and those of every stage on the way
  if (m.tell != nullptr && !m.counted) {
    m.tell->total.fetch_add(keysOf(m.inputs), std::memory_order_relaxed);
  }
  // Too many inputs: merged in groups into a few parts each first, so that
  // few files are open at once.
  while (m.inputs.size() > kMaxMergeInputs) {
    const std::uint64_t before = keysOf(m.inputs);
    if (m.tell != nullptr) {
      m.tell->total.fetch_add(before, std::memory_order_relaxed);
    }
    std::vector<SetPtr<Key>> merged;
    for (std::size_t i = 0; i < m.inputs.size(); i += kMaxMergeInputs) {
      Merge<Key> stage;
      const std::size_t end = std::min(i + kMaxMergeInputs, m.inputs.size());
      stage.inputs.assign(m.inputs.begin() + static_cast<std::ptrdiff_t>(i),
                          m.inputs.begin() + static_cast<std::ptrdiff_t>(end));
      stage.tell = m.tell;
      stage.counted = true;
      stage.abort = m.abort;
      if (!runMerge(stage, store, pool, threads, kMaxMergeInputs / 4)) {
        return false;
      }
      merged.insert(merged.end(), stage.parts.begin(), stage.parts.end());
      // merged: the group's files go now, not with the whole stage
      for (std::size_t j = i; j < end; j++) {
        m.inputs[j].reset();
      }
    }
    m.inputs = std::move(merged);
    // the last pass takes what the stages left, fewer keys than they took
    if (m.tell != nullptr) {
      m.tell->total.fetch_sub(before - std::min(before, keysOf(m.inputs)),
                              std::memory_order_relaxed);
    }
  }
  std::uint64_t total = 0;
  std::uint64_t bytes = 0;
  std::uint64_t blocks = 0;
  for (const SetPtr<Key>& s : m.inputs) {
    if (!s->reload()) {
      store.ioFailed();
      return false;
    }
    total += s->size();
    bytes += s->rawBytes();
    blocks += s->blocks();
  }
  for (const SetPtr<Key>& s : m.subtract) {
    if (s && !s->reload()) {
      store.ioFailed();
      return false;
    }
  }
  if ((m.intersect && !m.intersect->reload())) {
    store.ioFailed();
    return false;
  }
  if (m.filter != nullptr) {
    for (const SetPtr<Key>& s : *m.filter) {
      if (!s->reload()) {
        store.ioFailed();
        return false;
      }
    }
  }
  if (total == 0) {
    m.inputs.clear();
    return true;
  }
  const unsigned int parts = static_cast<unsigned int>(std::clamp<std::uint64_t>(
      total / (std::uint64_t{1} << 16), 1, maxParts != 0 ? std::min(maxParts, threads) : threads));
  // The cut points: quantiles of a sample of the inputs' block index.
  std::vector<Key> cuts;
  if (parts > 1) {
    std::vector<Key> sample;
    const std::size_t stride = static_cast<std::size_t>(std::max<std::uint64_t>(1, blocks / 4096));
    for (const SetPtr<Key>& s : m.inputs) {
      for (std::size_t b = 0; b < s->blocks(); b += stride) {
        sample.push_back(s->firstKey(b));
      }
    }
    std::sort(sample.begin(), sample.end());
    for (unsigned int p = 1; p < parts; p++) {
      cuts.push_back(sample[sample.size() * p / parts]);
    }
  }

  std::vector<SetPtr<Key>> out(parts);
  std::vector<std::optional<Key>> met(parts);
  std::vector<std::uint64_t> produced(parts, 0);
  std::vector<std::uint64_t> dropped(parts, 0);
  std::atomic<bool> failed{false};
  std::atomic<bool> aborted{false};
  const std::uint64_t estimate = bytes / parts + 1;

  forEachIndex(pool, threads, parts, [&](std::size_t p) {
    if (aborted.load(std::memory_order_relaxed)) {
      return;
    }
    const bool hasLo = p > 0;
    const bool hasHi = p + 1 < parts;
    const Key lo = hasLo ? cuts[p - 1] : Key{0};
    const Key hi = hasHi ? cuts[p] : Key{0};
    auto inRange = [&](Key k) { return !hasHi || k < hi; };

    // One cursor per input, the smallest key on top.
    struct In {
      std::unique_ptr<typename Set::Cursor> c;
      Key k = 0;
    };
    std::vector<In> ins(m.inputs.size());
    using Item = std::pair<Key, std::size_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> heap;
    for (std::size_t i = 0; i < m.inputs.size(); i++) {
      ins[i].c = hasLo ? std::make_unique<typename Set::Cursor>(*m.inputs[i], lo)
                       : std::make_unique<typename Set::Cursor>(*m.inputs[i]);
      if (ins[i].c->next(ins[i].k) && inRange(ins[i].k)) {
        heap.emplace(ins[i].k, i);
      }
    }
    // The streams to drop keys of, and the one to meet.
    struct Stream {
      std::unique_ptr<typename Set::Cursor> c;
      Key k = 0;
      bool has = false;
      void start(const Set& s, bool seek, Key from) {
        c = seek ? std::make_unique<typename Set::Cursor>(s, from)
                 : std::make_unique<typename Set::Cursor>(s);
        has = c->next(k);
      }
      // Whether k is in the stream, the stream moved on to it.
      bool holds(Key key) {
        while (has && k < key) {
          has = c->next(k);
        }
        return has && k == key;
      }
    };
    std::vector<Stream> subs;
    for (const SetPtr<Key>& s : m.subtract) {
      if (s && s->size() != 0) {
        subs.emplace_back();
        subs.back().start(*s, hasLo, lo);
      }
    }
    Stream inter;
    if (m.intersect && m.intersect->size() != 0) {
      inter.start(*m.intersect, hasLo, lo);
    }

    SetPtr<Key> set = store.make(estimate, false, m.kept);
    if (!set) {
      failed.store(true);
      return;
    }
    // The filter: keys looked up in batches, so that each set is walked
    // once per batch.
    std::vector<std::unique_ptr<typename Set::Reader>> readers;
    if (m.filter != nullptr) {
      for (const SetPtr<Key>& s : *m.filter) {
        readers.push_back(std::make_unique<typename Set::Reader>(*s, std::size_t{1} << 12));
      }
    }
    std::vector<Key> batch;
    std::vector<char> found;
    auto flushBatch = [&]() {
      if (batch.empty()) {
        return true;
      }
      found.assign(batch.size(), 0);
      for (std::size_t f = 0; f < readers.size(); f++) {
        if (!readers[f]->ok() ||
            // a reader was made for every filter set above:
            // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
            !(*m.filter)[f]->find(batch.data(), batch.size(), found.data(), *readers[f])) {
          return false;
        }
      }
      for (std::size_t i = 0; i < batch.size(); i++) {
        if (found[i] != 0) {
          dropped[p]++;
        } else {
          produced[p]++;
          if (!set->add(batch[i])) {
            return false;
          }
        }
      }
      batch.clear();
      return true;
    };

    bool ok = true;
    bool haveLast = false;
    Key last = 0;
    // the keys taken, told now and then
    std::uint64_t taken = 0;
    while (ok && !heap.empty()) {
      const Item top = heap.top();
      heap.pop();
      if (++taken == kTellEvery) {
        if (m.tell != nullptr) {
          m.tell->done.fetch_add(taken, std::memory_order_relaxed);
        }
        taken = 0;
        if (aborted.load(std::memory_order_relaxed) ||
            (m.abort != nullptr && m.abort->kind() == StopKind::Abort)) {
          // the part made so far goes with `set`
          aborted.store(true, std::memory_order_relaxed);
          return;
        }
      }
      const std::size_t i = top.second;
      if (ins[i].c->next(ins[i].k) && inRange(ins[i].k)) {
        heap.emplace(ins[i].k, i);
      }
      const Key k = top.first;
      if (haveLast && k == last) {
        continue;
      }
      haveLast = true;
      last = k;
      bool drop = false;
      for (Stream& s : subs) {
        if (s.holds(k)) {
          drop = true;
          break;
        }
      }
      if (drop) {
        continue;
      }
      if (inter.c && !met[p] && inter.holds(k)) {
        met[p] = k;
      }
      if (m.filter != nullptr) {
        batch.push_back(k);
        if (batch.size() >= kFilterBatch) {
          ok = flushBatch();
        }
      } else {
        produced[p]++;
        ok = set->add(k);
      }
    }
    ok = ok && flushBatch();
    if (m.tell != nullptr) {
      m.tell->done.fetch_add(taken, std::memory_order_relaxed);
    }
    for (In& in : ins) {
      ok = ok && !in.c->failed();
    }
    for (Stream& s : subs) {
      ok = ok && !s.c->failed();
    }
    ok = ok && (!inter.c || !inter.c->failed()) && store.finish(set);
    if (!ok) {
      failed.store(true);
      return;
    }
    if (set->size() != 0) {
      set->release();
      store.refresh(set);
      out[p] = set;
    }
  });
  if (aborted.load()) {
    // no failure of the store's: the caller hears the abort from its token
    return false;
  }
  if (failed.load()) {
    if (store.failure == SlideOutcome::NoPath) {
      store.ioFailed();
    }
    return false;
  }
  for (unsigned int p = 0; p < parts; p++) {
    if (out[p]) {
      m.parts.push_back(out[p]);
    }
    if (met[p] && !m.met) {
      m.met = met[p];
    }
    m.produced += produced[p];
    m.dropped += dropped[p];
  }
  m.inputs.clear();
  return true;
}

// The parts of a merge joined into one set (the parts freed); an empty set
// for none. Null when it cannot be made. A file's chunks are copied as
// they are, compressed as the merge wrote them; parts in memory joined
// into a file are written here, by this one thread, so compressed fast.
template <class Key>
SetPtr<Key> joinParts(std::vector<SetPtr<Key>>& parts, SetStore<Key>& store) {
  if (parts.size() == 1) {
    SetPtr<Key> one = std::move(parts[0]);
    parts.clear();
    return one;
  }
  std::uint64_t bytes = 0;
  for (const SetPtr<Key>& p : parts) {
    bytes += p->rawBytes();
  }
  SetPtr<Key> joined = store.make(bytes, false, false);
  if (!joined) {
    return nullptr;
  }
  for (SetPtr<Key>& p : parts) {
    if (!p->reload() || !joined->appendSet(*p)) {
      store.ioFailed();
      return nullptr;
    }
    p.reset();
  }
  parts.clear();
  if (!store.finish(joined)) {
    return nullptr;
  }
  return joined;
}

// ---------------------------------------------------------------------------
// Expanding a level: every thread takes blocks of the level, works out the
// moves of their arrangements and gathers what they reach in sorted pieces;
// the pieces are merged into runs (sets) as they outgrow the thread's share
// of the memory, and the runs are merged into the next level after.

// Keys a thread gathers before sorting them.
inline constexpr std::size_t kPiece = std::size_t{1} << 16;
// Blocks a thread takes from the level at a time.
inline constexpr std::size_t kBlocksPerGrab = 8;
// Arrangements in a level from which the search goes on from the goal too
// (SlideSearch::meetFrom 0).
inline constexpr std::uint64_t kDefaultMeetFrom = 1000000;
// The same with nested slides, where the search from one end has the wider
// levels to fear: it keeps them all, and a grid's loose stops are given up
// for the search from both ends (findSlidePath).
inline constexpr std::uint64_t kNestedMeetFrom = 1;

template <class Key>
class RunBuilder {
 public:
  using Set = levels::KeySet<Key>;

  RunBuilder(SetStore<Key>& store, std::uint64_t cap) : store_(store), cap_(cap) {
    raw_.reserve(kPiece);
  }
  ~RunBuilder() { store_.pieces.fetch_sub(bytes_, std::memory_order_relaxed); }
  RunBuilder(const RunBuilder&) = delete;
  RunBuilder& operator=(const RunBuilder&) = delete;

  bool emit(Key k) {
    raw_.push_back(k);
    return raw_.size() < kPiece || settle();
  }

  // The last pieces merged into a run.
  bool finishAll() { return settle() && flush(); }

  std::vector<SetPtr<Key>> runs;

 private:
  // Sorts the raw keys into a piece; merges the pieces into a run when they
  // fill the thread's share.
  bool settle() {
    if (raw_.empty()) {
      return true;
    }
    std::sort(raw_.begin(), raw_.end());
    raw_.erase(std::unique(raw_.begin(), raw_.end()), raw_.end());
    const std::uint64_t bytes = std::uint64_t{raw_.size()} * sizeof(Key);
    pieces_.emplace_back(raw_.begin(), raw_.end());
    raw_.clear();
    bytes_ += bytes;
    store_.pieces.fetch_add(bytes, std::memory_order_relaxed);
    return bytes_ < cap_ || flush();
  }

  bool flush() {
    if (pieces_.empty()) {
      return true;
    }
    std::uint64_t keys = 0;
    for (const std::vector<Key>& p : pieces_) {
      keys += p.size();
    }
    SetPtr<Key> set = store_.make(keys * 4, false, false);
    bool ok = set != nullptr;
    if (ok && pieces_.size() == 1) {
      ok = set->addAll(pieces_[0].begin(), pieces_[0].end());
    } else if (ok) {
      using Item = std::pair<Key, std::size_t>;
      std::priority_queue<Item, std::vector<Item>, std::greater<>> heap;
      std::vector<std::size_t> at(pieces_.size(), 0);
      for (std::size_t i = 0; i < pieces_.size(); i++) {
        heap.emplace(pieces_[i][0], i);
      }
      bool haveLast = false;
      Key last = 0;
      while (ok && !heap.empty()) {
        const Item top = heap.top();
        heap.pop();
        const std::size_t i = top.second;
        if (++at[i] < pieces_[i].size()) {
          heap.emplace(pieces_[i][at[i]], i);
        }
        if (!haveLast || top.first != last) {
          ok = set->add(top.first);
          last = top.first;
          haveLast = true;
        }
      }
    }
    ok = ok && store_.finish(set);
    if (ok) {
      set->release();
      store_.refresh(set);
    }
    pieces_.clear();
    store_.pieces.fetch_sub(bytes_, std::memory_order_relaxed);
    bytes_ = 0;
    if (!ok) {
      if (store_.failure == SlideOutcome::NoPath) {
        store_.ioFailed();
      }
      return false;
    }
    if (set->size() != 0) {
      runs.push_back(set);
    }
    return true;
  }

  SetStore<Key>& store_;
  std::uint64_t cap_;
  std::vector<Key> raw_;
  std::vector<std::vector<Key>> pieces_;
  std::uint64_t bytes_ = 0;
};

// What expanding a level came to.
template <class Key>
struct Expanded {
  std::vector<SetPtr<Key>> runs;
  std::vector<SetPtr<Key>> oneWay;
  bool hit = false;
  bool halted = false;
  bool failed = false;
};

// ---------------------------------------------------------------------------
// The saved search: the levels it stands on, in the folder, and state.bin
// naming them. A search stopped before its end is saved when it has a
// folder; a search of the same start carries it on.

// One end of a saved search: its depth, its newest two levels and the
// levels it keeps, by file name.
struct SavedSide {
  unsigned int depth = 0;
  std::string prev;
  std::string cur;
  std::vector<std::pair<unsigned int, std::string>> stored;
};

// What state.bin holds: whose search it is, its counts and its ends.
struct SavedSearch {
  std::uint64_t problemKey = 0;
  std::uint64_t searchKey = 0;
  std::uint64_t visited = 0;
  std::uint64_t oneWayRepeats = 0;
  unsigned int interval = 1;
  unsigned int fileNumber = 0;
  bool nested = false;
  bool bidirectional = false;
  SavedSide side[2];
  // The start end's visited sets, and the goal end's (only a search from
  // both ends with nested slides has those).
  std::vector<std::string> visitedSets;
  std::vector<std::string> goalVisitedSets;
};

// Bytes for the log: "512.0 MB", "62.31 GB".
std::string bytesText(std::uint64_t bytes);

// Seconds for the log: "12.3 s".
std::string secondsText(std::chrono::steady_clock::duration d);

// What a saved state names, for the log's debug lines: each end's depth
// and the files of its levels, the visited sets, the counts.
std::string savedText(const SavedSearch& st);

// The files in a folder and their bytes, for the log.
std::string folderText(const std::filesystem::path& dir);

// Writes state.bin through a temporary file of its own, checked once it is
// closed and synced before it replaces the last good state (cleanup item
// 4.1: a write lost at the close left an empty state.bin in its place).
bool writeState(const std::filesystem::path& dir, const SavedSearch& st);

// Reads state.bin; with `whole` false only the keys, enough to tell whose
// search it is.
bool readState(const std::filesystem::path& dir, SavedSearch& st, bool whole);

}  // namespace bt::sliding::detail
