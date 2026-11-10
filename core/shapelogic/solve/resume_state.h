// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// What a stopped search split over threads, or a stopped vector search,
/// keeps to carry on, and how that goes into a saved position.
///
/// Each assembler writes its own state first and then this tail, which
/// after its leading flag is
///
///   flag 3:     " K <assemblies already reported>"
///   flag 2, 4:  " T <tasks>" <each task, as the assembler writes it>
///               " C <one 0 or 1 per task, finished or not>"
///               " S <count> <signature>..."
///               " W <weight of the tasks dropped> <weight>..."   (optional)
///
/// Up to the signatures the text is what the original wrote (decision
/// 0006); the weights are ShapeLogic's, for the progress of a search whose
/// tasks were split further while it ran (SplitTasks), and a reader that
/// does not know them stops before them.
/// Ported from src/lib/assembler_resume.h.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace bt {

/// The resumable state of a search that is split into tasks (decision 0009).
/// It lives in the assembler across runs and is what a saved position of a
/// parallel or vector search holds.
template <class Task>
struct SplitSearchState {
  /// The subtrees the search was split into; empty when not split.
  std::vector<Task> tasks;
  /// One flag per task: searched to its end.
  std::vector<std::uint8_t> completed;
  /// Each task's estimated part of the whole search, for the progress; empty
  /// when nothing is known (all alike). doneWeight is the part of the tasks
  /// that were finished and dropped from the list (prune()).
  std::vector<double> weights;
  double doneWeight = 0;
  /// The signatures of the assemblies reported so far, so that a task
  /// searched again after a stop reports each one once.
  std::unordered_set<std::uint64_t> reported;
  /// A stopped vector search starts again from the beginning and passes
  /// over the first `vectorSkip` assemblies, which it reported before; it
  /// counts the ones it reports in `vectorDone`.
  std::uint64_t vectorSkip = 0;
  std::uint64_t vectorDone = 0;
  /// The last run of a split or vector search stopped before its end.
  bool interrupted = false;
  /// The tasks never overlap, and every run left them exactly where it
  /// stood: no assembly is found twice, so none is looked up in `reported`
  /// and none recorded there (for a search with millions of assemblies the
  /// signatures cost more than the search). Set by the assembler when it
  /// makes such tasks; a search loaded from a file has it only when the
  /// file says so much.
  bool exact = false;

  /// Forget the split (the search is through, or starts over).
  void clearTasks() {
    tasks.clear();
    completed.clear();
    weights.clear();
    doneWeight = 0;
    reported.clear();
    exact = false;
  }

  /// Drops the finished tasks, keeping their weight in doneWeight: a search
  /// that hands branches of its tasks on as new tasks (SplitTasks) would
  /// otherwise carry every task it ever had. At least one task stays, so
  /// that the search is still known as a split one.
  void prune() {
    const bool weighted = weights.size() == tasks.size();
    std::size_t kept = 0;
    for (std::size_t i = 0; i < tasks.size(); i++) {
      if (completed[i] && (kept > 0 || i + 1 < tasks.size())) {
        doneWeight += weighted ? weights[i] : 1.0;
        continue;
      }
      if (kept != i) {
        tasks[kept] = std::move(tasks[i]);
        completed[kept] = completed[i];
        if (weighted) {
          weights[kept] = weights[i];
        }
      }
      kept++;
    }
    tasks.resize(kept);
    completed.resize(kept);
    if (weighted) {
      weights.resize(kept);
    } else {
      weights.clear();
    }
  }
};

/// The weights of a saved search are written as whole units of this many
/// to the whole.
inline constexpr double kWeightUnits = 1099511627776.0;  // 2^40

/// Numbers and markers read one after the other from a saved position.
class ResumeReader {
 public:
  explicit ResumeReader(const char* at) : s(at), last(at + std::strlen(at)) {}

  /// The characters left: a bound for any count read, since each counted
  /// item takes at least one (a damaged count would otherwise allocate what
  /// it says).
  [[nodiscard]] std::size_t remaining() const { return static_cast<std::size_t>(last - s); }

