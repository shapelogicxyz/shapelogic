// SPDX-License-Identifier: GPL-3.0-or-later
//
// bt-engine: the process the web front end talks to, JSON-RPC 2.0 over
// stdio (docs/decisions/0003-engine-process-json-rpc.md). Implemented in
// stage 7; until then it only reports its version.
#include "bt/base/version.h"

#include <cstdio>
#include <string_view>

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--version") {
    std::printf("bt-engine %.*s\n", static_cast<int>(bt::kVersion.size()), bt::kVersion.data());
    return 0;
  }
  std::fprintf(stderr, "bt-engine: the JSON-RPC interface arrives in stage 7 (docs/PLAN.md)\n");
  return 2;
}
