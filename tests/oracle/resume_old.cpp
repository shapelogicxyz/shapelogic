// SPDX-License-Identifier: GPL-3.0-or-later
// resume_old: pause and continue assembler searches with the original's
// library, for `oracle.py resume-check`, which checks that a search paused
// in one program continues in the other (decision 0006). The counterpart
// is tests/tools/resume.cpp (bt_resume); keep the two in step.
//
//   resume_old full   <puzzle> <solver> <threads> <plain> <reduce>
//   resume_old pause  <puzzle> <solver> <threads> <plain> <reduce> <stopAfter>
//   resume_old resume <puzzle> <solver> <threads> <plain> <reduce> <positionFile>
//
// <solver> is classic or bt2 (the assembler is the one findAssembler picks
// for it); <plain> 1 runs without the vector search (SHAPELOGIC_NO_SIMD).
// BurrTools 2 searches over several threads through bt2Assemble, as burrTxt
// does. pause prints "VERSION v" and "TEXT t" (the saved position) after
// the assemblies; a position file holds those two lines. Every assembly
// found is printed as "A t,x,y,z;...", and a position that cannot be
// restored as "ERR <code>".

#include "lib/assembler.h"
#include "lib/assembly.h"
#include "lib/bt2_assemble.h"
#include "lib/gridtype.h"
#include "lib/problem.h"
#include "lib/puzzle.h"
#include "lib/solvertype.h"
#include "tools/xml.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace {

std::string fingerprint(const assembly_c& a) {
  std::string s;
  for (unsigned int i = 0; i < a.placementCount(); i++) {
    if (a.isPlaced(i))
      s += std::to_string(a.getTransformation(i)) + "," + std::to_string(a.getX(i)) + "," +
           std::to_string(a.getY(i)) + "," + std::to_string(a.getZ(i)) + ";";
    else
      s += "-;";
  }
  return s;
}

/* prints each assembly and asks to stop after stopAfter of them */
class printer_c : public assembler_cb {
public:
  unsigned long stopAfter = 0;
  unsigned long count = 0;
  bool assembly(std::unique_ptr<assembly_c> a) override {
    std::printf("A %s\n", fingerprint(*a).c_str());
    count++;
    return stopAfter == 0 || count < stopAfter;
  }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 7) {
    std::fputs("usage: resume_old full|pause|resume <puzzle> <solver> <threads> <plain> <reduce> [arg]\n",
               stderr);
    return 2;
  }
  const std::string cmd = argv[1];
  const bool bt2 = std::strcmp(argv[3], "bt2") == 0;
  const unsigned int threads = (unsigned int)std::atoi(argv[4]);
  if (std::atoi(argv[5]) != 0)
    setenv("SHAPELOGIC_NO_SIMD", "1", 1);
  const bool reduce = std::atoi(argv[6]) != 0;

  auto p = puzzle_c::load(argv[2]);
  if (!p) {
    std::fprintf(stderr, "cannot load %s\n", argv[2]);
    return 2;
  }
  problem_c* problem = p->getProblem(0);
  std::unique_ptr<assembler_c> assm =
      p->getGridType()->findAssembler(*problem, true, bt2 ? SOLVER_BT2 : SOLVER_CLASSIC);
  assm->setNumThreads(threads);
  if (assm->createMatrix(false, false, false) != assembler_c::ERR_NONE) {
    std::fputs("createMatrix failed\n", stderr);
    return 2;
  }
  if (reduce)
    assm->reduce();

  if (cmd == "resume") {
    if (argc < 8)
      return 2;
    std::ifstream in(argv[7]);
    std::string version, text;
    std::getline(in, version);
    std::getline(in, text);
    const assembler_c::errState err = assm->setPosition(text.c_str(), version.c_str());
    if (err != assembler_c::ERR_NONE) {
      std::printf("ERR %d\n", (int)err);
      return 0;
    }
  }

  printer_c cb;
  if (cmd == "pause") {
    if (argc < 8)
      return 2;
    cb.stopAfter = std::strtoul(argv[7], nullptr, 10);
  }

  if (bt2)
    bt2Assemble(assm.get(), &cb, threads);
  else
    assm->assemble(&cb);

  if (cmd == "pause") {
    std::ostringstream str;
    xmlWriter_c xml(str);
    assm->save(xml);
    const std::string s = str.str();
    const size_t tag = s.find("<assembler");
    const size_t v = s.find("version=\"", tag) + 9;
    const size_t ve = s.find('"', v);
    const size_t c = s.find('>', tag) + 1;
    const size_t ce = s.find("</assembler>", c);
    std::printf("VERSION %s\n", s.substr(v, ve - v).c_str());
    std::printf("TEXT %s\n", s.substr(c, ce - c).c_str());
  }
  std::printf("FINISHED %d\n", assm->getFinished() >= 1 ? 1 : 0);
  return 0;
}
