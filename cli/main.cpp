// SPDX-License-Identifier: GPL-3.0-or-later
//
// bt: batch solving from the command line. Its --json output matches the
// original burrTxt --json so the comparison harness (tests/oracle) can put
// the two side by side. Stage 3a: assembly only (assembler 0); taking apart
// arrives in stage 4, so solutions are reported as 0 and levels as null.
// The full CLI is stage 8. Modelled on src/burrTxt.cpp of the original.
#include "bt/base/stop.h"
#include "bt/base/thread_pool.h"
#include "bt/base/version.h"
#include "bt/grid/voxel.h"
#include "bt/io/puzzle_file.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"
#include "bt/solve/assembler.h"
#include "bt/solve/solver_type.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

// exit codes
constexpr int kExitOk = 0;
constexpr int kExitError = 1;      // the puzzle cannot be solved (as burrTxt --json)
constexpr int kExitUsage = 2;      // bad arguments or file
constexpr int kExitNotPorted = 3;  // the problem needs a part not ported yet

void usage() {
  std::fputs(
      "bt [options] file\n\n"
      "  file: puzzle file with the puzzle definition to solve\n\n"
      "  --json   machine-readable result (implies -d -q -r): one JSON object with\n"
      "           assemblies, solutions, dotlevel, level, totalmoves, solvetime;\n"
      "           with -R also moves and rotations\n"
      "  -d       take apart (not ported yet: solutions are reported as 0)\n"
      "  -R       also try rotations when taking apart (implies -d)\n"
      "  -q       only print the summary line\n"
      "  -n       no newline after the summary line\n"
      "  -r       reduce the placements before solving\n"
      "  -C       strict colour rules: a voxel fits only a result voxel of its colour\n"
      "  -o n     the problem to solve (from 0)\n"
      "  -t n     threads for the assembler (0: all)\n"
      "  --solver TYPE\n"
      "           \"BurrTools Classic\" (classic, default), \"Andrew Crowell\" (crowell),\n"
      "           \"BurrTools 2\" (bt2; arrives in stage 3b)\n"
      "  --no-simd\n"
      "           the plain search and scalar loops only (a benchmark switch)\n"
      "  --version\n",
      stdout);
}

struct Options {
  bool json = false;
  bool disassemble = false;
  bool rotations = false;
  bool quiet = false;
  bool newline = true;
  bool reduce = false;
  bool strictColors = false;
  bool noSimd = false;
  unsigned int problem = 0;
  unsigned int threads = 0;
  bt::SolverType solver = bt::SolverType::Classic;
  const char* file = nullptr;
};

// Parses a non-negative number; false when `s` is not one.
bool parseCount(const char* s, unsigned int* out) {
  char* end = nullptr;
  const long v = std::strtol(s, &end, 10);
  if (end == s || *end != 0 || v < 0) {
    return false;
  }
  *out = static_cast<unsigned int>(v);
  return true;
}

// Returns the exit code when parsing ends the program, -1 to go on.
int parseArgs(int argc, char** argv, Options& o) {
  for (int i = 1; i < argc; i++) {
    const std::string_view a = argv[i];
    if (a == "--version") {
      std::printf("bt %.*s\n", static_cast<int>(bt::kVersion.size()), bt::kVersion.data());
      return kExitOk;
    }
    if (a == "-h" || a == "--help") {
      usage();
      return kExitOk;
    }
    if (a == "--json") {
      o.json = true;
      o.disassemble = true;
      o.quiet = true;
      o.reduce = true;
    } else if (a == "--no-simd") {
      o.noSimd = true;
    } else if (a == "--solver") {
      if (i + 1 >= argc) {
        usage();
        return kExitUsage;
      }
      const auto t = bt::solverTypeFromName(argv[++i]);
      if (!t) {
        std::fprintf(stderr,
                     "bt: unknown solver '%s'\n"
                     "    use \"BurrTools Classic\", \"Andrew Crowell\", or \"BurrTools 2\"\n"
                     "    (also: classic, crowell, bt2)\n",
                     argv[i]);
        return kExitUsage;
      }
      o.solver = *t;
    } else if (a == "-t" || a == "-o") {
      unsigned int v = 0;
      if (i + 1 >= argc || !parseCount(argv[i + 1], &v)) {
        std::fprintf(stderr, "bt: %s requires a non-negative number\n", argv[i]);
        return kExitUsage;
      }
      (a == "-t" ? o.threads : o.problem) = v;
      i++;
    } else if (a.size() > 1 && a[0] == '-' && a[1] != '-') {
      for (const char c : a.substr(1)) {
        switch (c) {
          case 'd':
            o.disassemble = true;
            break;
          case 'R':
            o.rotations = true;
            o.disassemble = true;
            break;
          case 'q':
            o.quiet = true;
            break;
          case 'n':
            o.newline = false;
            break;
          case 'r':
            o.reduce = true;
            break;
          case 'C':
            o.strictColors = true;
            break;
          default:
            std::fprintf(stderr, "bt: unknown option -%c\n", c);
            usage();
            return kExitUsage;
        }
      }
    } else if (o.file == nullptr) {
      o.file = argv[i];
    } else {
      std::fprintf(stderr, "bt: more than one file given\n");
      return kExitUsage;
    }
  }

  if (o.file == nullptr) {
    usage();
    return kExitError;
  }
  return -1;
}

