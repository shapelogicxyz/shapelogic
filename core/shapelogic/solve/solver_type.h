// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The solver choices (Complete (ShapeLogic), Fast (Crowell)) and
/// which assembler a problem gets with each.
/// Ported from src/lib/solvertype.h and gridType_c::findAssembler in
/// src/lib/gridtype.cpp.

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace bt {

class Assembler;
class Problem;
class Puzzle;

/// The solver pipeline. The numbers are the original's (solverType_e).
enum class SolverType : std::uint8_t {
  Classic = 0,  ///< complete take-apart, dancing links assembly
  Crowell = 1,  ///< Andrew Crowell's 90 degree take-apart heuristics, same assembly
};

/// The number of solver types.
inline constexpr unsigned int kSolverTypeCount = 2;

/// "Complete (ShapeLogic)" or "Fast (Crowell)".
[[nodiscard]] std::string_view solverTypeLabel(SolverType t) noexcept;

/// The type with this index; Classic for an index out of range.
[[nodiscard]] SolverType solverTypeFromIndex(int v) noexcept;

/// The type a name stands for: case, spaces, hyphens and underscores are
/// ignored ("classic", "Complete (ShapeLogic)", "crowell", "Fast (Crowell)",
/// "fortran", ...). Empty for an unknown name.
[[nodiscard]] std::optional<SolverType> solverTypeFromName(std::string_view name);

/// The assemblers.
enum class AssemblerKind : std::uint8_t {
  Assembler0,  ///< dancing links, every part exactly one piece
  Assembler1,  ///< Wei-Hwa Huang's cover with piece counts and ranges
};

/// "assembler 0" or "assembler 1".
[[nodiscard]] std::string_view assemblerName(AssemblerKind k) noexcept;

/// The assembler a problem gets with a solver type: assembler 0 when every
/// part is one piece, assembler 1 for everything else.
[[nodiscard]] AssemblerKind chooseAssembler(const Problem& problem, SolverType solver);

/// A new assembler of the chosen kind for `problem` of `puzzle` (gridType_c
/// ::findAssembler); both must outlive it.
[[nodiscard]] std::unique_ptr<Assembler> makeAssembler(const Puzzle& puzzle,
                                                       const Problem& problem,
                                                       SolverType solver);

}  // namespace bt
