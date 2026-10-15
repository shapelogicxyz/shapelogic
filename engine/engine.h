// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// shapelogic_engine: JSON-RPC 2.0 over stdio around one app::Document (decisions
/// 0003 and 0012). The main thread runs the requests one at a time and
/// polls the document's background work for events; a reader thread only
/// queues the lines of stdin.

#include "output.h"
#include "rpc.h"

#include "shapelogic/app/document.h"
#include "shapelogic/app/explorers.h"
#include "shapelogic/app/playback.h"
#include "shapelogic/solve/solver_type.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <istream>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bt::engine {

/// The engine's settings, kept across documents.
struct Settings {
  unsigned int undoDepth = 25;
  /// The threads of a solve whose options do not say; 0 every hardware thread.
  unsigned int threads = 0;
  SolverType solver = SolverType::Classic;
  /// Where a full sliding search keeps its older levels (empty: in
  /// memory), and the disk they may take in GB (0: automatic). The folder
  /// starts as the user's cache folder's "sliding" (the original's default
  /// for SHAPELOGIC_SLIDE_DIR).
  std::string slideFolder = defaultSlideFolder();
  double slideDiskGB = 0;
  /// Where a stacking solve's Panex search is saved (empty: nothing is
  /// saved, and a search that outgrows memory solves by halves), and the
  /// disk its levels may take in GB (0: automatic). The folder starts as
  /// the user's cache folder's "panex" (the original's default for
  /// SHAPELOGIC_PANEX_DIR). A paused stacking solve is its saved search
  /// there: solve.continue, solve.abort, solve.status and the paused
  /// solve's export and import look for it there.
  std::string panexFolder = defaultPanexFolder();
  double panexDiskGB = 0;

  /// The user's cache folder's "sliding", or empty without one.
  static std::string defaultSlideFolder();
  /// The user's cache folder's "panex", or empty without one.
  static std::string defaultPanexFolder();
};

struct State;

/// A method: reads its params, does its work, returns its result.
using Method = Reply (*)(State&, Params&);
using MethodTable = std::map<std::string, Method, std::less<>>;

/// What the methods work on: the document and what the engine keeps
/// beside it.
struct State {
  State(Output& output, std::ostream& logs, const MethodTable& table);

  Output& out;
  /// stderr: what has no reply to go into.
  std::ostream& log;
  const MethodTable& methods;
  Settings settings;
  /// Always set; replaced by document.new, open and the paused import.
  std::optional<app::Document> doc;
  /// Where it was opened from or last saved to.
  std::optional<std::filesystem::path> path;

  /// Playbacks and explorers, by handle number (never reused).
  std::uint64_t nextHandle = 1;
  std::map<std::uint64_t, app::PlaybackSampler> playbacks;
  std::map<std::uint64_t, app::AssemblyExplorer> assemblyExplorers;
  std::map<std::uint64_t, app::MovementExplorer> movementExplorers;

  bool shutdown = false;
  /// The request being run is a notification: it gets no reply, so it
  /// may not produce a frame either (nothing would refer to it).
  bool inNotification = false;
  /// Events to send after the reply of the request being run (a new
  /// document's farewell to the old one's work).
  std::vector<std::pair<std::string, Json>> queuedEvents;

  /// What the events last told of a problem's background work.
  struct Watch {
    SolveState state = SolveState::Idle;
    /// A run was started since the last poll: its state is told even if
    /// it is the one told before (a short run can start and end between
    /// two polls).
    bool launched = false;
    std::chrono::steady_clock::time_point progressAt{};
    /// The problem's solutionsVersion() last told by solve.solutionsAdded.
    std::uint64_t solutionsVersion = 0;
    bool analysing = false;
    std::chrono::steady_clock::time_point analyseAt{};
    unsigned int analysed = 0;
  };
  std::map<app::Id, Watch> watches;
  /// The revision document.changed last told.
  std::uint64_t announced = 0;

