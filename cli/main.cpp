// SPDX-License-Identifier: GPL-3.0-or-later
//
// bt: batch solving from the command line. Its --json output matches the
// original burrTxt --json so the comparison harness (tests/oracle) can put
// the two side by side: assemblies, and with -d each one taken apart as
// burrTxt's asm_cb did (the solutions, the highest level by its dotlevel
// rules). The full CLI is stage 8. Modelled on src/burrTxt.cpp of the
// original.
#include "bt/base/stop.h"
#include "bt/base/thread_pool.h"
#include "bt/base/version.h"
#include "bt/grid/voxel.h"
#include "bt/io/puzzle_file.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"
#include "bt/solve/assembler.h"
#include "bt/solve/assembler_bt2.h"
#include "bt/solve/disassembler.h"
#include "bt/solve/solver_type.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

// exit codes
constexpr int kExitOk = 0;
constexpr int kExitError = 1;      // the puzzle cannot be solved (as burrTxt --json)
constexpr int kExitUsage = 2;      // bad arguments or file

void usage() {
  std::fputs(
      "bt [options] file\n\n"
      "  file: puzzle file with the puzzle definition to solve\n\n"
      "  --json   machine-readable result (implies -d -q -r): one JSON object with\n"
      "           assemblies, solutions, dotlevel, level, totalmoves, solvetime;\n"
      "           with -R also moves and rotations\n"
      "  -d       take apart: only what comes apart is a solution\n"
      "  -R       also try 90 degree rotations when taking apart (implies -d)\n"
      "  -q       only print the summary line\n"
      "  -n       no newline after the summary line\n"
      "  -r       reduce the placements before solving\n"
      "  -C       strict colour rules: a voxel fits only a result voxel of its colour\n"
      "  -o n     the problem to solve (from 0)\n"
      "  -t n     threads for the assembler (0: all)\n"
      "  --solver TYPE\n"
      "           \"BurrTools Classic\" (classic, default), \"Andrew Crowell\" (crowell),\n"
      "           \"BurrTools 2\" (bt2; without -t: the hardware's threads less two, at most 16)\n"
      "  --no-simd\n"
      "           the plain search and scalar loops only (a benchmark switch)\n"
      "  --no-level-threads, --no-fast-analysis, --no-fast-rotations, --no-rotation-anchor\n"
      "           take-apart A/B switches (BURRTOOLS_NO_DISASM_PAR, NO_DISASM_FAST,\n"
      "           NO_ROT_FAST, NO_ROT_ANCHOR of the original)\n"
      "  --vector-memory MB\n"
      "           the most memory assembler 1's vector search may take (default 256;\n"
      "           0 turns it off; BURRTOOLS_HUANG_MEM_MB of the original)\n"
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
  bool levelThreads = true;
  bool fastAnalysis = true;
  bool fastRotations = true;
  bool rotationAnchor = true;
  unsigned int problem = 0;
  unsigned int threads = 0;
  unsigned int vectorMemoryMb = 256;
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
    } else if (a == "--no-level-threads") {
      o.levelThreads = false;
    } else if (a == "--no-fast-analysis") {
      o.fastAnalysis = false;
    } else if (a == "--no-fast-rotations") {
      o.fastRotations = false;
    } else if (a == "--no-rotation-anchor") {
      o.rotationAnchor = false;
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
    } else if (a == "--vector-memory") {
      if (i + 1 >= argc || !parseCount(argv[i + 1], &o.vectorMemoryMb)) {
        std::fprintf(stderr, "bt: %s requires a non-negative number\n", argv[i]);
        return kExitUsage;
      }
      i++;
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

// A level text (movesText(): "5.1.3", or with rotations "5R0.1R3"), as
// burrTxt's parse_dotlevel read it.
struct DotLevel {
  int level = 0;       ///< slides and rotations of the first removal
  int totalmoves = 0;  ///< all slides and rotations
  int moves = 0;       ///< all slides
  int rotations = 0;   ///< all rotations
};

std::optional<DotLevel> parseDotLevel(std::string_view text) {
  int firstSlides = -1;
  int firstRots = 0;
  int moves = 0;
  int rotations = 0;
  int segments = 0;

  std::size_t p = 0;
  auto digit = [&text, &p] { return p < text.size() && text[p] >= '0' && text[p] <= '9'; };
  while (p < text.size()) {
    if (!digit()) {
      return std::nullopt;
    }
    int slides = 0;
    while (digit()) {
      slides = slides * 10 + (text[p++] - '0');
    }
    int rots = 0;
    if (p < text.size() && text[p] == 'R') {
      p++;
      if (!digit()) {
        return std::nullopt;
      }
      while (digit()) {
        rots = rots * 10 + (text[p++] - '0');
      }
    }
    if (segments == 0) {
      firstSlides = slides;
      firstRots = rots;
    }
    moves += slides;
    rotations += rots;
    segments++;

    if (p == text.size()) {
      break;
    }
    if (text[p] != '.') {
      return std::nullopt;
    }
    p++;
  }

  if (segments == 0 || firstSlides < 0) {
    return std::nullopt;
  }
  return DotLevel{firstSlides + firstRots, moves + rotations, moves, rotations};
}

// The best level so far (burrTxt's asm_cb: the highest level, then the most
// moves; the first of equals stays).
struct BestLevel {
  bool has = false;
  std::string text;
  DotLevel stats;

  void consider(const std::string& t) {
    // burrTxt copied the text into 200 bytes first
    const std::string lev = t.substr(0, 199);
    const std::optional<DotLevel> d = parseDotLevel(lev);
    if (!d) {
      return;
    }
    if (!has || d->level > stats.level ||
        (d->level == stats.level && d->totalmoves > stats.totalmoves)) {
      has = true;
      text = lev;
      stats = *d;
    }
  }
};

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

  bt::MatrixOptions matrix;
  matrix.strictColors = o.strictColors;
  if (const bt::AssemblerError err = assembler->createMatrix(matrix);
      err != bt::AssemblerError::None) {
    return reportMatrixError(o, err, assembler->errorParam());
  }

  const auto start = std::chrono::steady_clock::now();

  if (o.reduce) {
    (void)assembler->reduce(bt::StopToken{});
  }

  const unsigned int budget = o.threads > 0 ? o.threads : bt::hardwareThreads();
  bt::ThreadPool pool(budget);
  bt::SearchOptions search;
  // BurrTools 2 picks its own count when none is given (bt2Threads), as
  // burrTxt's bt2ChooseAssemblerWorkers did
  search.threads = kind == bt::AssemblerKind::Bt2 ? bt::bt2Threads(o.threads) : o.threads;
  search.pool = &pool;
  search.vectorSearch = !o.noSimd;
  search.vectorMemoryBudget = std::uint64_t{o.vectorMemoryMb} << 20;
  if (o.noSimd) {
    search.kernels = bt::SimdKernels::none();
  }

  // The take-apart, on grids that have one (the others report solutions 0,
  // where the original's --json aborted). Its levels spread over threads of
  // their own, as burrTxt's helper pool: the assembly's are busy.
  std::unique_ptr<bt::ThreadPool> takeApartPool;
  std::unique_ptr<bt::Disassembler> disassembler;
  if (o.disassemble && bt::Disassembler::canDisassemble(puzzle)) {
    takeApartPool = std::make_unique<bt::ThreadPool>(budget);
    bt::DisassemblerOptions d;
    d.rotations = o.rotations;
    d.solver = o.solver;
    d.pool = takeApartPool.get();
    d.levelThreads = o.levelThreads;
    d.analysis.fast = o.fastAnalysis;
    d.analysis.rotations.fast = o.fastRotations;
    d.analysis.rotations.anchor = o.rotationAnchor;
    disassembler = std::make_unique<bt::Disassembler>(puzzle, problem, d);
  }

  unsigned long assemblies = 0;
  unsigned long solutions = 0;
  BestLevel best;
  const bt::RunEnd end = assembler->assemble(
      [&](const bt::Assembly& assembly) {
        assemblies++;
        if (disassembler) {
          const bt::TakeApartResult r = disassembler->disassemble(assembly);
          if (r.separation) {
            solutions++;
            const std::string level = r.separation->movesText();
            if (o.json) {
              best.consider(level);
            } else if (!o.quiet) {
              std::printf("level: %s\n", level.c_str());
            }
          }
        }
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
    std::printf("{\"assemblies\":%lu,\"solutions\":%lu", assemblies, solutions);
    if (best.has) {
      std::printf(",\"dotlevel\":\"%s\",\"level\":%d,\"totalmoves\":%d", best.text.c_str(),
                  best.stats.level, best.stats.totalmoves);
      if (o.rotations) {
        std::printf(",\"moves\":%d,\"rotations\":%d", best.stats.moves, best.stats.rotations);
      }
    } else {
      std::printf(",\"dotlevel\":null,\"level\":null,\"totalmoves\":null");
      if (o.rotations) {
        std::printf(",\"moves\":null,\"rotations\":null");
      }
    }
    std::printf(",\"solvetime\":%.3f}\n", seconds);
  } else {
    std::printf("%lu assemblies and %lu solutions found with %llu iterations in %.3f seconds",
                assemblies, solutions, static_cast<unsigned long long>(assembler->iterations()),
                seconds);
    if (o.newline) {
      std::printf("\n");
    }
  }
  return kExitOk;
}
