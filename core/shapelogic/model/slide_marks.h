// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The starts and goals of a sliding problem: per cell of its board (the
/// problem's result, a start/goal shape), the piece that starts there and
/// the piece whose goal it is. They belong to the problem, not the board,
/// so several problems may share one board and each have a layout of its
/// own (decision 0027). The rules that read them are bt/model/sliding.h.

#include <cstddef>
#include <vector>

namespace bt {

/// One problem's start and goal marks: a mark is a piece's shape index + 1
/// (0 is none, 255 the most) on the board cell with that index
/// (Voxel::getIndex). Cells past the last mark hold none, so the marks of a
/// board that grew or shrank at its end still read right; an edit that
/// moves the board's cells moves the marks with them
/// (sliding::BoardEdit).
class SlideMarks {
 public:
  /// The start (goal false) or goal mark of cell i; 0 for none.
  [[nodiscard]] unsigned int get(std::size_t i, bool goal) const {
    const std::vector<unsigned char>& m = goal ? goal_ : start_;
    return i < m.size() ? m[i] : 0;
  }
  /// The start mark of cell i.
  [[nodiscard]] unsigned int start(std::size_t i) const { return get(i, false); }
  /// The goal mark of cell i.
  [[nodiscard]] unsigned int goal(std::size_t i) const { return get(i, true); }

  /// Sets the start or goal mark of cell i (0 takes it off).
  void set(std::size_t i, bool goal, unsigned int mark) {
    std::vector<unsigned char>& m = goal ? goal_ : start_;
    if (i >= m.size()) {
      if (mark == 0) {
        return;
      }
      m.resize(i + 1, 0);
    }
    m[i] = static_cast<unsigned char>(mark);
    // no zeros at the end: equal marks compare equal
    while (!m.empty() && m.back() == 0) {
      m.pop_back();
    }
  }

  /// The cells one side's marks reach: every cell at or past it has none.
  [[nodiscard]] std::size_t size(bool goal) const { return (goal ? goal_ : start_).size(); }
  /// The cells either side's marks reach.
  [[nodiscard]] std::size_t size() const { return start_.size() > goal_.size() ? start_.size() : goal_.size(); }

  /// True when any cell has a start (goal false) or a goal.
  [[nodiscard]] bool any(bool goal) const { return !(goal ? goal_ : start_).empty(); }
  /// True when no cell has a start or a goal.
  [[nodiscard]] bool empty() const { return start_.empty() && goal_.empty(); }

  /// Takes every start and goal off.
  void clear() {
    start_.clear();
    goal_.clear();
  }

  [[nodiscard]] bool operator==(const SlideMarks&) const = default;

 private:
  std::vector<unsigned char> start_;
  std::vector<unsigned char> goal_;
};

}  // namespace bt
