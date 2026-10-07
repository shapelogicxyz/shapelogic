// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The check that drops assemblies which are rotations or mirror images of
/// others. Every assembler carried the same members and the same three
/// functions for it (checkForTransformedAssemblies,
/// applySolutionFilterFlags and the test in solution()); ported once from
/// src/lib/assembler_0.cpp, assembler_1.cpp and assembler_bt2.cpp.

#include "bt/model/assembly.h"

#include <memory>

namespace bt {

class Problem;
class Puzzle;

/// Which assemblies are dropped as rotated (or mirrored) copies of others.
///
/// An assembler leaves out some orientations of one piece (the symmetry
/// breaker) so that few rotated copies are found in the first place; when
/// that cannot catch them all, its matrix turns this check on, and every
/// assembly found is compared with its rotations. Read only while a search
/// runs, so threads share it.
struct RotationFilter {
  /// Assemblies are checked.
  bool avoidTransformed = false;
  /// The matrix asked for the check (kept when keepRotations turns it off,
  /// so that a later solve with other settings can turn it on again).
  bool active = false;
  /// The piece compared first.
  unsigned int pivot = 0;
  /// Mirror pairs of pieces; null: mirror images are not dropped.
  std::unique_ptr<MirrorInfo> mirror;
  /// Try every transformation and shift, not only the result's symmetries.
  bool complete = false;

  /// The matrix asks for the check with this pivot and mirror information
  /// (checkForTransformedAssemblies).
  void request(unsigned int pivotPiece, std::unique_ptr<MirrorInfo> mir) {
    avoidTransformed = true;
    active = true;
    pivot = pivotPiece;
    mirror = std::move(mir);
  }

  /// The settings of a solve: keep mirror images, keep rotations, check
  /// completely (applySolutionFilterFlags).
  void apply(bool keepMirrors, bool keepRotations, bool completeCheck) {
    complete = completeCheck;
    if (keepMirrors) {
      mirror.reset();
    }
    if (keepRotations) {
      avoidTransformed = false;
    } else if (active) {
      avoidTransformed = true;
    }
  }

  /// True when `assembly` is to be dropped: a rotation of it is smaller.
  [[nodiscard]] bool rejects(const Assembly& assembly, const Puzzle& puzzle,
                             const Problem& problem, bool strictColors) const {
    return avoidTransformed && assembly.smallerRotationExists(puzzle, problem, pivot, mirror.get(),
                                                              complete, strictColors);
  }
};

}  // namespace bt
