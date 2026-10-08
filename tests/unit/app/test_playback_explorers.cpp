// SPDX-License-Identifier: GPL-3.0-or-later
// The playback sampler, ported from the original's test/test_gltfexport.cpp
// (a burr, the rotation moves of test_rotation_solvers, a solution without
// disassembly refused; the glTF container itself belongs to the exports),
// with new cases on the poses; and the explorers (new). The tracks are
// compared with the original's sampler in test_app_dump.cpp.
#include "app_test_helpers.h"

#include "bt/app/document.h"
#include "bt/app/explorers.h"
#include "bt/app/playback.h"
#include "bt/model/problem.h"
#include "bt/model/solution.h"
#include "bt/solve/disassembler.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <thread>

using namespace bt;
using namespace bt::app;
using test::openFixture;

namespace {

PlaybackSampler sampler(const Document& d, unsigned int sol = 0) {
  const Id p = d.problemIds().front();
  const Id s = d.puzzle().getProblem(0).getSavedSolution(sol).getId();
  Result<PlaybackSampler> r = d.playback(p, s);
  REQUIRE(r.has_value());
  return std::move(*r);
}

}  // namespace

TEST_CASE("playback: a burr comes apart by slides only", "[app][playback]") {
  Document d = openFixture("PelikanBurr.xmpuzzle");
  PlaybackSampler s = sampler(d);
  // the cage and six sticks
  CHECK(s.pieces().size() == 7);
  CHECK(s.stepCount() > 0);
  const PlaybackTrack t = s.track({}).value();
  bool moved = false;
  for (const auto& row : t.poses) {
    REQUIRE(row.size() == 7);
    for (std::size_t i = 0; i < row.size(); i++) {
      // nothing turns: each piece keeps its rotation (the original's glTF
      // has no rotation track then)
      CHECK(row[i].rotation == t.poses[0][i].rotation);
      moved = moved || row[i].translation != t.poses[0][i].translation;
    }
  }
  CHECK(moved);
  // the times: a hold, the moves, a hold, back again, a hold
  const std::size_t forward = std::size_t{s.stepCount()} * 8 + 1;
  CHECK(t.times.size() == 1 + forward + 1 + (forward - 1) + 1);
  CHECK(t.times.front() == 0.0F);
  for (std::size_t k = 1; k < t.times.size(); k++) {
    CHECK(t.times[k] > t.times[k - 1]);
  }
  // without reassembly: the moves once
  TrackOptions once;
  once.reassemble = false;
  once.samplesPerStep = 2;
  CHECK(s.track(once).value().times.size() == 1 + (std::size_t{s.stepCount()} * 2 + 1) + 1);

  // assembled, every piece shown; at the end, the last ones still shown
  const std::vector<PiecePose> start = s.sample(0).value();
  for (const PiecePose& p : start) {
    CHECK(p.visible);
  }
  // the meshes of the pieces
  for (unsigned int i = 0; i < s.pieces().size(); i++) {
    CHECK(s.pieceMesh(i).triangleCount() > 0);
  }
  CHECK_FALSE(s.pieces()[0].name.empty());
}

TEST_CASE("playback: rotation moves turn the pieces", "[app][playback][rotation]") {
  Document d = openFixture("test_rotation_solvers_solved.xmpuzzle");
  PlaybackSampler s = sampler(d);
  const PlaybackTrack t = s.track({}).value();
  bool turned = false;
  for (const auto& row : t.poses) {
    for (std::size_t i = 0; i < row.size(); i++) {
      const PiecePose& p = row[i];
      turned = turned || p.rotation != t.poses[0][i].rotation;
      // unit quaternions
      double n2 = 0;
      for (const float c : p.rotation) {
        n2 += static_cast<double>(c) * static_cast<double>(c);
      }
      const double n = std::sqrt(n2);
      CHECK(std::fabs(n - 1) < 1e-5);
    }
  }
  CHECK(turned);
  // half way through a turn the piece is between its two orientations
  bool between = false;
  for (unsigned int st = 0; st < s.stepCount(); st++) {
    for (const PiecePose& p : s.sample(static_cast<float>(st) + 0.5F).value()) {
      const auto w = std::fabs(p.rotation[3]);
      between = between || (w > 0.01F && w < 0.99F && std::fabs(w - 0.7071F) > 0.01F);
    }
  }
  CHECK(between);
}

