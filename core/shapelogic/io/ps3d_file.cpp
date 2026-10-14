// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/ps3dloader.cpp.

#include "shapelogic/grid/voxel.h"
#include "shapelogic/io/imports.h"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>

namespace bt::io {

namespace {

// The lines as std::getline(stream, line, '\n') gives them: a last line
// without its newline too, nothing after a final newline.
class Lines {
 public:
  explicit Lines(std::string_view text) : text_(text) {}

  bool next(std::string& line) {
    if (at_ >= text_.size()) {
      return false;
    }
    const std::size_t end = text_.find('\n', at_);
    if (end == std::string_view::npos) {
      line.assign(text_.substr(at_));
      at_ = text_.size();
    } else {
      line.assign(text_.substr(at_, end - at_));
      at_ = end + 1;
    }
    return true;
  }

  [[nodiscard]] unsigned int number() const { return number_; }
  void count() { number_++; }

 private:
  std::string_view text_;
  std::size_t at_ = 0;
  unsigned int number_ = 0;
};

}  // namespace

Result<Puzzle> readPuzzleSolver3D(std::string_view text, std::uint64_t maxCells) {
  Puzzle p;

  const unsigned int prIdx = p.addProblem();
  p.getProblem(prIdx).setName("Problem");

  std::string line;

  int state = 0;
  int linenum = 0;
  int sx = 0;
  int sy = 0;
  int sz = 0;
  unsigned int piece = 0;
  std::uint64_t cells = 0;

  Lines lines(text);
  while (lines.next(line)) {
    lines.count();

    switch (state) {
      case 0:
        if (line.substr(0, 6) == "PIECE ") {
          state = 1;
          line = line.substr(6, 11);
        } else if (line.substr(0, 7) == "RESULT ") {
          state = 2;
          line = line.substr(7, 11);
        }

        if (state != 0) {
          std::istringstream s(line);

          char c = 0;

          s >> sx >> c >> sy >> c >> sz;

          // a size line that does not read as three numbers is not a
          // PuzzleSolver3D file we can take
          if (!s || (sx <= 0) || (sy <= 0) || (sz <= 0) || (sx > 500) || (sy > 500) ||
              (sz > 500)) {
            return fail(Errc::ParseError,
                        "line " + std::to_string(lines.number()) +
                            ": the size of a shape must be three numbers from 1 to 500");
          }

          cells += std::uint64_t{static_cast<unsigned int>(sx)} * static_cast<unsigned int>(sy) *
                   static_cast<unsigned int>(sz);
          if (cells > maxCells) {
            return fail(Errc::ParseError, "line " + std::to_string(lines.number()) +
                                              ": the shapes would be too large");
          }

          piece = p.addShape(static_cast<unsigned int>(sx), static_cast<unsigned int>(sy),
                             static_cast<unsigned int>(sz));

          Problem& pr = p.getProblem(prIdx);
          if (state == 2) {
            pr.setResultId(p, piece);
          } else {
            pr.setShapeMinimum(p, piece, 1);
          }

          linenum = 0;
        }

        break;

      case 1:
      case 2:
      default:
        for (int z = 0; z < sz; z++) {
          for (int x = 0; x < sx; x++) {
            const auto at = static_cast<std::size_t>((z * (sx + 1)) + x);
            // a short line is empty to its end
            const char c = at < line.size() ? line[at] : ' ';
            if (c == ',') {
              return fail(Errc::ParseError, "line " + std::to_string(lines.number()) +
                                                ": a comma among the cells");
            }
            if (c != ' ') {
              p.getShape(piece).setState(static_cast<unsigned int>(x),
                                         static_cast<unsigned int>(linenum),
                                         static_cast<unsigned int>(z), Voxel::kFilled);
            }
          }
        }

        linenum++;
        if (linenum >= sy) {
          state = 0;
        }

        break;
    }
  }

  // find and mark duplicate shapes, and remove them from the problem
  Problem& pr = p.getProblem(prIdx);
  unsigned int s = pr.getNumberOfParts();

  for (unsigned int s1 = 0; s1 + 1 < s; s1++) {
    for (unsigned int s2 = s1 + 1; s2 < s; s2++) {
      if (pr.partShape(p, s1).identicalWithRots(pr.partShape(p, s2), false, false)) {
        const unsigned int sh1 = pr.getShapeIdOfPart(s1);
        const unsigned int sh2 = pr.getShapeIdOfPart(s2);
        pr.setShapeMaximum(p, sh1, pr.getShapeMaximum(sh1) + pr.getShapeMaximum(sh2));
        pr.setShapeMinimum(p, sh1, pr.getShapeMinimum(sh1) + pr.getShapeMinimum(sh2));
        p.getShape(sh2).setName("Duplicate");
        pr.setShapeMaximum(p, sh2, 0);
        s2--;
        s--;
      }
    }
  }

  return p;
}

}  // namespace bt::io
