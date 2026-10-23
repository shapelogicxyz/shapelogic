// SPDX-License-Identifier: GPL-3.0-or-later
// The routes of sliding solutions and where the player puts the pieces
// between the steps, against the original's: tests/data/slide_routes.txt
// (oracle.py record-slide-routes) holds, for the sliding puzzles the
// original's solve thread solved (tests/data/slide_solved_by_original/),
// every move's route (sliding::slideRoute) and the positions its solution
// slider gave at quarter steps with the routes applied. The playback
// sampler is checked to move the pieces the same way.
#include "shapelogic/app/playback.h"
#include "shapelogic/io/puzzle_file.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/solution.h"
#include "shapelogic/solve/disasm_to_moves.h"
#include "shapelogic/solve/sliding_search.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace bt;

namespace {

struct Recorded {
  unsigned int solution = 0;
  unsigned int moves = 0;
  std::vector<std::string> routes;     // "ROUTE ..." lines
  std::vector<std::string> positions;  // "AT ..." lines
};

std::map<std::string, std::vector<Recorded>> readDump() {
  std::ifstream in("tests/data/slide_routes.txt");
  REQUIRE(in.good());
  std::map<std::string, std::vector<Recorded>> out;
  std::string file;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::istringstream is(line);
    std::string key;
    is >> key;
    if (key == "FILE") {
      is >> file;
      out[file];
    } else if (key == "SOLUTION") {
      Recorded r;
      std::string word;
      is >> r.solution >> word >> r.moves;
      out[file].push_back(r);
    } else if (key == "ROUTE") {
      out[file].back().routes.push_back(line);
    } else if (key == "AT") {
      out[file].back().positions.push_back(line);
    }
  }
  return out;
}

std::string routeLine(const Puzzle& puzzle, const Problem& pr, const Separation& sep,
                      unsigned int step) {
  std::vector<unsigned int> movers;
  const std::vector<sliding::SlideShift> route =
      sliding::slideRoute(puzzle, pr, sep, step, &movers);
  std::string s = "ROUTE " + std::to_string(step) + " movers";
  for (const unsigned int m : movers) {
    s += " " + std::to_string(m);
  }
  s += " corners";
  // the original's trays are one layer: its corners are x,y
  for (const sliding::SlideShift& c : route) {
    s += " " + std::to_string(c[0]) + "," + std::to_string(c[1]);
    if (c[2] != 0) {
      s += "," + std::to_string(c[2]);
    }
  }
  return s;
}

std::string positionLine(const DisasmToMoves& mv, unsigned int quarter, unsigned int pieces) {
  std::string s = "AT " + std::to_string(quarter) + "/4";
  char buf[96];
  for (unsigned int i = 0; i < pieces; i++) {
    std::snprintf(buf, sizeof(buf), " %.3f,%.3f,%.3f,%.3f", static_cast<double>(mv.getX(i)),
                  static_cast<double>(mv.getY(i)), static_cast<double>(mv.getZ(i)),
                  static_cast<double>(mv.getA(i)));
    s += buf;
  }
  return s;
}

}  // namespace

TEST_CASE("sliding routes: every move's route and the player's positions are the original's",
          "[sliding][playback]") {
  const std::map<std::string, std::vector<Recorded>> dump = readDump();
  REQUIRE(dump.size() >= 10);
  unsigned int solutions = 0;
  unsigned int bent = 0;
  for (const auto& [file, recorded] : dump) {
    INFO(file);
    Result<Puzzle> loaded = io::loadPuzzle("tests/data/slide_solved_by_original/" + file);
    REQUIRE(loaded.has_value());
    const Puzzle& puzzle = *loaded;
    const Problem& pr = puzzle.getProblem(0);
    for (const Recorded& r : recorded) {
      INFO("solution " << r.solution);
      REQUIRE(r.solution < pr.getNumberOfSavedSolutions());
      const Separation* sep = pr.getSavedSolution(r.solution).getDisassembly();
      REQUIRE(sep != nullptr);
      REQUIRE(sep->getMoves() == r.moves);
      solutions++;

      REQUIRE(r.routes.size() == r.moves);
      for (unsigned int step = 0; step < r.moves; step++) {
        CHECK(routeLine(puzzle, pr, *sep, step) == r.routes[step]);
        std::vector<unsigned int> movers;
        if (sliding::slideRoute(puzzle, pr, *sep, step, &movers).size() > 2) {
          bent++;
        }
      }

      // the solution slider's driver, with the routes
      DisasmToMoves mv(sep, 2 * pr.resultShape(puzzle).getBiggestDimension(),
                       pr.getNumberOfPieces());
      sliding::applySlideRoutes(puzzle, pr, *sep, mv);
      REQUIRE(r.positions.size() == 4 * r.moves + 1);
      for (unsigned int q = 0; q <= 4 * r.moves; q++) {
        mv.setStep(static_cast<float>(q) / 4.0F, false, true);
        CHECK(positionLine(mv, q, pr.getNumberOfPieces()) == r.positions[q]);
      }

      // the playback sampler moves each piece the same way (a cell is one
      // unit on this grid): its translation from the start is the driver's
      Result<app::PlaybackSampler> pb = app::PlaybackSampler::make(puzzle, 0, r.solution);
      REQUIRE(pb.has_value());
      REQUIRE(pb->stepCount() == r.moves);
      const Result<std::vector<app::PiecePose>> start = pb->sample(0);
      REQUIRE(start.has_value());
      mv.setStep(0, false, true);
      std::vector<std::pair<float, float>> origin;
      for (const app::PlaybackPiece& piece : pb->pieces()) {
        origin.emplace_back(mv.getX(piece.piece), mv.getY(piece.piece));
      }
      for (unsigned int q = 1; q <= 4 * r.moves; q++) {
        const float step = static_cast<float>(q) / 4.0F;
        const Result<std::vector<app::PiecePose>> at = pb->sample(step);
        REQUIRE(at.has_value());
        mv.setStep(step, false, true);
        for (std::size_t i = 0; i < pb->pieces().size(); i++) {
          const unsigned int piece = pb->pieces()[i].piece;
          CHECK(std::fabs(((*at)[i].translation[0] - (*start)[i].translation[0]) -
                          (mv.getX(piece) - origin[i].first)) < 1e-4F);
          CHECK(std::fabs(((*at)[i].translation[1] - (*start)[i].translation[1]) -
                          (mv.getY(piece) - origin[i].second)) < 1e-4F);
        }
      }
    }
  }
  CHECK(solutions > 100);
  // the recording has routes that turn corners, which the player follows
  CHECK(bent > 10);
}
