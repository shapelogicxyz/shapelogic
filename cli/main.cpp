// SPDX-License-Identifier: GPL-3.0-or-later
//
// bt: batch solving from the command line. Its --json output matches the
// original burrTxt --json so the comparison harness (tests/oracle) can put
// the two side by side. The solver arrives in stages 3-5; until then it only
// reports its version.
#include "bt/base/version.h"

#include <cstdio>
#include <string_view>

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--version") {
    std::printf("bt %.*s\n", static_cast<int>(bt::kVersion.size()), bt::kVersion.data());
    return 0;
  }
  std::fprintf(stderr, "bt: solving arrives in stages 3-5 (docs/PLAN.md)\n");
  return 2;
}
