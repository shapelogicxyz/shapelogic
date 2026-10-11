// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Helpers for the app tests: documents from the fixtures, the undo / redo
// round trip every command is checked with, and a disassembly given the
// orientations a file without <dt> dropped.

#include "bt/app/document.h"
#include "bt/base/clone_ptr.h"
#include "bt/io/puzzle_file.h"
#include "bt/model/assembly.h"
#include "bt/model/disassembly.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace bt::test {

/// A document on a fixture puzzle.
inline app::Document openFixture(const std::string& name) {
  Result<app::Document> d = app::Document::open("tests/data/puzzles/" + name);
  REQUIRE(d.has_value());
  return std::move(*d);
}

/// What a document holds: the file it saves and its ids.
struct DocState {
  std::string file;
  std::vector<app::Id> shapes;
  std::vector<app::Id> colours;
  std::vector<app::Id> problems;
  std::vector<app::Id> rodSets;
  bool operator==(const DocState&) const = default;
};

inline DocState state(const app::Document& d) {
  return DocState{io::writePuzzle(d.puzzle()), d.shapeIds(), d.colourIds(), d.problemIds(),
                  d.rodSetIds()};
}

/// Runs a command that must change the document, then checks that undo
/// gives the document back byte for byte (and its ids), and redo the
/// edited one; the revision goes up with each. Returns what the command
/// returned.
template <class F>
auto roundTrip(app::Document& d, F&& cmd) {
  const DocState before = state(d);
  const std::uint64_t rev = d.revision();
  auto r = std::forward<F>(cmd)(d);
  REQUIRE(r.has_value());
  const DocState after = state(d);
  CHECK(after != before);
  CHECK(d.revision() > rev);
  CHECK(d.dirty());
  REQUIRE(d.canUndo());

  REQUIRE(d.undo().has_value());
  CHECK(state(d) == before);
  REQUIRE(d.redo().has_value());
  CHECK(state(d) == after);
  return r;
}

/// Runs a command that must be refused with `code` and change nothing.
template <class F>
void refused(app::Document& d, Errc code, F&& cmd) {
  const DocState before = state(d);
  const std::uint64_t rev = d.revision();
  const std::size_t steps = d.history().undo.size();
  auto r = std::forward<F>(cmd)(d);
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error().code == code);
  CHECK(state(d) == before);
  CHECK(d.revision() == rev);
  CHECK(d.history().undo.size() == steps);
}

/// A copy of a disassembly tree without turns whose states carry
/// orientations: every piece in its transformation in `assm`, as the solve
/// that found it stored it before a file without <dt> dropped them.
inline Separation withOrientations(const Separation& t, const Assembly& assm) {
  ClonePtr<Separation> removed;
  ClonePtr<Separation> left;
  if (t.getRemoved() != nullptr) {
    removed = ClonePtr<Separation>(
        std::make_unique<Separation>(withOrientations(*t.getRemoved(), assm)));
  }
  if (t.getLeft() != nullptr) {
    left = ClonePtr<Separation>(std::make_unique<Separation>(withOrientations(*t.getLeft(), assm)));
  }
  Separation out(std::move(removed), std::move(left), t.getPieces());
  std::vector<int> dt;
  for (unsigned int i = 0; i < t.getPieceNumber(); i++) {
    dt.push_back(static_cast<int>(assm.getTransformation(t.getPieceName(i))));
  }
  for (unsigned int k = 0; k < t.getNumStates(); k++) {
    State st = t.getState(k);
    st.setOrientations(dt);
    out.appendState(std::move(st));
  }
  return out;
}

}  // namespace bt::test
