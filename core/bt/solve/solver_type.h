// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The solver choices (BurrTools Classic, Andrew Crowell, BurrTools 2) and
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
  Bt2 = 2,      ///< Classic take-apart, dancing cells assembly split over threads
};

/// The number of solver types.
inline constexpr unsigned int kSolverTypeCount = 3;

/// "BurrTools Classic", "Andrew Crowell" or "BurrTools 2".
[[nodiscard]] std::string_view solverTypeLabel(SolverType t) noexcept;

/// The type with this index; Classic for an index out of range.
[[nodiscard]] SolverType solverTypeFromIndex(int v) noexcept;

/// The type a name stands for: case, spaces, hyphens and underscores are
/// ignored ("classic", "BurrTools Classic", "crowell", "fortran", "bt2",
/// "BurrTools 2", ...). Empty for an unknown name.
[[nodiscard]] std::optional<SolverType> solverTypeFromName(std::string_view name);

/// The assemblers.
enum class AssemblerKind : std::uint8_t {
  Assembler0,  ///< dancing links, every part exactly one piece
  Assembler1,  ///< Wei-Hwa Huang's cover with piece counts and ranges
  Bt2,         ///< dancing cells with work stealing
};

/// "assembler 0", "assembler 1" or "assembler bt2".
[[nodiscard]] std::string_view assemblerName(AssemblerKind k) noexcept;

/// The assembler a problem gets with a solver type: BurrTools 2 uses its
/// own when every part is one piece, the others assembler 0; everything
/// else goes to assembler 1.
[[nodiscard]] AssemblerKind chooseAssembler(const Problem& problem, SolverType solver);

/// A new assembler of the chosen kind for `problem` of `puzzle` (gridType_c
/// ::findAssembler); both must outlive it.
[[nodiscard]] std::unique_ptr<Assembler> makeAssembler(const Puzzle& puzzle,
                                                       const Problem& problem,
                                                       SolverType solver);

}  // namespace bt