  [[nodiscard]] app::Document& document() { return *doc; }
  [[nodiscard]] const app::Document& document() const { return *doc; }
  /// The revision clients see: the document's, counted on from the
  /// documents before it, so that it never goes back.
  [[nodiscard]] std::uint64_t revision() const { return revisionBase_ + doc->revision(); }
  /// {revision}: the result of a mutating method.
  [[nodiscard]] Json changed() const { return Json{{"revision", revision()}}; }
  /// Replaces the document; drops the handles and what the events told,
  /// after queueing a last solve.state idle and solution.analyseEnded for
  /// the old document's problems whose work the events were reporting.
  void replaceDocument(app::Document d, std::optional<std::filesystem::path> from);
  /// Sends a frame now (before the reply) and describes it. Throws
  /// (std::runtime_error) inside a notification: no reply would refer to
  /// the frame.
  Json sendFrame(const FrameBuilder& frame);
  /// Sends the queued events.
  void flushEvents();
  /// A new handle number.
  std::uint64_t newHandle() { return nextHandle++; }

 private:
  std::uint64_t revisionBase_ = 0;
};

// The methods of each area (methods_*.cpp).
void addEngineMethods(MethodTable& t);
void addDocumentMethods(MethodTable& t);
void addShapeMethods(MethodTable& t);
void addColourMethods(MethodTable& t);
void addProblemMethods(MethodTable& t);
void addSolveMethods(MethodTable& t);
void addSolutionMethods(MethodTable& t);
void addPlaybackMethods(MethodTable& t);
void addExploreMethods(MethodTable& t);
void addSlidingMethods(MethodTable& t);
void addStackingMethods(MethodTable& t);
void addExportMethods(MethodTable& t);

/// Sends the events of the background work: solves, analyses, and
/// document.changed when the document changed without a request (events.cpp).
/// What has ended is taken in first (Document::poll) and announced last, so
/// each run's last solve.state and each analysis's analyseEnded come before
/// the document.changed of their results (decision 0012). `between` runs
/// after the taking in and before the telling (a test makes work end there).
void pollEvents(State& s, std::chrono::steady_clock::time_point now,
                const std::function<void()>& between = {});
/// document.changed, when the revision moved since it was last told.
void announceChange(State& s);

/// The engine.
class Engine {
 public:
  /// Writes messages and frames to `out`, logs to `log`.
  Engine(std::ostream& out, std::ostream& log);

  /// Runs one request line: its reply (unless a notification) and the
  /// events it caused.
  void handleLine(std::string_view line);
  /// Sends the events of the background work.
  void poll();
  /// A solve or analysis is active: poll often.
  [[nodiscard]] bool busy() const;
  /// engine.shutdown was called.
  [[nodiscard]] bool stopped() const { return state_.shutdown; }
  /// Reads requests from `in` until it ends or engine.shutdown; returns
  /// true when the input ended. After a shutdown the thread reading `in`
  /// may still be blocked on it: `in` must then outlive the process
  /// (stdin).
  bool run(std::istream& in);

  [[nodiscard]] State& state() { return state_; }

  /// The longest request line read (64 MiB): the reader stops keeping a
  /// longer one and it is answered -32600 once its newline arrives.
  static constexpr std::size_t kMaxLine = std::size_t{64} << 20;
  /// The deepest nesting of a request's JSON; deeper lines are -32600.
  static constexpr int kMaxDepth = 64;
  /// The reader thread waits when this many lines (or bytes of lines) are
  /// queued and not yet run: a client that floods requests is held back
  /// instead of growing the engine's memory.
  static constexpr std::size_t kMaxQueuedLines = 1000;
  static constexpr std::size_t kMaxQueuedBytes = std::size_t{64} << 20;

 private:
  Output out_;
  std::ostream& log_;
  MethodTable methods_;
  State state_;
};

}  // namespace bt::engine
