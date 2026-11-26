// SPDX-License-Identifier: GPL-3.0-or-later
//
// shapelogic_engine: the process the web front end talks to, JSON-RPC 2.0 over
// stdio (decisions 0003 and 0012; the methods are api/*.json, documented
// in docs/API.md).
#include "app_log.h"
#include "engine.h"

#include "shapelogic/base/system.h"
#include "shapelogic/base/version.h"

#include <csignal>
#include <cstdint>
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
    const std::string_view version = bt::versionString();
    std::printf("shapelogic_engine %.*s\n", static_cast<int>(version.size()), version.data());
    return 0;
  }
  if (argc != 1) {
    std::fprintf(stderr, "usage: shapelogic_engine [--version]\n"
                         "  speaks JSON-RPC 2.0 on stdin and stdout (docs/API.md)\n");
    return 2;
  }
#ifdef _WIN32
  // frames are raw bytes, and a line ends in \n alone: no text-mode
  // translation of either stream
  (void)_setmode(_fileno(stdout), _O_BINARY);
  (void)_setmode(_fileno(stdin), _O_BINARY);
#endif
#ifndef _WIN32
  // The app can go without a word (a crash, a force quit): a write to its
  // closed pipe then fails (Output::failed ends the run loop) rather than
  // raise SIGPIPE, which would end the engine before the document's
  // destructor has paused and saved the solves (cleanup item 15.1). On
  // Windows such a write only ever fails.
  (void)std::signal(SIGPIPE, SIG_IGN);
#endif
  std::ios::sync_with_stdio(false);
  // std::cin is tied to std::cout: each read would flush std::cout first.
  // The reader thread reads std::cin while the main thread writes std::cout,
  // and two flushes of one unsynchronised buffer at once can write a reply
  // twice (seen on Linux when the tests run side by side). Only the main
  // thread flushes std::cout.
  std::cin.tie(nullptr);
  // a slide search reads many level files at once
  const std::uint64_t openFiles = bt::raiseOpenFileLimit(4096);
  // the app's log learns at once which engine runs, whatever the log setting
  std::cerr << bt::engine::startLine(openFiles) << '\n';
  std::cerr.flush();
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
