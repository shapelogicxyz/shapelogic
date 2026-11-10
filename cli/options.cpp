// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from the argument handling and usage() of src/burrTxt.cpp, with
// burrTxt2's options (src/burrTxt2.cpp) as long options for --save.
#include "options.h"

#include "shapelogic/base/version.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace bt::cli {

namespace {

constexpr int kExitOk = 0;
constexpr int kExitNoFile = 1;  // burrTxt: no file given
constexpr int kExitUsage = 2;

// Lower case, letters and digits only, so "Sliding Puzzle Solver" and
// "sliding-puzzle-solver" both match (burrTxt's squash).
std::string squash(std::string_view s) {
  std::string out;
  for (char c : s) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      out += c;
    }
  }
  return out;
}

// A non-negative number; false when `s` is not one.
bool parseCount(const char* s, unsigned int* out) {
  char* end = nullptr;
  const long v = std::strtol(s, &end, 10);
  if (end == s || *end != 0 || v < 0) {
    return false;
  }
  *out = static_cast<unsigned int>(v);
  return true;
}

}  // namespace

void usage(HelpKind kind) {
  std::fputs(
      "shapelogic_cli [options] file [options]\n\n"
      "  file: puzzle file with the puzzle definition to solve\n"
      "  The file's puzzle type (brick, sliding or stacking) decides which solvers\n"
      "  and options apply. Brick covers every assembly and take-apart grid.\n\n"
      "  -h [TYPE], --help [TYPE]\n"
      "          show this help; with TYPE brick, sliding or stacking, also show\n"
      "          that type's options and solvers\n"
      "  --json  machine-readable result for batch tools (implies -d -q -r;\n"
      "          prints one JSON object with the highest disassembly level.\n"
      "          Fields: assemblies, solutions, dotlevel, level, totalmoves,\n"
      "          solvetime (seconds); with -R also moves (linear) and rotations)\n"
      "  --progress\n"
      "          print to stderr, once a second, how far the solve is\n"
      "  Short options may be combined (e.g. -dR, -rq).\n"
      "  -d      solve: take apart (brick), find the slide path (sliding) or the rod\n"
      "          transfers (stacking); only print what solves\n"
      "  -p      print the moves of each solution\n"
      "  -s      print the assembly\n"
      "  -q      be quiet and only print statistics\n"
      "  -n      don't print a newline at the end of the line\n"
      "  -o n    select the problem to solve\n"
      "  -o all  solves all problems in file\n"
      "  -x      only redisassemble the given solutions\n"
      "  --solver TYPE\n"
      "          solver to use; the choices depend on the puzzle type (see -h TYPE).\n"
      "          Quotes are needed if TYPE has spaces; case and spaces are ignored.\n"
      "  -a      ask for information about the current puzzle, the next letters must be:\n"
      "     s0   print solutions with the only the used pieces\n"
      "     s1   print solutions including the assemblies\n"
      "     c    print comment\n"
      "  --save FILE\n"
      "          solve as the GUI does and write the solved puzzle to FILE (burrTxt2):\n"
      "          a paused solve in the file carries on, the solutions are kept\n"
      "          sorted by moves; takes -d, -R, -C, -t, -o, --solver, --progress, -q\n"
      "          and these:\n"
      "    --restart             drop what the file found before and start over\n"
      "    --limit n             keep the n best solutions (default 10; 0 keeps all)\n"
      "    --count-only          count the solutions, keep none\n"
      "    --keep-mirrors        keep mirrored solutions\n"
      "    --keep-rotations      keep rotated solutions\n"
      "    --drop-disassemblies  keep only the levels of each take-apart, not its moves\n"
      "  --version\n"
      "          print the version\n",
      stdout);

  if (kind == HelpKind::Any) {
    std::fputs(
        "\n  For the options and solvers of one puzzle type:\n"
        "    shapelogic_cli -h brick      shapelogic_cli -h sliding      shapelogic_cli -h stacking\n",
        stdout);
    return;
  }

  if (kind == HelpKind::Brick) {
    std::fputs(
        "\nbrick puzzles:\n"
        "  -r      reduce the placements before starting to solve the puzzle\n"
        "  -R      also try 90 degree piece rotations during disassembly (implies -d)\n"
        "  -C      strict color restrictions: a voxel fits only a result voxel of the same color\n"
        "          (a colored voxel does not also fit a neutral one, and a neutral voxel\n"
        "          fits only a neutral result voxel)\n"
        "  -t n    set number of worker threads for assembler (0 = auto)\n"
        "  --solver TYPE   (default: Complete (ShapeLogic))\n"
        "            \"Complete (ShapeLogic)\"  complete take-apart (also: classic)\n"
        "            \"Fast (Crowell)\"         90° take-apart heuristics (also: crowell)\n"
        "\n  Switches for benchmarks and comparisons (the answers stay the same):\n"
        "  --no-simd            the plain search and scalar loops only\n"
        "  --no-row-check       check each assembly against its rotations as built, not\n"
        "                       on the rows of the matrix\n"
        "  --no-level-threads   take apart on one thread per assembly\n"
        "  --no-fast-analysis, --no-fast-rotations, --no-rotation-anchor\n"
        "                       the take-apart's slower reference paths\n"
        "  --derived-checks     on cubes and triangular prisms, take apart with the\n"
        "                       movement checks derived from the cells, as the other\n"
        "                       grids do, not with the original's tables\n"
        "  --vector-memory MB   the most memory assembler 1's vector search may take\n"
        "                       (default 256; 0 turns it off)\n"
        "  --split-tasks N, --split-depth N\n"
        "                       split a search on several threads into at least N tasks,\n"
        "                       at most N pieces deep (0: the assembler's choice)\n"
        "  --original-drawings  draw the take-apart's parts as burrTxt did (-p), with\n"
        "                       the wrong shapes below the first separation\n",
        stdout);
    return;
  }

  if (kind == HelpKind::Sliding) {
    std::fputs(
        "\nsliding puzzles:\n"
        "  -r      reduce the placements before finding the start layouts\n"
        "  -t n    set number of worker threads for finding start layouts and for the\n"
        "          Sliding Puzzle Solver (0 = auto)\n"
        "  --nested-slides\n"
        "          a piece may carry the pieces nested inside its outline, such as a\n"
        "          piece in another piece's pocket. Pieces that only touch never move\n"
        "          together.\n"
        "  --partial-nested\n"
        "          with --nested-slides, a piece also carries the pieces partly nested\n"
        "          in its outline: some of their cells inside it, the rest sticking out.\n"
        "          Implies --nested-slides.\n"
        "  --vertical-moves\n"
        "          on a tray more than one layer deep, a piece may also move up and\n"
        "          down; without it every piece stays in its layer\n"
        "  --any-orientation\n"
        "          a piece with neither a start nor a goal on the board may be placed in\n"
        "          any quarter turn in its layers; without it every piece is placed as\n"
        "          drawn. A piece never turns as it slides.\n"
        "  --mirrored-orientations\n"
        "          with --any-orientation, those pieces may also be mirrored (turned\n"
        "          over). Implies --any-orientation.\n"
        "  --high-memory\n"
        "          let the search keep up to half of this computer's memory in\n"
        "          arrangements before it writes to disk, instead of about 2 GB\n"
        "  --solver TYPE   \"Sliding Puzzle Solver\" (also: sliding), the only one: it\n"
        "          finds the fewest moves or proves there is no solution, on every core\n"
        "          (-t n to limit); what outgrows its memory goes to disk, so free disk\n"
        "          space limits it\n"
        "  The Sliding Puzzle Solver writes to $SHAPELOGIC_SLIDE_DIR, or the user's cache\n"
        "  folder, and deletes its files when done. SHAPELOGIC_SLIDE_DISK_GB limits the\n"
        "  disk it may take (default: half the free space, at most 200 GB; a larger value\n"
        "  is held to the free space but 2 GB or a twentieth of it). It also stops while\n"
        "  100 MB and room to save itself are left on the disk. A search that stops on\n"
        "  disk is saved and the run ends there: run the same command again to carry it\n"
        "  on. Saved searches unused for 30 days are deleted.\n"
        "  --restart       throw away the saved searches and start over (no --save needed)\n"
        "\n  Switches for benchmarks and comparisons (the answers stay the same):\n"
        "  --slide-legacy   the original search over text keys\n"
        "  --slide-apart    like pieces told apart instead of searched as one\n"
        "  --slide-trace    each level of the search on stderr\n"
        "  --slide-step N   pieces stop only where they are a multiple of N cells from\n"
        "                   their start in x and y (a smaller search; a path it finds is\n"
        "                   real, but no solution then proves nothing)\n",
        stdout);
    return;
  }

  std::fputs(
      "\nstacking puzzles:\n"
      "  The start and goal stacks come from the file. A move is one disc going\n"
      "  from the top of one rod to the top of another. With Panex columns a\n"
      "  disc of size n sits at most n places below the top of its rod.\n"
      "  --high-memory\n"
      "          let the search hold up to half of this computer's memory in\n"
      "          stackings, instead of stopping at about 2 GB\n"
      "  --solver TYPE   (default: Stacking Solver)\n"
      "            \"Stacking Solver\"  fewest rod transfers (also: stacking)\n"
      "            \"Panex Solver\"     fewest rod transfers for Panex columns, built for\n"
      "                                large towers: searches from both ends, holds only the\n"
      "                                newest levels and uses every core (-t n to limit)\n"
      "                                (also: panex)\n"
      "  Panex Solver searches are saved when stopped (Ctrl-C), when they run out\n"
      "  of memory, and every 20 minutes; the same command carries a saved search on.\n"
      "  --restart       discard a saved search and start over\n"
      "  --panex-dir DIR keep saved searches in DIR (default: $SHAPELOGIC_PANEX_DIR, or\n"
      "                  the user's cache folder)\n"
      "  --panex-disk GB disk saved levels may take (default: $SHAPELOGIC_PANEX_DISK_GB,\n"
      "                  or half the free space, at most 200); less disk makes tracing\n"
      "                  the path back slower\n"
      "  With --save, -d searches the transfers as the GUI's Solve did (burrTxt2 had\n"
      "  no stacking solver); a saved search carries on unless --restart.\n"
      "\n  Switches for benchmarks and comparisons (the answers stay the same):\n"
      "  --no-tower-rule  search the classic tower too, instead of its recursion\n"
      "  --panex-log      each level of a Panex search on stderr\n",
      stdout);
}

