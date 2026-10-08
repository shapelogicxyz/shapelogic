// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from the argument handling and usage() of src/burrTxt.cpp, with
// burrTxt2's options (src/burrTxt2.cpp) as long options for --save.
#include "options.h"

#include "bt/base/version.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace bt::cli {

namespace {

constexpr int kExitOk = 0;
constexpr int kExitNoFile = 1;  // burrTxt: no file given
constexpr int kExitUsage = 2;

// Lower case, letters and digits only, so "Sliding Fast Solver" and
// "sliding-fast" both match (burrTxt's squash).
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
      "bt [options] file [options]\n\n"
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
      "  -d      solve: take apart; only print what solves\n"
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
        "    bt -h brick      bt -h sliding      bt -h stacking\n",
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
        "  --solver TYPE   (default: BurrTools Classic)\n"
        "            \"BurrTools Classic\"  complete take-apart (also: classic)\n"
        "            \"Andrew Crowell\"     90° take-apart heuristics (also: crowell)\n"
        "            \"BurrTools 2\"        Classic take-apart + dancing-cells assembly (also: bt2)\n"
        "\n  Switches for benchmarks and comparisons (the answers stay the same):\n"
        "  --no-simd            the plain search and scalar loops only\n"
        "  --no-level-threads   take apart on one thread per assembly\n"
        "  --no-fast-analysis, --no-fast-rotations, --no-rotation-anchor\n"
        "                       the take-apart's slower reference paths\n"
        "  --vector-memory MB   the most memory assembler 1's vector search may take\n"
        "                       (default 256; 0 turns it off)\n"
        "  --original-drawings  draw the take-apart's parts as burrTxt did (-p), with\n"
        "                       the wrong shapes below the first separation\n",
        stdout);
    return;
  }

  std::fprintf(stdout,
               "\n%s puzzles:\n"
               "  bt cannot solve %s puzzles yet: their files load, and -a reads them,\n"
               "  but a solve is refused.\n",
               kind == HelpKind::Sliding ? "sliding" : "stacking",
               kind == HelpKind::Sliding ? "sliding" : "stacking");
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
      std::printf("bt %.*s\n", static_cast<int>(kVersion.size()), kVersion.data());
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
        std::fprintf(stderr, "bt: -o takes a problem number (from 0) or all, not '%s'\n",
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
        std::fputs("bt: --save requires the file to write\n", stderr);
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
    } else if (a == "--limit" || a == "--vector-memory") {
      if (!hasValue ||
          !parseCount(argv[i + 1], a == "--limit" ? &o.solutionLimit : &o.vectorMemoryMb)) {
        std::fprintf(stderr, "bt: %s requires a non-negative number\n", argv[i]);
        return kExitUsage;
      }
      i++;
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
    } else if (a == "--original-drawings") {
      o.originalDrawings = true;
    } else if (a == "--nested-slides" || a == "--partial-nested" || a == "--high-memory" ||
               a == "--panex-dir" || a == "--panex-disk") {
      std::fprintf(stderr,
                   "bt: %s is for sliding and stacking puzzles, which bt cannot solve yet\n",
                   argv[i]);
      return kExitUsage;
    } else if (a.size() > 1 && a[0] == '-' && a[1] == '-') {
      std::fprintf(stderr, "bt: unknown option %s\n", argv[i]);
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
            std::fprintf(stderr, "bt: -%c cannot be clustered; give it as its own argument\n", c);
            return kExitUsage;
          default:
            std::fprintf(stderr, "bt: unknown option -%c\n", c);
            usage();
            return kExitUsage;
        }
      }
    } else if (o.file == nullptr) {
      o.file = argv[i];
    } else {
      std::fputs("bt: more than one file given\n", stderr);
      return kExitUsage;
    }
  }

  if (o.file == nullptr) {
    usage();
    return kExitNoFile;
  }

  if (o.json && (o.ask != Ask::None || !o.assemble || o.allProblems || o.printDisassembly ||
                 o.printAssembly)) {
    std::fputs("bt: --json cannot be combined with -a, -x, -o all, -p, or -s\n", stderr);
    return kExitUsage;
  }
  if (o.saveTo != nullptr && (o.json || o.ask != Ask::None || !o.assemble ||
                              o.printDisassembly || o.printAssembly)) {
    std::fputs("bt: --save cannot be combined with --json, -a, -x, -p, or -s\n", stderr);
    return kExitUsage;
  }
  if (o.saveTo == nullptr && (o.restart || o.countOnly || o.keepMirrors || o.keepRotations ||
                              o.dropDisassemblies || o.solutionLimit != 10)) {
    std::fputs(
        "bt: --restart, --limit, --count-only, --keep-mirrors, --keep-rotations and\n"
        "    --drop-disassemblies go with --save\n",
        stderr);
    return kExitUsage;
  }
  return std::nullopt;
}

}  // namespace bt::cli