  /// The next number (strtoull: leading blanks are skipped); false when
  /// there is none.
  bool next(unsigned long long& v) {
    char* end = nullptr;
    v = std::strtoull(s, &end, 10);
    if (end == s) {
      return false;
    }
    s = end;
    return true;
  }

  bool next(unsigned int& v) {
    unsigned long long l = 0;
    if (!next(l)) {
      return false;
    }
    v = static_cast<unsigned int>(l);
    return true;
  }

  /// `c`, after any spaces.
  bool expect(char c) {
    skipSpaces();
    if (*s != c) {
      return false;
    }
    s++;
    return true;
  }

  void skipSpaces() {
    while (*s == ' ') {
      s++;
    }
  }

  const char* s;
  /// The text's terminating NUL.
  const char* last;
};

/// Reads the tail for flag 2, 3 or 4 from `at` into `state` (4 is assembler
/// 1's, laid out as 2). `readTask(reader, task)` reads one task as
/// writeResumeTail's `writeTask` wrote it. False on a syntax error. `done`
/// is set to the number of tasks already finished.
template <class Task, class ReadTask>
bool readResumeTail(const char* at, unsigned int flag, SplitSearchState<Task>& state,
                    std::size_t& done, ReadTask readTask) {
  ResumeReader r(at);
  done = 0;
  unsigned long long n = 0;
  if (flag == 3) {
    if (!r.expect('K') || !r.next(n)) {
      return false;
    }
    state.vectorSkip = n;
    return true;
  }
  if (!r.expect('T') || !r.next(n) || n > r.remaining()) {
    return false;
  }
  state.tasks.resize(static_cast<std::size_t>(n));
  for (Task& t : state.tasks) {
    if (!readTask(r, t)) {
      return false;
    }
  }
  if (!r.expect('C')) {
    return false;
  }
  r.skipSpaces();
  state.completed.assign(state.tasks.size(), 0);
  for (std::size_t i = 0; i < state.tasks.size(); i++, r.s++) {
    if (*r.s != '0' && *r.s != '1') {
      return false;
    }
    state.completed[i] = *r.s == '1' ? 1 : 0;
    done += state.completed[i];
  }
  unsigned long long m = 0;
  if (!r.expect('S') || !r.next(m)) {
    return false;
  }
  for (unsigned long long i = 0; i < m; i++) {
    unsigned long long sig = 0;
    if (!r.next(sig)) {
      return false;
    }
    state.reported.insert(sig);
  }
  // the weights, when they are there and whole; without them all are alike
  if (r.expect('W')) {
    unsigned long long w = 0;
    if (!r.next(w)) {
      return false;
    }
    state.doneWeight = static_cast<double>(w) / kWeightUnits;
    state.weights.resize(state.tasks.size());
    for (double& weight : state.weights) {
      if (!r.next(w)) {
        return false;
      }
      weight = static_cast<double>(w) / kWeightUnits;
    }
  }
  return true;
}

/// Appends the tail for flag 2, 3 or 4 to `out`; nothing for other flags.
/// `vectorReported` is what a flag 3 tail records.
template <class Task, class WriteTask>
void writeResumeTail(std::string& out, unsigned int flag, std::uint64_t vectorReported,
                     const SplitSearchState<Task>& state, WriteTask writeTask) {
  if (flag == 3) {
    out += " K " + std::to_string(vectorReported);
  } else if (flag == 2 || flag == 4) {
    out += " T " + std::to_string(state.tasks.size());
    for (const Task& t : state.tasks) {
      writeTask(out, t);
    }
    out += " C ";
    for (const std::uint8_t c : state.completed) {
      out += c ? '1' : '0';
    }
    out += " S " + std::to_string(state.reported.size());
    for (const std::uint64_t sig : state.reported) {
      out += ' ';
      out += std::to_string(sig);
    }
    if (state.weights.size() == state.tasks.size() && !state.tasks.empty()) {
      const auto units = [](double w) {
        return std::to_string(static_cast<unsigned long long>(w * kWeightUnits + 0.5));
      };
      out += " W " + units(state.doneWeight);
      for (const double w : state.weights) {
        out += ' ';
        out += units(w);
      }
    }
  }
}

}  // namespace bt