// burrTxt's messages for the errors of createMatrix: on stderr with --json
// (exit 1), else on stdout (exit 0).
int reportMatrixError(const Options& o, bt::AssemblerError err, int param) {
  char msg[160];
  switch (err) {
    case bt::AssemblerError::TooManyUnits:
      std::snprintf(msg, sizeof msg, "%i units too many for the result shape", param);
      break;
    case bt::AssemblerError::TooFewUnits:
      std::snprintf(msg, sizeof msg, "%i units too few for the result shape", param);
      break;
    case bt::AssemblerError::CannotPlace:
      std::snprintf(msg, sizeof msg, "Piece %i can be place nowhere in the result shape", param);
      break;
    case bt::AssemblerError::PuzzleUnhandable:
      std::snprintf(msg, sizeof msg,
                    "The puzzles contains features not yet supported by burrTxt");
      break;
    default:
      std::snprintf(msg, sizeof msg, "Oops internal error");
      break;
  }
  std::fprintf(o.json ? stderr : stdout, "%s\n", msg);
  return o.json ? kExitError : kExitOk;
}

}  // namespace

int main(int argc, char** argv) {
  Options o;
  if (const int code = parseArgs(argc, argv, o); code >= 0) {
    return code;
  }

  auto loaded = bt::io::loadPuzzle(o.file);
  if (!loaded) {
    std::fprintf(stderr, "bt: could not load \"%s\": %s\n", o.file,
                 loaded.error().message.c_str());
    return kExitUsage;
  }
  bt::Puzzle& puzzle = *loaded;

  if (o.problem >= puzzle.getNumberOfProblems()) {
    std::fprintf(stderr, "bt: there is no problem %u; the file has %u (numbered from 0)\n",
                 o.problem, puzzle.getNumberOfProblems());
    return kExitUsage;
  }

  // as burrTxt: the hotspots are set where every orientation keeps them
  for (unsigned int i = 0; i < puzzle.getNumberOfShapes(); i++) {
    puzzle.getShape(i).initHotspot();
  }

  const bt::Problem& problem = puzzle.getProblem(o.problem);

  const bt::AssemblerKind kind = bt::chooseAssembler(problem, o.solver);
  std::unique_ptr<bt::Assembler> assembler = bt::makeAssembler(puzzle, problem, o.solver);
  if (!assembler) {
    std::fprintf(stderr, "bt: this problem needs %.*s, which is not ported yet (stage 3b)\n",
                 static_cast<int>(bt::assemblerName(kind).size()), bt::assemblerName(kind).data());
    return kExitNotPorted;
  }

  bt::MatrixOptions matrix;
  matrix.strictColors = o.strictColors;
  if (const bt::AssemblerError err = assembler->createMatrix(matrix);
      err != bt::AssemblerError::None) {
    return reportMatrixError(o, err, assembler->errorParam());
  }

  if (o.disassemble && !o.json) {
    std::fprintf(stderr, "bt: taking apart is not ported yet (stage 4); solutions are not counted\n");
  }

  const auto start = std::chrono::steady_clock::now();

  if (o.reduce) {
    (void)assembler->reduce(bt::StopToken{});
  }

  bt::ThreadPool pool(o.threads > 0 ? o.threads : bt::hardwareThreads());
  bt::SearchOptions search;
  search.threads = o.threads;
  search.pool = &pool;
  search.vectorSearch = !o.noSimd;
  if (o.noSimd) {
    search.kernels = bt::SimdKernels::none();
  }

  unsigned long assemblies = 0;
  const bt::RunEnd end = assembler->assemble(
      [&assemblies](bt::Assembly) {
        assemblies++;
        return true;
      },
      search, bt::StopToken{});
  if (end != bt::RunEnd::Finished) {
    std::fprintf(stderr, "bt: the search stopped before its end\n");
    return kExitError;
  }

  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

  if (o.json) {
    std::printf("{\"assemblies\":%lu,\"solutions\":0", assemblies);
    std::printf(",\"dotlevel\":null,\"level\":null,\"totalmoves\":null");
    if (o.rotations) {
      std::printf(",\"moves\":null,\"rotations\":null");
    }
    std::printf(",\"solvetime\":%.3f}\n", seconds);
  } else {
    std::printf("%lu assemblies and 0 solutions found with %llu iterations in %.3f seconds",
                assemblies, static_cast<unsigned long long>(assembler->iterations()), seconds);
    if (o.newline) {
      std::printf("\n");
    }
  }
  return kExitOk;
}
