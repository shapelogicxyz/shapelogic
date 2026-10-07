// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Helpers for the model and file tests. Ported from the end of
/// test/test_helpers.h of the original (puzzlesRoundtripEqual), plus fixture
/// loading.

#include "bt/io/puzzle_file.h"
#include "bt/model/puzzle.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace bt::test {

/// A fixture puzzle from tests/data/puzzles (the tests run from the source
/// root).
inline Puzzle loadFixture(const std::string& name) {
  Result<Puzzle> p = io::loadPuzzle("tests/data/puzzles/" + name);
  if (!p) {
    FAIL("could not load " << name << ": " << p.error().message);
  }
  return std::move(*p);
}

/// Compares two puzzles for the properties a save/load round trip must keep
/// and that this narrow probe tracks: grid, comment and popup flag, colours,
/// shapes (size, state and colour per cell), and problems with their names
/// and results. It does not look at shape names, weights or hotspots, part
/// lists, groups, colour rules, maxHoles, solve state or solutions; the
/// byte-for-byte cases cover those.
inline bool puzzlesRoundtripEqual(const Puzzle& a, const Puzzle& b) {
  if (a.getGridType() != b.getGridType() || a.getComment() != b.getComment() ||
      a.getCommentPopup() != b.getCommentPopup()) {
    return false;
  }

  if (a.colorNumber() != b.colorNumber()) {
    return false;
  }
  for (unsigned int i = 0; i < a.colorNumber(); i++) {
    if (a.getColor(i) != b.getColor(i)) {
      return false;
    }
  }

  if (a.getNumberOfShapes() != b.getNumberOfShapes()) {
    return false;
  }
  for (unsigned int s = 0; s < a.getNumberOfShapes(); s++) {
    const Voxel& va = a.getShape(s);
    const Voxel& vb = b.getShape(s);
    if (va.getX() != vb.getX() || va.getY() != vb.getY() || va.getZ() != vb.getZ()) {
      return false;
    }
    for (unsigned int i = 0; i < va.getXYZ(); i++) {
      if (va.getState(i) != vb.getState(i) || va.getColor(i) != vb.getColor(i)) {
        return false;
      }
    }
  }

  if (a.getNumberOfProblems() != b.getNumberOfProblems()) {
    return false;
  }
  for (unsigned int p = 0; p < a.getNumberOfProblems(); p++) {
    const Problem& pa = a.getProblem(p);
    const Problem& pb = b.getProblem(p);
    if (pa.getName() != pb.getName() || pa.resultValid() != pb.resultValid()) {
      return false;
    }
    if (pa.resultValid() && pa.getResultId() != pb.getResultId()) {
      return false;
    }
  }

  return true;
}

}  // namespace bt::test
