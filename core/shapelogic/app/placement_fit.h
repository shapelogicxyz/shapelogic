// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Trying a tilted tray's every placement (decision 0045, stage G of
/// docs/TILTED-SQUARE-PLAN.md): for each board the tray can give
/// (tilted::placements()), the assemblies of one problem's pieces on it,
/// counted by an ordinary solve on a puzzle of the job's own. On a thread of
/// its own, with progress and a stop. New code; the pattern is Analysis.

#include "shapelogic/app/background_job.h"
#include "shapelogic/model/tilted_tray.h"
#include "shapelogic/solve/solve_session.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace sl {
class Puzzle;
}  // namespace sl

namespace sl::app {

/// One board of a fit and what was found on it.
struct BoardFit {
  /// The board's shift and its whole cells.
  tilted::Placed board;
  /// The board was tried to the end: `assemblies` is its count. A board
  /// too small for the pieces, or one a piece fits nowhere on, is counted
  /// with none (a solve of it would end in an error).
  bool counted = false;
  /// The assemblies of the pieces on it; of a board the job was stopped on,
  /// those found until then.
  std::uint64_t assemblies = 0;
};

/// Where a fit stands.
struct PlacementFitProgress {
  bool active = false;
  /// False when the boards are not every one there is (tilted::Placements).
  bool complete = true;
  /// The boards in the list's order, the most whole cells first.
  std::vector<BoardFit> boards;
  /// The boards tried to the end.
  unsigned int done = 0;
  /// How far the solve of the board under way is, 0 .. 1.
  float current = 0;
  /// Ended before every board was tried (stopped, or by an error).
  bool stopped = false;
  /// What ended it early, when it was no stop: a solve that could not start
  /// or an internal error.
  std::optional<std::string> failure;
};

/// One fit: solves the problem on each board in turn, on a thread of its
/// own. It works on `scratch`, a puzzle of its own, so nothing it does
/// touches the document.
class PlacementFit final : public BackgroundJob {
 public:
  /// Starts at once. `scratch` is a tilted puzzle whose shape 0 is the board
  /// and whose problem 0 is the problem to try, its result the board; the
  /// boards are its tray's (`boards`). `options` are the solve's: the count
  /// alone is taken, nothing is taken apart.
  PlacementFit(std::unique_ptr<Puzzle> scratch, const tilted::Placements& boards,
               SolveOptions options);
  /// Stops and waits.
  ~PlacementFit() override;

  /// The kind of every PlacementFit: the document's key for it.
  static constexpr Kind kKind = Kind::PlacementFit;
  /// Any thread.
  [[nodiscard]] PlacementFitProgress progress() const;

 private:
  void run() override;
  /// Pauses the solve of the board under way.
  void onStop() override;
  /// Solves the problem on board `index`; false when the job is to end.
  bool tryBoard(std::size_t index);

  std::unique_ptr<Puzzle> puzzle_;
  SolveOptions options_;
  bool complete_;

  /// Under mutex_: the boards' results and the solve under way (null
  /// between boards).
  std::vector<BoardFit> boards_;
  SolveSession* session_ = nullptr;
};

}  // namespace sl::app
