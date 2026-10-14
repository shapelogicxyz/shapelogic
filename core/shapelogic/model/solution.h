// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// One found solution of a problem: its assembly and, when it was taken
/// apart, the disassembly (in full or as a summary).
/// Ported from src/lib/solution.{h,cpp} (XML reading and writing lives in
/// bt/io).

#include "shapelogic/model/assembly.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/xml_element.h"

#include <cstdint>
#include <optional>

namespace bt {

/// A solution. A value type. The assembly and solution numbers say in which
/// order the solver found it; the solution number is 0 when there is no
/// disassembly.
class Solution {
 public:
  /// An assembly without disassembly.
  Solution(Assembly assm, unsigned int assmNum);
  /// An assembly without disassembly that still carries a solution number
  /// (a file can have one).
  Solution(Assembly assm, unsigned int assmNum, unsigned int solNum);
  /// An assembly with its full disassembly.
  Solution(Assembly assm, unsigned int assmNum, Separation tree, unsigned int solNum);
  /// An assembly with the summary of its disassembly.
  Solution(Assembly assm, unsigned int assmNum, SeparationInfo info, unsigned int solNum);

  [[nodiscard]] const Assembly& getAssembly() const { return assembly_; }
  [[nodiscard]] Assembly& getAssembly() { return assembly_; }

  /// The full disassembly, or nullptr.
  [[nodiscard]] const Separation* getDisassembly() const {
    return tree_ ? &*tree_ : nullptr;
  }

  /// The summary, when only that is kept, or nullptr.
  [[nodiscard]] const SeparationInfo* getSeparationInfo() const {
    return treeInfo_ ? &*treeInfo_ : nullptr;
  }

  /// The disassembly or its summary, whichever there is, or nullptr.
  [[nodiscard]] const Disassembly* getDisassemblyInfo() const;

  /// A number naming the solution while the puzzle is open (not saved):
  /// given by its problem as it is added, it stays with the solution when
  /// the list is sorted, thinned or edited, and is never given twice in a
  /// problem. 0 until the problem gives one.
  [[nodiscard]] std::uint64_t getId() const { return id_; }
  void setId(std::uint64_t id) { id_ = id; }

  [[nodiscard]] unsigned int getAssemblyNumber() const { return assemblyNum_; }
  [[nodiscard]] unsigned int getSolutionNumber() const { return solutionNum_; }

  /// Replaces the full disassembly by its summary.
  void removeDisassembly();

  /// Sets the full disassembly, dropping any summary.
  void setDisassembly(Separation sep);

  /// Pieces s1 and s2 changed places in the problem.
  void exchangeShape(unsigned int s1, unsigned int s2);

  /// Pieces [start, start+count) were removed from the problem.
  void removePieces(unsigned int start, unsigned int count);

  /// `count` unplaced pieces were inserted at `start`.
  void addNonPlacedPieces(unsigned int start, unsigned int count);

  /// File content of the solution element this version does not model.
  [[nodiscard]] const PreservedXml& preserved() const { return preserved_; }
  [[nodiscard]] PreservedXml& preserved() { return preserved_; }

 private:
  Assembly assembly_;
  std::optional<Separation> tree_;
  std::optional<SeparationInfo> treeInfo_;
  unsigned int assemblyNum_;
  unsigned int solutionNum_;
  std::uint64_t id_ = 0;
  PreservedXml preserved_;
};

}  // namespace bt
