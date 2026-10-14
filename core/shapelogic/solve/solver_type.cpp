// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/solvertype.h and gridType_c::findAssembler
// (src/lib/gridtype.cpp).
#include "shapelogic/solve/solver_type.h"

#include "shapelogic/solve/assembler_0.h"
#include "shapelogic/solve/assembler_1.h"

#include <string>

namespace bt {

std::string_view solverTypeLabel(SolverType t) noexcept {
  switch (t) {
    case SolverType::Classic:
      return "Complete (ShapeLogic)";
    case SolverType::Crowell:
      return "Fast (Crowell)";
  }
  return "Complete (ShapeLogic)";
}

SolverType solverTypeFromIndex(int v) noexcept {
  if (v < 0 || static_cast<unsigned int>(v) >= kSolverTypeCount) {
    return SolverType::Classic;
  }
  return static_cast<SolverType>(v);
}

std::optional<SolverType> solverTypeFromName(std::string_view name) {
  // lower case, without spaces, hyphens, underscores and parentheses
  std::string n;
  for (const char ch : name) {
    auto c = static_cast<unsigned char>(ch);
    if (c == ' ' || c == '-' || c == '_' || c == '(' || c == ')') {
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

  if (n == "classic" || n == "complete" || n == "completeshapelogic" || n == "shapelogicclassic" ||
      n == "burrtoolsclassic" || n == "btclassic") {
    return SolverType::Classic;
  }
  if (n == "crowell" || n == "fast" || n == "fastcrowell" || n == "andrewcrowell" ||
      n == "fortran") {
    return SolverType::Crowell;
  }
  return std::nullopt;
}

std::string_view assemblerName(AssemblerKind k) noexcept {
  switch (k) {
    case AssemblerKind::Assembler0:
      return "assembler 0";
    case AssemblerKind::Assembler1:
      return "assembler 1";
  }
  return "assembler 0";
}

AssemblerKind chooseAssembler(const Problem& problem, SolverType /*solver*/) {
  return Assembler0::canHandle(problem) ? AssemblerKind::Assembler0 : AssemblerKind::Assembler1;
}

std::unique_ptr<Assembler> makeAssembler(const Puzzle& puzzle, const Problem& problem,
                                         SolverType solver) {
  switch (chooseAssembler(problem, solver)) {
    case AssemblerKind::Assembler0:
      return std::make_unique<Assembler0>(puzzle, problem);
    case AssemblerKind::Assembler1:
      return std::make_unique<Assembler1>(puzzle, problem);
  }
  return nullptr;
}

}  // namespace bt
