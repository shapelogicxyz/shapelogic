// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/solvertype.h and gridType_c::findAssembler
// (src/lib/gridtype.cpp).
#include "bt/solve/solver_type.h"

#include "bt/solve/assembler_0.h"
#include "bt/solve/assembler_1.h"
#include "bt/solve/assembler_bt2.h"

#include <string>

namespace bt {

std::string_view solverTypeLabel(SolverType t) noexcept {
  switch (t) {
    case SolverType::Classic:
      return "BurrTools Classic";
    case SolverType::Crowell:
      return "Andrew Crowell";
    case SolverType::Bt2:
      return "BurrTools 2";
  }
  return "BurrTools Classic";
}

SolverType solverTypeFromIndex(int v) noexcept {
  if (v < 0 || static_cast<unsigned int>(v) >= kSolverTypeCount) {
    return SolverType::Classic;
  }
  return static_cast<SolverType>(v);
}

std::optional<SolverType> solverTypeFromName(std::string_view name) {
  // lower case, without spaces, hyphens and underscores
  std::string n;
  for (const char ch : name) {
    auto c = static_cast<unsigned char>(ch);
    if (c == ' ' || c == '-' || c == '_') {
      continue;
    }
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<unsigned char>(c - 'A' + 'a');
    }
    n += static_cast<char>(c);
  }
  if (n.empty()) {
    return std::nullopt;
  }

  if (n == "classic" || n == "burrtoolsclassic" || n == "btclassic") {
    return SolverType::Classic;
  }
  if (n == "crowell" || n == "andrewcrowell" || n == "fortran") {
    return SolverType::Crowell;
  }
  if (n == "bt2" || n == "burrtools2") {
    return SolverType::Bt2;
  }
  return std::nullopt;
}

std::string_view assemblerName(AssemblerKind k) noexcept {
  switch (k) {
    case AssemblerKind::Assembler0:
      return "assembler 0";
    case AssemblerKind::Assembler1:
      return "assembler 1";
    case AssemblerKind::Bt2:
      return "assembler bt2";
  }
  return "assembler 0";
}

AssemblerKind chooseAssembler(const Problem& problem, SolverType solver) {
  // assembler_bt2_c::canHandle is assembler_0_c's
  if (solver == SolverType::Bt2) {
    return Assembler0::canHandle(problem) ? AssemblerKind::Bt2 : AssemblerKind::Assembler1;
  }
  return Assembler0::canHandle(problem) ? AssemblerKind::Assembler0 : AssemblerKind::Assembler1;
}

std::unique_ptr<Assembler> makeAssembler(const Puzzle& puzzle, const Problem& problem,
                                         SolverType solver) {
  switch (chooseAssembler(problem, solver)) {
    case AssemblerKind::Assembler0:
      return std::make_unique<Assembler0>(puzzle, problem);
    case AssemblerKind::Bt2:
      return std::make_unique<AssemblerBt2>(puzzle, problem);
    case AssemblerKind::Assembler1:
      return std::make_unique<Assembler1>(puzzle, problem);
  }
  return nullptr;
}

}  // namespace bt
