// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Piece positions at any step of a disassembly, fractional steps
/// interpolated: what an animation of the take-apart shows. Stage 6
/// generalises it into the playback sampler.
/// Ported from src/lib/disasmtomoves.{h,cpp}.

#include "shapelogic/model/disassembly.h"

#include <array>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace bt {

class DisassemblerNode;

/// Where each piece is, by its number in the problem.
class PiecePositions {
 public:
  virtual ~PiecePositions() = default;

  [[nodiscard]] virtual float getX(unsigned int piece) const = 0;
  [[nodiscard]] virtual float getY(unsigned int piece) const = 0;
  [[nodiscard]] virtual float getZ(unsigned int piece) const = 0;
  /// 0 invisible, 1 opaque.
  [[nodiscard]] virtual float getA(unsigned int piece) const = 0;
  /// The piece moves at this time.
  [[nodiscard]] virtual bool moving(unsigned int piece) const = 0;

  /// The piece's orientation, or (unsigned)-1 when not given (positions
  /// without turns).
  [[nodiscard]] virtual unsigned int getTrans(unsigned int /*piece*/) const {
    return static_cast<unsigned int>(-1);
  }

  /// A piece in the middle of a 90 degree turn: the angle so far (0 to
  /// +-90), the axis, and the pivot (the centre of the turn in grid units).
  /// False when the piece is not turning.
  virtual bool getRotationAnim(unsigned int /*piece*/, float* /*angleDeg*/, float* /*axisX*/,
                               float* /*axisY*/, float* /*axisZ*/, float* /*pivotX*/,
                               float* /*pivotY*/, float* /*pivotZ*/) const {
    return false;
  }

 protected:
  PiecePositions() = default;
  PiecePositions(const PiecePositions&) = default;
  PiecePositions& operator=(const PiecePositions&) = default;
};

/// The positions along a disassembly tree. A state without orientations (a
/// file without <dt>) gives none: getTrans() answers "not given" for its
/// pieces, so a player keeps them as the assembly placed them (the original
/// answered orientation 0; decision 0013, item 7).
class DisasmToMoves final : public PiecePositions {
 public:
  /// Positions along a copy of `tree` (null: every piece at 0, invisible).
  /// A removed group is moved `size` units out; `maxPiece` is one more than
  /// the largest piece number the tree may use (numbers may have gaps).
  DisasmToMoves(const Separation* tree, unsigned int size, unsigned int maxPiece);

  /// Sets the positions for `step`; between two whole steps the pieces are
  /// moved part of the way (turning pieces get a turn angle instead). With
  /// fadeOut, removed pieces fade; with centerActive the group being taken
  /// apart stays in the middle and the others are hidden.
  void setStep(float step, bool fadeOut = true, bool centerActive = false);

  /// Pieces follow a bent route between states `step` and step + 1: x/y/z
  /// corner points relative to each piece's place at `step`, start and end
  /// included. Without one a piece moves in a straight line.
  void setRoute(unsigned int step, const std::vector<unsigned int>& pieces,
                const std::vector<std::array<float, 3>>& offsets);

  [[nodiscard]] float getX(unsigned int piece) const override;
  [[nodiscard]] float getY(unsigned int piece) const override;
  [[nodiscard]] float getZ(unsigned int piece) const override;
  [[nodiscard]] float getA(unsigned int piece) const override;
  [[nodiscard]] bool moving(unsigned int piece) const override;
  [[nodiscard]] unsigned int getTrans(unsigned int piece) const override;
  bool getRotationAnim(unsigned int piece, float* angleDeg, float* axisX, float* axisY,
                       float* axisZ, float* pivotX, float* pivotY, float* pivotZ) const override;

 private:
  struct Route {
    std::vector<unsigned int> pieces;
    std::vector<std::array<float, 3>> offsets;
  };

  int doRecursive(const Separation* tree, int step, float* array, unsigned int* orientsOut,
                  bool centerActive, int cx, int cy, int cz);
  bool routeAt(int step, unsigned int piece, float frac, float* xyz) const;
  bool findRotationArrival(int step, unsigned int pieceName, int* pvx, int* pvy, int* pvz,
                           unsigned int* axis, unsigned int* sense) const;
  bool findRotationArrivalRec(const Separation* t, int step, unsigned int pieceName, int* pvx,
                              int* pvy, int* pvz, unsigned int* axis, unsigned int* sense) const;

  std::optional<Separation> tree_;
  /// How far removed groups are moved out.
  unsigned int size_;
  /// x, y, z and alpha of each piece.
  std::vector<float> moves_;
  /// Each piece's orientation (the start one while it turns).
  std::vector<unsigned int> orients_;
  /// Each piece's turn in progress (angle 0: none).
  std::vector<float> rotAngle_;
  std::vector<float> rotAxisX_;
  std::vector<float> rotAxisY_;
  std::vector<float> rotAxisZ_;
  std::vector<float> rotPivotX_;
  std::vector<float> rotPivotY_;
  std::vector<float> rotPivotZ_;
  std::vector<bool> mv_;
  unsigned int maxPieceName_;
  std::map<unsigned int, Route> routes_;
};

/// Positions that do not move: the pieces of one take-apart position.
class FixedPositions final : public PiecePositions {
 public:
  /// The pieces `pc` at their places in `nd`, for `pcs` pieces in all
  /// (the others invisible).
  FixedPositions(const DisassemblerNode& nd, const std::vector<unsigned int>& pc, unsigned int pcs);

  [[nodiscard]] float getX(unsigned int piece) const override;
  [[nodiscard]] float getY(unsigned int piece) const override;
  [[nodiscard]] float getZ(unsigned int piece) const override;
  [[nodiscard]] float getA(unsigned int piece) const override;
  [[nodiscard]] bool moving(unsigned int piece) const override;

 private:
  unsigned int pieces_;
  std::vector<int> x_;
  std::vector<int> y_;
  std::vector<int> z_;
  std::vector<bool> visible_;
};

}  // namespace bt
