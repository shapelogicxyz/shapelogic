// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The puzzle: its grid, shapes, constraint colours, problems and comment.
/// Ported from src/lib/puzzle.{h,cpp} (reading and writing files lives in
/// bt/io; stacking rod sets are not modelled yet and are kept as preserved
/// XML).

#include "bt/base/assert.h"
#include "bt/base/clone_ptr.h"
#include "bt/grid/grid_type.h"
#include "bt/grid/voxel.h"
#include "bt/model/problem.h"
#include "bt/model/xml_element.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bt {

/// An RGB colour.
struct Rgb {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  bool operator==(const Rgb&) const = default;
};

/// A puzzle. A value type: copying it copies everything, solutions included.
///
/// Shapes are referred to by their index; problems refer to shapes by index
/// too, so removing or exchanging shapes goes through the puzzle, which
/// updates every problem.
class Puzzle {
 public:
  /// The most constraint colours (colour 0 is "none").
  static constexpr unsigned int kMaxColors = 63;

  /// An empty puzzle on grid `gt`.
  explicit Puzzle(GridType gt = GridType{}) : gt_(gt) {}

  [[nodiscard]] const GridType& getGridType() const { return gt_; }

  // ---- shapes ---------------------------------------------------------------

  /// Adds a shape (of this puzzle's grid); returns its index.
  unsigned int addShape(std::unique_ptr<Voxel> p);
  /// Adds an empty shape of the given size; returns its index.
  unsigned int addShape(unsigned int sx, unsigned int sy, unsigned int sz);
  [[nodiscard]] unsigned int getNumberOfShapes() const {
    return static_cast<unsigned int>(shapes_.size());
  }
  [[nodiscard]] const Voxel& getShape(unsigned int idx) const {
    BT_ASSERT(idx < shapes_.size());
    return *shapes_[idx];
  }
  [[nodiscard]] Voxel& getShape(unsigned int idx) {
    BT_ASSERT(idx < shapes_.size());
    return *shapes_[idx];
  }
  /// Removes a shape; the problems drop it and renumber the shapes above it.
  void removeShape(unsigned int idx);
  /// Swaps two shapes; the problems follow.
  void exchangeShapes(unsigned int s1, unsigned int s2);

  // ---- constraint colours -----------------------------------------------------

  /// Adds a colour; returns its 0-based index (colour number index+1).
  unsigned int addColor(std::uint8_t r, std::uint8_t g, std::uint8_t b);
  /// Removes colour number `col` (1-based, as in cells and colour rules):
  /// cells of that colour become neutral, higher colours move down, and the
  /// problems' colour rules follow. Solutions that no longer hold stay.
  void removeColor(unsigned int col);
  /// Changes the colour with 0-based index `idx`.
  void changeColor(unsigned int idx, std::uint8_t r, std::uint8_t g, std::uint8_t b);
  /// The colour with 0-based index `idx`.
  [[nodiscard]] Rgb getColor(unsigned int idx) const;
  /// The number of colours.
  [[nodiscard]] unsigned int colorNumber() const {
    return static_cast<unsigned int>(colors_.size());
  }

  // ---- problems ---------------------------------------------------------------

  /// Adds an empty problem; returns its index.
  unsigned int addProblem();
  /// Adds a copy of a problem's definition (Problem::copyDefinition); shape
  /// ids this puzzle does not have are dropped.
  unsigned int addProblem(const Problem& prob);
  [[nodiscard]] unsigned int getNumberOfProblems() const {
    return static_cast<unsigned int>(problems_.size());
  }
  [[nodiscard]] const Problem& getProblem(unsigned int p) const {
    BT_ASSERT(p < problems_.size());
    return problems_[p];
  }
  [[nodiscard]] Problem& getProblem(unsigned int p) {
    BT_ASSERT(p < problems_.size());
    return problems_[p];
  }
  void removeProblem(unsigned int p);
  void exchangeProblems(unsigned int p1, unsigned int p2);

  // ---- comment ----------------------------------------------------------------

  [[nodiscard]] const std::string& getComment() const { return comment_; }
  void setComment(const std::string& com) { comment_ = com; }
  /// True when the comment should be shown when the puzzle is opened.
  [[nodiscard]] bool getCommentPopup() const { return commentPopup_; }
  void setCommentPopup(bool val) { commentPopup_ = val; }

  /// File content of the puzzle element this version does not model (e.g.
  /// stacking <rodSets>).
  [[nodiscard]] const PreservedXml& preserved() const { return preserved_; }
  [[nodiscard]] PreservedXml& preserved() { return preserved_; }

 private:
  friend struct PuzzleFileAccess;  // the .xmpuzzle reader (bt/io)

  GridType gt_;
  std::vector<ClonePtr<Voxel>> shapes_;
  std::vector<Problem> problems_;
  /// red in the lowest byte, then green, then blue
  std::vector<std::uint32_t> colors_;
  std::string comment_;
  bool commentPopup_ = false;
  PreservedXml preserved_;
};

}  // namespace bt
