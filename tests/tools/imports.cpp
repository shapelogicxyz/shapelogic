// SPDX-License-Identifier: GPL-3.0-or-later
// bt_imports: the imports, the Puzzlecad export and grid conversion
// through the document, with the arguments of tests/oracle/imports_old.cpp
// (which does the same with the original's library); `oracle.py imports`
// compares the two.
//   bt_imports ps3d|scad <in> <out.xmpuzzle>
//   bt_imports scad-export <puzzle> <problem> <name>   (the file on stdout)
//   bt_imports convert <puzzle> <grid> <out.xmpuzzle>
// Exit 3 when refused, with the reason on stderr.

#include "shapelogic/app/document.h"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

namespace {

int refused(const bt::Error& e) {
  std::fprintf(stderr, "refused: %s\n", e.message.c_str());
  return 3;
}

int saved(bt::app::Document& d, const char* path) {
  const bt::Result<void> r = d.save(path);
  if (!r) {
    std::fprintf(stderr, "save failed: %s\n", r.error().message.c_str());
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4 && argc != 5) {
    std::fputs("usage: bt_imports ps3d|scad <in> <out> | scad-export <puzzle> <problem> <name> | "
               "convert <puzzle> <grid> <out>\n",
               stderr);
    return 2;
  }
  const std::string mode = argv[1];
  using bt::app::Document;
  if ((mode == "ps3d" || mode == "scad") && argc == 4) {
    bt::Result<Document> d = Document::importFile(
        mode == "ps3d" ? bt::app::ImportFormat::PuzzleSolver3D : bt::app::ImportFormat::Puzzlecad,
        argv[2]);
    if (!d) {
      return refused(d.error());
    }
    return saved(*d, argv[3]);
  }
  if ((mode == "scad-export" || mode == "convert") && argc == 5) {
    bt::Result<Document> d = Document::open(argv[2]);
    if (!d) {
      std::fprintf(stderr, "load failed: %s\n", d.error().message.c_str());
      return 1;
    }
    if (mode == "scad-export") {
      const auto prob = static_cast<unsigned int>(std::strtoul(argv[3], nullptr, 10));
      std::optional<bt::app::Id> problem;
      if (prob < d->problemIds().size()) {
        problem = d->problemIds()[prob];
      }
      const bt::Result<std::string> text = d->exportPuzzlecad(problem, argv[4]);
      if (!text) {
        return refused(text.error());
      }
      std::fwrite(text->data(), 1, text->size(), stdout);
      return 0;
    }
    bt::Result<Document> c =
        d->convertGrid(static_cast<bt::GridKind>(std::strtoul(argv[3], nullptr, 10)));
    if (!c) {
      return refused(c.error());
    }
    return saved(*c, argv[4]);
  }
  std::fputs("bad arguments\n", stderr);
  return 2;
}
