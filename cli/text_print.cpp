// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from src/lib/print.cpp.
#include "text_print.h"

#include "bt/base/assert.h"
#include "bt/grid/voxel.h"
#include "bt/model/assembly.h"
#include "bt/model/disassembly.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"

#include <memory>
#include <vector>

namespace bt::cli {

namespace {

// " +---+ +---+" for `layers` layers of `width` cells.
void printFrame(std::FILE* out, int layers, int width) {
  for (int z = 0; z < layers; z++) {
    std::fputs(" +", out);
    for (int x = 0; x < width; x++) {
      std::fputc('-', out);
    }
    std::fputc('+', out);
  }
}

// The pieces of an assembly in their orientations, by piece number; null
// for a piece that is not placed when `placedOnly`. The original
// transformed every piece for a take-apart (and failed an assertion on an
// unplaced one), only the placed ones for an assembly.
std::vector<std::unique_ptr<Voxel>> orientedPieces(const Puzzle& puzzle, const Problem& problem,
                                                   const Assembly& assembly) {
  std::vector<std::unique_ptr<Voxel>> pieces(assembly.placementCount());
  unsigned int pc = 0;
  for (unsigned int i = 0; i < problem.getNumberOfParts(); i++) {
    for (unsigned int j = 0; j < problem.getPartMaximum(i); j++) {
      if (assembly.isPlaced(pc)) {
        pieces[pc] = problem.partShape(puzzle, i).clone();
        const bool turned = pieces[pc]->transform(assembly.getTransformation(pc));
        BT_ASSERT(turned);
      }
      pc++;
    }
  }
  return pieces;
}

// One take-apart node: `shapeOf[pc]` is drawn for its pc-th piece, named
// 'a' + `nameOf[pc]` (print_rec).
void printNode(std::FILE* out, const Separation& s,
               const std::vector<std::unique_ptr<Voxel>>& pieces,
               const std::vector<unsigned int>& shapeOf, const std::vector<unsigned int>& nameOf,
               int sx, int sy, int sz, bool originalDrawings) {
  for (unsigned int i = 0; i <= s.getMoves(); i++) {
    const State& st = s.getState(i);

    printFrame(out, sz + 2 * (sz / 2), sx + 2 * (sx / 2));
    std::fputc('\n', out);

    for (int y = -sy / 2; y < sy + sy / 2; y++) {
      for (int z = -sz / 2; z < sz + sz / 2; z++) {
        std::fputs(" !", out);
        for (int x = -sx / 2; x < sx + sx / 2; x++) {
          char c = ' ';
          for (unsigned int pc = 0; pc < s.getPieceNumber(); pc++) {
            const Voxel* v = pieces[shapeOf[pc]].get();
            if (v != nullptr && v->isFilled2(x - (st.getX(pc) - v->getHx()),
                                             y - (st.getY(pc) - v->getHy()),
                                             z - (st.getZ(pc) - v->getHz()))) {
              c = static_cast<char>('a' + nameOf[pc]);
              break;
            }
          }
          std::fputc(c, out);
        }
        std::fputc('!', out);
      }
      std::fputc('\n', out);
    }

    printFrame(out, sz + 2 * (sz / 2), sx + 2 * (sx / 2));
    std::fputc('\n', out);
  }

  std::fputs(" puzzle separates into  2 parts\n", out);

  // The pieces of the two parts, in the order of this node.
  const State& last = s.getState(s.getMoves());
  auto part = [&](const Separation* sub, bool removed) {
    if (sub == nullptr) {
      return;
    }
    std::vector<unsigned int> subShape;
    std::vector<unsigned int> subName;
    for (unsigned int i = 0; i < s.getPieceNumber(); i++) {
      if (last.pieceRemoved(i) == removed) {
        subName.push_back(nameOf[i]);
      }
    }
    if (originalDrawings) {
      // the n-th piece of the problem for the n-th of the node
      for (unsigned int i = 0; i < sub->getPieceNumber(); i++) {
        subShape.push_back(i);
      }
    } else {
      subShape = sub->getPieces();
      subName = sub->getPieces();
    }
    printNode(out, *sub, pieces, subShape, subName, sx, sy, sz, originalDrawings);
  };
  part(s.getRemoved(), true);
  part(s.getLeft(), false);
}

}  // namespace

void printShape(std::FILE* out, const Voxel& v, char base) {
  const int sx = static_cast<int>(v.getX());
  const int sz = static_cast<int>(v.getZ());
  printFrame(out, sz, sx);
  std::fprintf(out, " bx %u-%u by %u-%u bz %u-%u h: %i %i %i \n", v.boundX1(), v.boundX2(),
               v.boundY1(), v.boundY2(), v.boundZ1(), v.boundZ2(), v.getHx(), v.getHy(), v.getHz());

  for (unsigned int y = 0; y < v.getY(); y++) {
    for (unsigned int z = 0; z < v.getZ(); z++) {
      std::fputs(" !", out);
      for (unsigned int x = 0; x < v.getX(); x++) {
        // the whole value (colours give letters past the states'), as printf's %c wrote it
        const int cell = v.get(x, y, z);
        std::fputc(cell != 0 ? base + cell - 1 : ' ', out);
      }
      std::fputc('!', out);
    }
    std::fputc('\n', out);
  }

  printFrame(out, sz, sx);
  std::fputc('\n', out);
}

void printPuzzle(std::FILE* out, const Puzzle& puzzle) {
  for (unsigned int s = 0; s < puzzle.getNumberOfShapes(); s++) {
    std::fprintf(out, "shape %u:\n", s);
    printShape(out, puzzle.getShape(s));
  }

  std::fputs("=======================================================\n", out);

  for (unsigned int pr = 0; pr < puzzle.getNumberOfProblems(); pr++) {
    const Problem& prob = puzzle.getProblem(pr);

    std::fprintf(out, "problem %u (%s):\n", pr, prob.getName().c_str());
    if (!prob.resultValid()) {
      std::fputs(" result shape: not defined\n", out);
    } else {
      std::fprintf(out, " result shape: %u\n", prob.getResultId());
    }

    for (unsigned int sh = 0; sh < prob.getNumberOfParts(); sh++) {
      const unsigned int lo = prob.getPartMinimum(sh);
      const unsigned int hi = prob.getPartMaximum(sh);
      if (lo != hi) {
        std::fprintf(out, " piece shape: %u-%u times shape number %u\n", lo, hi,
                     prob.getShapeIdOfPart(sh));
      } else if (lo != 1) {
        std::fprintf(out, " piece shape: %u times shape number %u\n", lo,
                     prob.getShapeIdOfPart(sh));
      } else {
        std::fprintf(out, " piece shape: %u\n", prob.getShapeIdOfPart(sh));
      }
    }

    std::fputs("-------------------------------------------------------\n", out);
  }
}

void printSeparation(std::FILE* out, const Puzzle& puzzle, const Problem& problem,
                     const Separation& separation, const Assembly& assembly,
                     bool originalDrawings) {
  BT_ASSERT(problem.resultValid());
  const Voxel& res = problem.resultShape(puzzle);
  const std::vector<std::unique_ptr<Voxel>> pieces = orientedPieces(puzzle, problem, assembly);

  // the root names its pieces by their place in it, as the original did
  // (the problem's numbers when every piece is placed)
  std::vector<unsigned int> shapeOf;
  std::vector<unsigned int> nameOf;
  for (unsigned int i = 0; i < separation.getPieceNumber(); i++) {
    shapeOf.push_back(originalDrawings ? i : separation.getPieceName(i));
    nameOf.push_back(originalDrawings ? i : separation.getPieceName(i));
  }
  printNode(out, separation, pieces, shapeOf, nameOf, static_cast<int>(res.getX()),
            static_cast<int>(res.getY()), static_cast<int>(res.getZ()), originalDrawings);
}

void printAssembly(std::FILE* out, const Puzzle& puzzle, const Problem& problem,
                   const Assembly& assembly) {
  BT_ASSERT(problem.resultValid());
  const Voxel& res = problem.resultShape(puzzle);
  const std::vector<std::unique_ptr<Voxel>> pieces = orientedPieces(puzzle, problem, assembly);
  const int sx = static_cast<int>(res.getX());
  const int sz = static_cast<int>(res.getZ());

  printFrame(out, sz, sx);
  std::fputc('\n', out);

  for (int y = 0; y < static_cast<int>(res.getY()); y++) {
    for (int z = 0; z < sz; z++) {
      std::fputs(" !", out);
      for (int x = 0; x < sx; x++) {
        char c = ' ';
        // placed pieces are named in order, skipping the others
        unsigned int named = 0;
        for (unsigned int p = 0; p < assembly.placementCount(); p++) {
          if (!assembly.isPlaced(p)) {
            continue;
          }
          const Voxel& v = *pieces[p];
          if (v.isFilled2(x - (assembly.getX(p) - v.getHx()), y - (assembly.getY(p) - v.getHy()),
                          z - (assembly.getZ(p) - v.getHz()))) {
            c = static_cast<char>('a' + named);
            break;
          }
          named++;
        }
        std::fputc(c, out);
      }
      std::fputc('!', out);
    }
    std::fputc('\n', out);
  }

  printFrame(out, sz, sx);
  std::fputc('\n', out);
}

}  // namespace bt::cli
