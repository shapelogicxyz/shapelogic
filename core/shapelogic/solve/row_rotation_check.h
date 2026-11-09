// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The rotation check of RotationFilter, done on the rows of a placement
/// matrix instead of on an assembly built for the purpose.
///
/// Where the symmetry breaker cannot be used (several pieces of one shape,
/// piece ranges), every cover the search finds is an assembly to check, and
/// most are dropped: a tray of dominoes is found in each of its sixteen
/// orientations. Assembly::smallerRotationExists() builds the assembly,
/// turns all of it by every symmetry of the result and sorts it each time,
/// which there costs many times what the search does. This check gives the
/// same answer from a table: what each row's placement becomes under each
/// symmetry, as a number that orders as placements do. An assembly is then
/// compared with a turned copy by the smallest of those numbers per part,
/// and only an assembly that is kept is built.

#include "shapelogic/model/assembly.h"
#include "shapelogic/solve/rotation_filter.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace bt {

class Problem;
class Puzzle;

/// The rotation check over the rows of one matrix. Made for one run of a
/// search from the filter as it then stands; shared by the run's threads,
/// each with a Scratch of its own.
class RowRotationCheck {
 public:
  /// What a row stands for: a piece of this part, placed so.
  struct Row {
    unsigned int part;
    Placement placement;
  };

  /// What one thread works with, kept from one assembly to the next.
  struct Scratch {
    /// The rows of an assembly, as an assembler hands them to rejects().
    std::vector<std::uint32_t> found;

   private:
    friend class RowRotationCheck;
    /// The rows of one part: rows[begin .. end), the smallest of their own
    /// placements, and whether own[begin .. end) holds them sorted yet.
    struct Segment {
      std::size_t begin;
      std::size_t end;
      unsigned int part;
      std::uint64_t ownMin;
      bool ownSorted;
    };
    std::vector<std::uint32_t> rows;
    std::vector<std::size_t> partEnd;
    std::vector<Segment> segments;
    std::vector<std::uint64_t> own;
    std::vector<std::uint64_t> image;
    std::optional<Assembly> whole;
    std::optional<Assembly> turned;
  };

  /// The check `filter` asks for over `rows` (indexed as rejects() names
  /// them; the rows of a part next to each other, parts ascending). Null
  /// when it does not apply and Assembly::smallerRotationExists() has to
  /// do: the complete check, a pivot piece that is one of several of its
  /// shape, placements outside what the table's numbers hold, or a table
  /// too large.
  [[nodiscard]] static std::unique_ptr<RowRotationCheck> make(const Puzzle& puzzle,
                                                              const Problem& problem,
                                                              const RotationFilter& filter,
                                                              std::vector<Row> rows);

  /// True when the assembly of the rows `scratch.found` (in any order) is
  /// to be dropped: what RotationFilter::rejects() says of that assembly.
  /// Any thread, each with its own scratch.
  [[nodiscard]] bool rejects(Scratch& scratch) const;

 private:
  RowRotationCheck(const Puzzle& puzzle, const Problem& problem, const RotationFilter& filter,
                   std::vector<Row> rows);

  /// The table's entry for row `row` under the j-th transformation, worked
  /// out the first time it is asked for.
  [[nodiscard]] std::uint64_t entry(std::size_t j, std::uint32_t row) const;
  [[nodiscard]] std::uint64_t compute(std::size_t j, std::uint32_t row) const;

  /// Compares one part's pieces, turned by the j-th transformation, with
  /// the same pieces as they are: -1 smaller, 0 equal, 1 greater, 2 when
  /// the table cannot say.
  [[nodiscard]] int comparePart(std::size_t j, Scratch::Segment& seg, Scratch& s) const;
  /// The same once the smallest placements of both are found equal.
  [[nodiscard]] int compareBeyondSmallest(std::size_t j, Scratch::Segment& seg, Scratch& s,
                                          std::uint64_t smallest) const;

  /// The assembly of the rows in `s`, as the assemblers build it.
  void buildWhole(Scratch& s) const;

  const Puzzle& puzzle_;
  const Problem& problem_;
  GridType gt_;
  unsigned int rotations_;
  unsigned int pivot_;
  /// The pivot's part; the number of parts when there is no pivot.
  unsigned int pivotPart_;
  const MirrorInfo* mirror_;
  /// For turning a single placement by a mirroring transformation.
  MirrorInfo noMirror_;

  std::vector<Row> rows_;
  /// Each row's own placement as a number.
  std::vector<std::uint64_t> keys_;
  /// The first piece of each part, and whether the part's shape has no
  /// mirror symmetry (a mirrored copy of it is another shape).
  std::vector<unsigned int> firstPiece_;
  std::vector<std::uint8_t> chiral_;
  /// The symmetries of the result to check, the identity left out.
  std::vector<unsigned char> transforms_;
  /// transforms_.size() * rows_.size() entries: 0 until worked out. Written
  /// by any thread; two threads that work out one entry store the same.
  std::unique_ptr<std::atomic<std::uint64_t>[]> images_;
};

}  // namespace bt
