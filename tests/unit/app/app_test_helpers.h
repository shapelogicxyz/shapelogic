// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Helpers for the app tests: documents from the fixtures, and the undo /
// redo round trip every command is checked with.

#include "bt/app/document.h"
#include "bt/io/puzzle_file.h"

#include <catch2/catch_test_macros.hpp>

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
  bool operator==(const DocState&) const = default;
};

inline DocState state(const app::Document& d) {
  return DocState{io::writePuzzle(d.puzzle()), d.shapeIds(), d.colourIds(), d.problemIds()};
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

}  // namespace bt::test
