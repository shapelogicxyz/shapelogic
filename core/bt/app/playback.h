// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The playback of a solution's disassembly: where each piece is, turned
/// how, and whether it is shown, at any step of the moves, fractions
/// included (turns tumble about their pivots); and the keyframe track an
/// animation plays (forward, holds, and back again).
/// Ported from src/lib/gltfexport.cpp (the sampler of solutionAnimation:
/// evaluate, the poses of hidden pieces, the keyframe times, the
/// quaternion hemispheres) over DisasmToMoves (stage 4). Writing glTF
/// belongs to the exports, later.

#include "bt/base/error.h"
#include "bt/mesh/draw_mesh.h"
#include "bt/solve/disasm_to_moves.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bt {
class Problem;
class Puzzle;
class Voxel;
}  // namespace bt

namespace bt::app {

/// Where a piece is: its mesh (in the assembly orientation, see
/// PlaybackSampler::pieceMesh) is turned by `rotation` and moved by
/// `translation`. Coordinates are mesh units, centred on the result.
struct PiecePose {
  std::array<float, 3> translation = {0, 0, 0};
  /// A unit quaternion x, y, z, w.
  std::array<float, 4> rotation = {0, 0, 0, 1};
  bool visible = false;
};

/// A piece of the solution.
struct PlaybackPiece {
  unsigned int piece = 0;  ///< its number in the problem
  unsigned int part = 0;
  unsigned int shape = 0;  ///< the shape's index in the puzzle
  /// "S1", or the shape's name, with " #2" for the second piece of a part.
  std::string name;
  /// Its orientation in the assembly: the one its mesh is drawn in.
  unsigned int orientation = 0;
};

/// How a track is sampled (the options of the glTF export).
struct TrackOptions {
  /// Samples per step; turns and bent slides need more than one.
  unsigned int samplesPerStep = 8;
  /// The time of one move or one turn.
  float secondsPerStep = 0.5F;
  /// The moves backwards after the disassembly, so a loop goes from
  /// assembled to apart and back.
  bool reassemble = true;
};

/// A keyframe track: the times and, per time, every piece's pose.
struct PlaybackTrack {
  std::vector<float> times;
  /// poses[k][i]: piece i (of pieces()) at times[k]. Hidden pieces carry
  /// the pose they have next to where they are shown, so an interpolation
  /// neither stalls nor drifts; consecutive quaternions of a piece lie in
  /// one hemisphere.
  std::vector<std::vector<PiecePose>> poses;
};

/// The playback of one solution. Keeps copies of what it needs, so it is
/// independent of the puzzle once made.
class PlaybackSampler {
 public:
  /// The playback of saved solution `solution` of problem `problemIndex`.
  /// Refused for stacking and sliding puzzles (not yet), a problem without
  /// a result, a missing solution, a solution without its disassembly
  /// (Unsupported / InvalidArgument / NotFound, with the original's
  /// messages), and a piece that cannot take its orientation.
  static Result<PlaybackSampler> make(const Puzzle& puzzle, unsigned int problemIndex,
                                      unsigned int solution);

  PlaybackSampler(PlaybackSampler&&) noexcept;
  PlaybackSampler& operator=(PlaybackSampler&&) noexcept;
  ~PlaybackSampler();

  /// The placed pieces, in problem order.
  [[nodiscard]] const std::vector<PlaybackPiece>& pieces() const { return pieces_; }
  /// The number of steps (moves and turns) of the disassembly.
  [[nodiscard]] unsigned int stepCount() const { return steps_; }
  /// The moves as the solution's level text ("3.5.16.6...").
  [[nodiscard]] const std::string& movesText() const { return movesText_; }

  /// The poses of all pieces at `step` (0 assembled, stepCount() apart;
  /// fractions in between). Refused (Internal) when a piece cannot take
  /// an orientation the moves give it.
  Result<std::vector<PiecePose>> sample(float step);

  /// The keyframes of an animation.
  Result<PlaybackTrack> track(const TrackOptions& options);

  /// The drawing mesh of piece i (of pieces()) in its assembly orientation.
  [[nodiscard]] mesh::DrawMesh pieceMesh(unsigned int i) const;

 private:
  PlaybackSampler() = default;

  struct Piece;

  bool evaluate(float step, std::vector<PiecePose>& out);

  std::vector<PlaybackPiece> pieces_;
  std::vector<Piece> data_;
  std::unique_ptr<DisasmToMoves> moves_;
  std::array<double, 3> centre_ = {0, 0, 0};
  unsigned int steps_ = 0;
  std::string movesText_;
};

}  // namespace bt::app