TEST_CASE("playback: a solution without its disassembly is shown assembled",
          "[app][playback][parallel]") {
  Document d = openFixture("PelikanBurr.xmpuzzle");
  const Id p = d.problemIds().front();
  const Id sol = d.puzzle().getProblem(0).getSavedSolution(0).getId();
  // a disassembly made here carries the pieces' orientations; the fixture's
  // comes from a file without them, which both programs play in
  // orientation 0 (see the stage 7 handoff)
  REQUIRE(d.deleteDisassemblies(p, sol).has_value());
  AnalyseOptions o;
  o.which = AnalyseWhich::All;
  REQUIRE(d.analyseStart(p, o).value() == 1);
  for (int i = 0; i < 6000 && d.analyseProgress(p).value().active; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  REQUIRE(d.poll());
  PlaybackSampler with = sampler(d);
  REQUIRE(with.hasDisassembly());
  REQUIRE(with.stepCount() > 0);
  const std::vector<PiecePose> assembled = with.sample(0).value();
  // the moves dropped: one pose, the assembled one, through the same path
  REQUIRE(d.deleteDisassemblies(p, sol).has_value());
  Result<PlaybackSampler> r = d.playback(p, sol);
  REQUIRE(r.has_value());
  CHECK(r->stepCount() == 0);
  CHECK_FALSE(r->hasDisassembly());
  CHECK(r->movesText().empty());
  REQUIRE(r->pieces().size() == with.pieces().size());
  for (const float step : {0.0F, 0.5F, 3.0F}) {
    const std::vector<PiecePose> single = r->sample(step).value();
    REQUIRE(single.size() == assembled.size());
    for (std::size_t i = 0; i < single.size(); i++) {
      INFO("piece " << i << " at step " << step);
      CHECK(single[i].visible);
      for (std::size_t k = 0; k < 3; k++) {
        CHECK(std::fabs(single[i].translation[k] - assembled[i].translation[k]) < 1e-5F);
      }
      for (std::size_t k = 0; k < 4; k++) {
        CHECK(std::fabs(single[i].rotation[k] - assembled[i].rotation[k]) < 1e-5F);
      }
    }
  }
  // a track is a hold on the assembled puzzle; the meshes come in every style
  const PlaybackTrack track = r->track(TrackOptions{}).value();
  CHECK(track.times.size() == track.poses.size());
  CHECK(track.times.size() >= 2);
  CHECK(r->pieceMesh(0, mesh::MeshStyle::Drawing).triangleCount() > 0);
  CHECK_FALSE(r->pieceMesh(0, mesh::MeshStyle::Flat).edgeLines.empty());
}

TEST_CASE("playback: what has no animation is refused", "[app][playback]") {
  Document d = openFixture("PelikanBurr.xmpuzzle");
  const Id p = d.problemIds().front();
  // a solution that is not there
  CHECK(d.playback(p, 99999).error().code == Errc::NotFound);
  // sliding puzzles: not yet
  const Puzzle sliding = test::openFixture("test_sliding_solver.xmpuzzle").puzzle();
  CHECK(PlaybackSampler::make(sliding, 0, 0).error().code == Errc::Unsupported);
}

TEST_CASE("explorers: the placements and the assembly search step by step",
          "[app][explorers]") {
  Document d = openFixture("PelikanBurr.xmpuzzle");
  Result<AssemblyExplorer> e =
      d.exploreAssembly(d.problemIds().front(), SolverType::Classic, MatrixOptions{}, true);
  REQUIRE(e.has_value());
  REQUIRE(e->placementsSupported());
  CHECK(e->pieceCount() == d.puzzle().getProblem(0).getNumberOfPieces());
  std::size_t total = 0;
  for (unsigned int i = 0; i < e->pieceCount(); i++) {
    const std::vector<PiecePlacement> pl = e->placements(i);
    total += pl.size();
  }
  CHECK(total > 0);
  // the first piece of an assembly 0 problem has one placement after the
  // symmetry breaking at most several; all lie in the result
  CHECK_FALSE(e->placements(0).empty());
  CHECK(e->placements(9999).empty());

  // step until through: some assembly is reached on the way
  CHECK(e->finished() == 0.0F);
  bool full = false;
  for (int i = 0; i < 200000 && e->finished() < 1.0F; i++) {
    e->step(1);
    const Assembly a = e->currentAssembly();
    unsigned int placed = 0;
    for (unsigned int k = 0; k < a.placementCount(); k++) {
      placed += a.isPlaced(k) ? 1 : 0;
    }
    full = full || placed == e->pieceCount();
  }
  CHECK(full);

  // what it refuses
  Document s = openFixture("test_sliding_solver.xmpuzzle");
  CHECK(s.exploreAssembly(s.problemIds().front(), SolverType::Classic, {}, false).error().code ==
        Errc::Unsupported);
}

TEST_CASE("explorers: the movements of a take-apart", "[app][explorers]") {
  Document d = openFixture("CubeInCage.xmpuzzle");
  const Id p = d.problemIds().front();
  const Id sol = d.puzzle().getProblem(0).getSavedSolution(0).getId();
  Result<MovementExplorer> e = d.exploreMovements(p, sol, false);
  REQUIRE(e.has_value());
  const MovementExplorer::Node& root = e->root();
  CHECK(root.label.empty());
  CHECK(root.pieces.size() > 1);
  CHECK(e->directionLabels().size() == 6);
  CHECK(e->directionLabels()[0] == "x");
  CHECK(e->directionLabels()[1] == "-x");

  const std::vector<unsigned int> kids = e->analyse(0).value();
  REQUIRE_FALSE(kids.empty());
  for (const unsigned int k : kids) {
    CHECK(e->node(k).parent == 0);
    CHECK(e->node(k).label.find(": S") != std::string::npos);
  }
  // analysing again adds nothing new
  CHECK(e->analyse(0).value() == kids);

  // a move asked for by piece and direction: the matching one is found
  const unsigned int k0 = kids.front();
  const MovementExplorer::Node& n0 = e->node(k0);
  for (std::size_t i = 0; i < n0.pieces.size(); i++) {
    const int dx = n0.x[i] - root.x[i];
    const int dy = n0.y[i] - root.y[i];
    const int dz = n0.z[i] - root.z[i];
    if (dx != 0 || dy != 0 || dz != 0) {
      const Result<unsigned int> m = e->findMatching(0, n0.pieces[i], dx, dy, dz);
      REQUIRE(m.has_value());
      CHECK(e->node(*m).parent == 0);
      break;
    }
  }
  // a move added by hand, valid or not, under the root: direction 2 (z)
  // reversed
  const unsigned int added = e->addMove(0, {root.pieces[0]}, 2, true).value();
  CHECK(e->node(added).parent == 0);
  CHECK(e->node(added).z[0] == root.z[0] - 1);
  CHECK(e->node(added).y[0] == root.y[0]);
  CHECK(e->addMove(0, {12345}, 0, false).error().code == Errc::InvalidArgument);

  // prune: only the way to the node is left
  const std::vector<unsigned int> grand = e->analyse(k0).value();
  e->prune(k0);
  CHECK(e->exists(k0));
  CHECK(e->exists(0));
  for (const unsigned int k : kids) {
    CHECK(e->exists(k) == (k == k0));
  }
  for (const unsigned int g : grand) {
    CHECK_FALSE(e->exists(g));
  }
  CHECK(e->root().children == std::vector<unsigned int>{k0});
}
