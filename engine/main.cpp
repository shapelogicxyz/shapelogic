// SPDX-License-Identifier: GPL-3.0-or-later
//
// bt-engine: the process the web front end talks to, JSON-RPC 2.0 over
// stdio (decisions 0003 and 0012; the methods are api/*.json, documented
// in docs/API.md).
#include "engine.h"

#include "shapelogic/base/version.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string_view>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--version") {
    std::printf("bt-engine %.*s\n", static_cast<int>(bt::kVersion.size()), bt::kVersion.data());
    return 0;
  }
  if (argc != 1) {
    std::fprintf(stderr, "usage: bt-engine [--version]\n"
                         "  speaks JSON-RPC 2.0 on stdin and stdout (docs/API.md)\n");
    return 2;
  }
#ifdef _WIN32
  // frames are raw bytes, and a line ends in \n alone: no text-mode
  // translation of either stream
  (void)_setmode(_fileno(stdout), _O_BINARY);
  (void)_setmode(_fileno(stdin), _O_BINARY);
#endif
  std::ios::sync_with_stdio(false);
  {
    bt::engine::Engine engine(std::cout, std::cerr);
    (void)engine.run(std::cin);
    // the document goes here: its solves pause and are waited for
  }
  std::cout.flush();
  std::cerr.flush();
  // the reader thread may still be blocked on stdin (engine.shutdown):
  // end without running static destructors under it
  std::_Exit(0);
}
