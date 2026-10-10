// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/gltfexport.cpp (solutionAnimation up to the keyframe
// track: the piece list, evaluate, the poses of hidden pieces, the times,
// the quaternion hemispheres; buildMesh's bounding box), with the sliding
// routes the original's solution slider applied (mainwindow.cpp).
#include "bt/app/playback.h"

#include "bt/base/assert.h"
#include "bt/base/int_cast.h"
#include "bt/grid/grid_type.h"
#include "bt/grid/symmetries.h"
#include "bt/grid/voxel.h"
#include "bt/mesh/grid_geometry.h"
#include "bt/mesh/voxel_mesh.h"
#include "bt/model/assembly.h"
#include "bt/model/disassembly.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"
#include "bt/model/solution.h"
#include "bt/solve/sliding_search.h"

#include <cmath>
#include <numbers>
#include <optional>
#include <utility>

namespace bt::app {

namespace {

struct Vec {
  double x;
  double y;
  double z;
};

Vec operator+(const Vec& a, const Vec& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec operator-(const Vec& a, const Vec& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

struct Mat {
  std::array<double, 9> m;
  Vec operator*(const Vec& v) const {
    return {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z,
            m[6] * v.x + m[7] * v.y + m[8] * v.z};
  }
  Mat operator*(const Mat& o) const {
    Mat r{};
    for (std::size_t i = 0; i < 3; i++) {
      for (std::size_t j = 0; j < 3; j++) {
        r.m[3 * i + j] = m[3 * i] * o.m[j] + m[3 * i + 1] * o.m[3 + j] + m[3 * i + 2] * o.m[6 + j];
      }
    }
    return r;
  }
  [[nodiscard]] Mat transposed() const { return {{m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]}}; }
  [[nodiscard]] double det() const {
    return m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
           m[2] * (m[3] * m[7] - m[4] * m[6]);
  }
  static Mat identity() { return {{1, 0, 0, 0, 1, 0, 0, 0, 1}}; }
};

// a turn of `deg` degrees about the unit axis, as glRotatef
Mat axisAngle(double deg, double ax, double ay, double az) {
  const double r = deg * std::numbers::pi / 180.0;
  const double c = std::cos(r);
  const double s = std::sin(r);
  const double t = 1 - c;
  return {{t * ax * ax + c, t * ax * ay - s * az, t * ax * az + s * ay, t * ax * ay + s * az,
           t * ay * ay + c, t * ay * az - s * ax, t * ax * az - s * ay, t * ay * az + s * ax,
           t * az * az + c}};
}

// the unit quaternion (x, y, z, w) of a rotation matrix
std::array<float, 4> toQuat(const Mat& rot) {
  const std::array<double, 9>& m = rot.m;
  const double tr = m[0] + m[4] + m[8];
  double x = 0;
  double y = 0;
  double z = 0;
  double w = 0;
  if (tr > 0) {
    const double s = std::sqrt(tr + 1.0) * 2;
    w = 0.25 * s;
    x = (m[7] - m[5]) / s;
    y = (m[2] - m[6]) / s;
    z = (m[3] - m[1]) / s;
  } else if (m[0] > m[4] && m[0] > m[8]) {
    const double s = std::sqrt(1.0 + m[0] - m[4] - m[8]) * 2;
    w = (m[7] - m[5]) / s;
    x = 0.25 * s;
    y = (m[1] + m[3]) / s;
    z = (m[2] + m[6]) / s;
  } else if (m[4] > m[8]) {
    const double s = std::sqrt(1.0 + m[4] - m[0] - m[8]) * 2;
    w = (m[2] - m[6]) / s;
    x = (m[1] + m[3]) / s;
    y = 0.25 * s;
    z = (m[5] + m[7]) / s;
  } else {
    const double s = std::sqrt(1.0 + m[8] - m[0] - m[4]) * 2;
    w = (m[3] - m[1]) / s;
    x = (m[2] + m[6]) / s;
    y = (m[5] + m[7]) / s;
    z = 0.25 * s;
  }
  const double n = std::sqrt(x * x + y * y + z * z + w * w);
  return {static_cast<float>(x / n), static_cast<float>(y / n), static_cast<float>(z / n),
          static_cast<float>(w / n)};
}

// The centre of the bounding box of a shape's drawing mesh (the faces
// buildMesh draws), or none for an empty mesh.
std::optional<Vec> meshCentre(const Voxel& v) {
  const mesh::Polyhedron poly = mesh::shapeMesh(v, mesh::MeshStyle::Drawing);
  bool first = true;
  Vec lo{0, 0, 0};
  Vec hi{0, 0, 0};
  for (int i = 0; i < poly.numFaces(); i++) {
    const int f = poly.faceAt(i);
    const mesh::Face& face = poly.face(f);
    if (face.hole || (face.flags & mesh::kFaceInside) != 0) {
      continue;
    }
    const mesh::Vec3 n = poly.faceNormal(f);
    if (std::isnan(n.x) || std::isnan(n.y) || std::isnan(n.z)) {
      continue;
    }
    int e = face.edge;
    const int sentinel = e;
    do {
      const mesh::Vec3& p = poly.dstPos(e);
      const Vec q{static_cast<double>(p.x), static_cast<double>(p.y), static_cast<double>(p.z)};
      if (first) {
        lo = hi = q;
        first = false;
      }
      lo = {std::fmin(lo.x, q.x), std::fmin(lo.y, q.y), std::fmin(lo.z, q.z)};
      hi = {std::fmax(hi.x, q.x), std::fmax(hi.y, q.y), std::fmax(hi.z, q.z)};
      e = poly.he(e).next;
    } while (e != sentinel);
  }
  if (first) {
    return std::nullopt;
  }
  return Vec{(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2};
}

}  // namespace

Result<mesh::DrawMesh> orientedMesh(const Voxel& shape, unsigned int transformation,
                                    mesh::MeshStyle style) {
  if (transformation >= shape.getGridType().getSymmetries().getNumTransformationsMirror()) {
    return fail(Errc::InvalidArgument, "there is no such transformation in this grid");
  }
  std::unique_ptr<Voxel> v = shape.clone();
  if (!v->transform(transformation)) {
    return fail(Errc::Internal, "A piece could not be put into its assembly orientation.");
  }
  return mesh::shapeDrawMesh(*v, style);
}

// A placed piece: its shape, starting orientation and what the poses are
// worked out from (pieceInfo of the original).
struct PlaybackSampler::Piece {
  std::unique_ptr<Voxel> shape;  // the part's shape, untransformed
  unsigned int trans0 = 0;
  int x0 = 0;  // where the assembly places it (for a solution without moves)
  int y0 = 0;
  int z0 = 0;
  Mat base0inv = Mat::identity();  // inverse of the matrix of trans0
  Vec centre0{0, 0, 0};            // bounding box centre of the trans0 mesh
  // the mesh centres of the other orientations the piece takes
  std::vector<std::pair<unsigned int, Vec>> orientCentre;
};

PlaybackSampler::PlaybackSampler(PlaybackSampler&&) noexcept = default;
PlaybackSampler& PlaybackSampler::operator=(PlaybackSampler&&) noexcept = default;
PlaybackSampler::~PlaybackSampler() = default;

Result<PlaybackSampler> PlaybackSampler::make(const Puzzle& puzzle, unsigned int problemIndex,
                                              unsigned int solNum) {
  const GridKind kind = puzzle.getGridType().getType();
  if (kind == GridKind::Stacking) {
    return fail(Errc::Unsupported, "Stacking puzzles cannot be exported as an animation.");
  }
  if (problemIndex >= puzzle.getNumberOfProblems()) {
    return fail(Errc::NotFound, "There is no such problem.");
  }
  const Problem& pr = puzzle.getProblem(problemIndex);
  if (!pr.resultValid()) {
    return fail(Errc::InvalidArgument, "The problem has no valid result shape.");
  }
  if (solNum >= pr.getNumberOfSavedSolutions()) {
    return fail(Errc::NotFound, "There is no such solution.");
  }
  const Solution& sol = pr.getSavedSolution(solNum);
  const Separation* tree = sol.getDisassembly();
  const Assembly& assm = sol.getAssembly();
  const Voxel& result = pr.resultShape(puzzle);

  PlaybackSampler s;
  if (tree != nullptr) {
    // the same driver and settings as the solution slider
    s.moves_ = std::make_unique<DisasmToMoves>(tree, 2 * result.getBiggestDimension(),
                                               pr.getNumberOfPieces());
    s.steps_ = tree->sumSteps();
    s.movesText_ = tree->movesText();
    // a sliding move can turn corners: it follows its route, not the
    // diagonal (as the original's solution slider did)
    if (kind == GridKind::Sliding) {
      sliding::applySlideRoutes(puzzle, pr, *tree, *s.moves_);
    }
  }
  // else: no moves to play; the pieces stay as the assembly places them

  float cx = 0;
  float cy = 0;
  float cz = 0;
  mesh::calculateSize(result, &cx, &cy, &cz);
  s.centre_ = {static_cast<double>(cx) * 0.5, static_cast<double>(cy) * 0.5,
               static_cast<double>(cz) * 0.5};

  // the placed pieces, with their mesh in the assembly orientation
  unsigned int piece = 0;
  for (unsigned int p = 0; p < pr.getNumberOfParts(); p++) {
    for (unsigned int q = 0; q < pr.getPartMaximum(p); q++, piece++) {
      if (!assm.isPlaced(piece)) {
        continue;
      }
      Piece d;
      d.shape = pr.partShape(puzzle, p).clone();
      d.trans0 = assm.getTransformation(piece);
      d.x0 = assm.getX(piece);
      d.y0 = assm.getY(piece);
      d.z0 = assm.getZ(piece);

      std::unique_ptr<Voxel> v = d.shape->clone();
      if (!v->transform(d.trans0)) {
        return fail(Errc::Internal, "A piece could not be put into its assembly orientation.");
      }
      const std::optional<Vec> c = meshCentre(*v);
      if (!c) {
        return fail(Errc::Internal, "A piece mesh could not be generated.");
      }
      d.centre0 = *c;
      Mat b = Mat::identity();
      v->getTransformMatrix(d.trans0, b.m.data());
      d.base0inv = b.transposed();

      PlaybackPiece info;
      info.piece = piece;
      info.part = p;
      info.shape = pr.getShapeIdOfPart(p);
      info.name = puzzle.getShape(info.shape).getName();
      if (info.name.empty()) {
        info.name = "S" + std::to_string(info.shape + 1);
      }
      if (pr.getPartMaximum(p) > 1) {
        info.name += " #" + std::to_string(q + 1);
      }
      info.orientation = d.trans0;

      s.pieces_.push_back(std::move(info));
      s.data_.push_back(std::move(d));
    }
  }
  if (s.pieces_.empty()) {
    return fail(Errc::InvalidArgument, "The solution places no pieces.");
  }
  return s;
}

bool PlaybackSampler::evaluate(float step, std::vector<PiecePose>& out) {
  if (moves_) {
    moves_->setStep(step, false, true);
  }
  out.assign(pieces_.size(), PiecePose{});
  const Vec centre{centre_[0], centre_[1], centre_[2]};

  for (std::size_t i = 0; i < pieces_.size(); i++) {
    Piece& pi = data_[i];
    const unsigned int p = pieces_[i].piece;

    // without moves every piece is shown where the assembly places it
    out[i].visible = !moves_ || moves_->getA(p) > 0;

    unsigned int t = moves_ ? moves_->getTrans(p) : pi.trans0;
    if (t == static_cast<unsigned int>(-1)) {
      t = pi.trans0;
    }

    std::unique_ptr<Voxel> v = pi.shape->clone();
    if (!v->transform(t)) {
      return false;
    }

    // the view draws the mesh of orientation t at (x - hotspot - centre)
    float x = moves_ ? moves_->getX(p) : static_cast<float>(pi.x0);
    float y = moves_ ? moves_->getY(p) : static_cast<float>(pi.y0);
    float z = moves_ ? moves_->getZ(p) : static_cast<float>(pi.z0);
    mesh::recalcSpaceCoordinates(*v, &x, &y, &z);
    auto hx = static_cast<float>(v->getHx());
    auto hy = static_cast<float>(v->getHy());
    auto hz = static_cast<float>(v->getHz());
    mesh::recalcSpaceCoordinates(*v, &hx, &hy, &hz);
    const Vec place{static_cast<double>(x - hx), static_cast<double>(y - hy),
                    static_cast<double>(z - hz)};

    // mesh_t = R * mesh_0 + d
    Mat r = Mat::identity();
    Vec d{0, 0, 0};
    if (t != pi.trans0) {
      Mat mt = Mat::identity();
      v->getTransformMatrix(t, mt.m.data());
      r = mt * pi.base0inv;
      if (std::fabs(r.det() - 1) > 1e-6) {
        return false;
      }
      std::optional<Vec> ct;
      for (const auto& [ot, oc] : pi.orientCentre) {
        if (ot == t) {
          ct = oc;
        }
      }
      if (!ct) {
        ct = meshCentre(*v);
        if (!ct) {
          return false;
        }
        pi.orientCentre.emplace_back(t, *ct);
      }
      d = *ct - r * pi.centre0;
    }

    // the tumble about the pivot cell's centre, local to the mesh_t frame
    Mat pm = Mat::identity();
    Vec pl{0, 0, 0};
    float ang = 0;
    float ax = 0;
    float ay = 0;
    float az = 0;
    float px = 0;
    float py = 0;
    float pz = 0;
    if (moves_ && moves_->getRotationAnim(p, &ang, &ax, &ay, &az, &px, &py, &pz)) {
      pm = axisAngle(static_cast<double>(ang), static_cast<double>(ax), static_cast<double>(ay),
                     static_cast<double>(az));
      pl = Vec{static_cast<double>(px), static_cast<double>(py), static_cast<double>(pz)} - place;
    }

    // world = P * (R * m0 + d - pl) + pl + place - centre
    const Vec tr = pm * (d - pl) + pl + place - centre;
    out[i].translation = {static_cast<float>(tr.x), static_cast<float>(tr.y),
                          static_cast<float>(tr.z)};
    out[i].rotation = toQuat(pm * r);
  }
  return true;
}

Result<std::vector<PiecePose>> PlaybackSampler::sample(float step) {
  std::vector<PiecePose> out;
  if (!evaluate(step, out)) {
    return fail(Errc::Internal, "The animation could not be computed for this puzzle.");
  }
  return out;
}

Result<PlaybackTrack> PlaybackSampler::track(const TrackOptions& opt) {
  // the step values sampled, in playback order
  const unsigned int k = opt.samplesPerStep != 0 ? opt.samplesPerStep : 1;
  std::vector<float> forward;
  for (unsigned int s = 0; s < steps_; s++) {
    for (unsigned int j = 0; j < k; j++) {
      forward.push_back(static_cast<float>(s) + static_cast<float>(j) / static_cast<float>(k));
    }
  }
  forward.push_back(static_cast<float>(steps_));

  std::vector<std::vector<PiecePose>> fwd(forward.size());
  for (std::size_t j = 0; j < forward.size(); j++) {
    if (!evaluate(forward[j], fwd[j])) {
      return fail(Errc::Internal, "The animation could not be computed for this puzzle.");
    }
  }

  // A hidden piece still has a pose, and an interpolation runs towards it
  // while the piece is shown: give it the pose it has just before it
  // vanishes (or just after it appears), so it does not stall or drift.
  const float eps = 1.0F / (64.0F * static_cast<float>(k));
  for (std::size_t j = 0; j < forward.size(); j++) {
    for (std::size_t i = 0; i < pieces_.size(); i++) {
      if (fwd[j][i].visible) {
        continue;
      }
      std::vector<PiecePose> near;
      if (j > 0 && fwd[j - 1][i].visible && evaluate(forward[j] - eps, near) && near[i].visible) {
        fwd[j][i] = near[i];
      } else if (j + 1 < forward.size() && fwd[j + 1][i].visible &&
                 evaluate(forward[j] + eps, near) && near[i].visible) {
        fwd[j][i] = near[i];
      } else {
        // the nearest keyframe where it is shown
        for (std::size_t dj = 1; dj < forward.size(); dj++) {
          if (j >= dj && fwd[j - dj][i].visible) {
            fwd[j][i] = fwd[j - dj][i];
            break;
          }
          if (j + dj < forward.size() && fwd[j + dj][i].visible) {
            fwd[j][i] = fwd[j + dj][i];
            break;
          }
        }
      }
      fwd[j][i].visible = false;
    }
  }

  // the times: a hold on the assembled puzzle, the moves, a hold, and
  // perhaps the moves backwards and a hold
  const float dt = (opt.secondsPerStep > 0 ? opt.secondsPerStep : 0.5F) / static_cast<float>(k);
  const float hold = 2 * dt * static_cast<float>(k);
  std::vector<float> times;
  std::vector<std::size_t> keys;
  times.push_back(0);
  keys.push_back(0);
  for (std::size_t j = 0; j < forward.size(); j++) {
    times.push_back(hold + static_cast<float>(j) * dt);
    keys.push_back(j);
  }
  times.push_back(times.back() + hold);
  keys.push_back(forward.size() - 1);
  if (opt.reassemble) {
    const float t0 = times.back();
    for (std::size_t j = 1; j < forward.size(); j++) {
      times.push_back(t0 + static_cast<float>(j) * dt);
      keys.push_back(forward.size() - 1 - j);
    }
    times.push_back(times.back() + hold);
    keys.push_back(0);
  }

  // consecutive quaternions of a piece in one hemisphere, so an
  // interpolation takes the short way
  PlaybackTrack track;
  track.times = std::move(times);
  track.poses.reserve(keys.size());
  for (std::size_t j = 0; j < keys.size(); j++) {
    std::vector<PiecePose> row = fwd[keys[j]];
    if (j > 0) {
      for (std::size_t i = 0; i < row.size(); i++) {
        const std::array<float, 4>& q = track.poses[j - 1][i].rotation;
        std::array<float, 4>& c = row[i].rotation;
        if (q[0] * c[0] + q[1] * c[1] + q[2] * c[2] + q[3] * c[3] < 0) {
          for (float& v : c) {
            v = -v;
          }
        }
      }
    }
    track.poses.push_back(std::move(row));
  }
  return track;
}

mesh::DrawMesh PlaybackSampler::pieceMesh(unsigned int i, mesh::MeshStyle style) const {
  // make() checked that the shape takes its orientation
  Result<mesh::DrawMesh> m = orientedMesh(*data_[i].shape, data_[i].trans0, style);
  BT_ASSERT(m.has_value());
  return std::move(*m);
}

}  // namespace bt::app