std::optional<int> parseArgs(int argc, char** argv, Options& o) {
  for (int i = 1; i < argc; i++) {
    const std::string_view a = argv[i];
    const bool hasValue = i + 1 < argc;

    if (a == "--progress") {
      o.progress = true;
    } else if (a == "--json") {
      o.json = true;
      o.disassemble = true;
      o.quiet = true;
      o.reduce = true;
    } else if (a == "-h" || a == "--help") {
      HelpKind k = HelpKind::Any;
      if (hasValue) {
        const std::string t = squash(argv[i + 1]);
        if (t == "brick") {
          k = HelpKind::Brick;
        } else if (t == "sliding" || t == "slider") {
          k = HelpKind::Sliding;
        } else if (t == "stacking") {
          k = HelpKind::Stacking;
        }
      }
      usage(k);
      return kExitOk;
    } else if (a == "--version") {
      std::printf("shapelogic_cli %.*s\n", static_cast<int>(kVersion.size()), kVersion.data());
      return kExitOk;
    } else if (a == "--solver") {
      if (!hasValue) {
        usage();
        return kExitUsage;
      }
      o.solverName = argv[++i];
    } else if (a == "-t") {
      if (!hasValue) {
        std::fputs("-t requires a numeric argument\n", stdout);
        return kExitUsage;
      }
      if (!parseCount(argv[i + 1], &o.threads)) {
        std::fputs("-t requires a non-negative number\n", stdout);
        return kExitUsage;
      }
      i++;
    } else if (a == "-o") {
      if (!hasValue) {
        usage();
        return kExitUsage;
      }
      if (std::string_view(argv[i + 1]) == "all") {
        o.allProblems = true;
      } else if (!parseCount(argv[i + 1], &o.problem)) {
        std::fprintf(stderr, "shapelogic_cli: -o takes a problem number (from 0) or all, not '%s'\n",
                     argv[i + 1]);
        return kExitUsage;
      }
      i++;
    } else if (a == "-a") {
      if (!hasValue) {
        usage();
        return kExitUsage;
      }
      const std::string_view what = argv[i + 1];
      if (what == "s0") {
        o.ask = Ask::SolutionPieces;
      } else if (what == "s1") {
        o.ask = Ask::SolutionAssemblies;
      } else if (what == "c") {
        o.ask = Ask::Comment;
      } else {
        usage();
        return kExitUsage;
      }
      i++;
    } else if (a == "--save") {
      if (!hasValue) {
        std::fputs("shapelogic_cli: --save requires the file to write\n", stderr);
        return kExitUsage;
      }
      o.saveTo = argv[++i];
    } else if (a == "--restart") {
      o.restart = true;
    } else if (a == "--count-only") {
      o.countOnly = true;
    } else if (a == "--keep-mirrors") {
      o.keepMirrors = true;
    } else if (a == "--keep-rotations") {
      o.keepRotations = true;
    } else if (a == "--drop-disassemblies") {
      o.dropDisassemblies = true;
    } else if (a == "--limit" || a == "--vector-memory" || a == "--split-tasks" ||
               a == "--split-depth") {
      unsigned int* const value = a == "--limit"           ? &o.solutionLimit
                                  : a == "--vector-memory" ? &o.vectorMemoryMb
                                  : a == "--split-tasks"   ? &o.splitTasks
                                                           : &o.splitDepth;
      if (!hasValue || !parseCount(argv[i + 1], value)) {
        std::fprintf(stderr, "shapelogic_cli: %s requires a non-negative number\n", argv[i]);
        return kExitUsage;
      }
      i++;
    } else if (a == "--no-simd") {
      o.noSimd = true;
    } else if (a == "--no-row-check") {
      o.rowRotationCheck = false;
    } else if (a == "--no-level-threads") {
      o.levelThreads = false;
    } else if (a == "--no-fast-analysis") {
      o.fastAnalysis = false;
    } else if (a == "--no-fast-rotations") {
      o.fastRotations = false;
    } else if (a == "--no-rotation-anchor") {
      o.rotationAnchor = false;
    } else if (a == "--derived-checks") {
      o.derivedChecks = true;
    } else if (a == "--original-drawings") {
      o.originalDrawings = true;
    } else if (a == "--nested-slides") {
      o.nestedSlides = true;
    } else if (a == "--partial-nested") {
      o.nestedSlides = true;
      o.partialNested = true;
    } else if (a == "--vertical-moves") {
      o.verticalMoves = true;
    } else if (a == "--any-orientation") {
      o.anyOrientation = true;
    } else if (a == "--mirrored-orientations") {
      o.anyOrientation = true;
      o.mirroredOrientations = true;
    } else if (a == "--high-memory") {
      o.highMemory = true;
    } else if (a == "--slide-legacy") {
      o.slideLegacy = true;
    } else if (a == "--slide-apart") {
      o.slideApart = true;
    } else if (a == "--slide-step") {
      unsigned int step = 0;
      if (!hasValue || !parseCount(argv[i + 1], &step) || step == 0) {
        std::fprintf(stderr, "shapelogic_cli: %s requires a positive number\n", argv[i]);
        return kExitUsage;
      }
      o.slideStep = static_cast<int>(step);
      i++;
    } else if (a == "--slide-trace") {
      o.slideTrace = true;
    } else if (a == "--panex-dir") {
      if (!hasValue) {
        std::fputs("shapelogic_cli: --panex-dir requires a folder\n", stderr);
        return kExitUsage;
      }
      o.panexDir = argv[++i];
    } else if (a == "--panex-disk") {
      // burrTxt read it with atof
      if (!hasValue) {
        std::fputs("shapelogic_cli: --panex-disk requires a number of GB\n", stderr);
        return kExitUsage;
      }
      o.panexDiskGB = std::atof(argv[++i]);
    } else if (a == "--no-tower-rule") {
      o.noTowerRule = true;
    } else if (a == "--panex-log") {
      o.panexLog = true;
    } else if (a.size() > 1 && a[0] == '-' && a[1] == '-') {
      std::fprintf(stderr, "shapelogic_cli: unknown option %s\n", argv[i]);
      return kExitUsage;
    } else if (a.size() > 1 && a[0] == '-') {
      for (const char c : a.substr(1)) {
        switch (c) {
          case 'd':
            o.disassemble = true;
            break;
          case 'p':
            o.printDisassembly = true;
            break;
          case 's':
            o.printAssembly = true;
            break;
          case 'r':
            o.reduce = true;
            break;
          case 'R':
            o.rotations = true;
            o.disassemble = true;
            break;
          case 'C':
            o.strictColors = true;
            break;
          case 'n':
            o.newline = false;
            break;
          case 'x':
            o.assemble = false;
            break;
          case 'q':
            o.quiet = true;
            o.printDisassembly = false;
            o.printAssembly = false;
            break;
          case 'o':
          case 'a':
            std::fprintf(stderr, "shapelogic_cli: -%c cannot be clustered; give it as its own argument\n", c);
            return kExitUsage;
          default:
            std::fprintf(stderr, "shapelogic_cli: unknown option -%c\n", c);
            usage();
            return kExitUsage;
        }
      }
    } else if (o.file == nullptr) {
      o.file = argv[i];
    } else {
      std::fputs("shapelogic_cli: more than one file given\n", stderr);
      return kExitUsage;
    }
  }

  if (o.file == nullptr) {
    usage();
    return kExitNoFile;
  }

  if (o.json && (o.ask != Ask::None || !o.assemble || o.allProblems || o.printDisassembly ||
                 o.printAssembly)) {
    std::fputs("shapelogic_cli: --json cannot be combined with -a, -x, -o all, -p, or -s\n", stderr);
    return kExitUsage;
  }
  if (o.saveTo != nullptr && (o.json || o.ask != Ask::None || !o.assemble ||
                              o.printDisassembly || o.printAssembly)) {
    std::fputs("shapelogic_cli: --save cannot be combined with --json, -a, -x, -p, or -s\n", stderr);
    return kExitUsage;
  }
  // --restart of a stacking puzzle goes without --save too: checked once
  // the puzzle's type is known
  if (o.saveTo == nullptr && (o.countOnly || o.keepMirrors || o.keepRotations ||
                              o.dropDisassemblies || o.solutionLimit != 10)) {
    std::fputs(
        "shapelogic_cli: --restart, --limit, --count-only, --keep-mirrors, --keep-rotations and\n"
        "    --drop-disassemblies go with --save\n",
        stderr);
    return kExitUsage;
  }
  return std::nullopt;
}

}  // namespace bt::cli
