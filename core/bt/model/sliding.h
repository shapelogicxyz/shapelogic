// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Sliding puzzles: the tray and its start/goal shapes, the starts and
/// goals stamped on them, the walls, and what a solve needs set up first.
///
/// A sliding puzzle (GridKind::Sliding) has pieces, which are plain shapes,
/// and start/goal shapes, whose names begin "sg:<n>:": a tray whose empty
/// cells are walls, whose variable cells are corridors a piece may cross
/// but not stop on, with each cell's colour the start mark of a piece
/// (shape index + 1) and its goal mark (Voxel::getGoalPiece) the goal of
/// one. Each start/goal shape has a problem of its own with that shape as
/// its result (syncSlidingProblems); the assembler finds the start layout,
/// whose pieces the colours lock to their starts (refreshStartLocks), and
/// the slide search (bt/solve/sliding_search.h) takes it to the goal.
///
/// An older model, a tray and a goal map painted in piece colours
/// (ensureSetup, placeStart, placeGoal), is still read by the solver; the
/// tests build such puzzles.
///
/// Ported from src/lib/sliding.{h,cpp} (the tray and start/goal part; the
/// search is in bt/solve/sliding_search.h).

#include <string>
#include <utility>
#include <vector>

namespace bt {
class Problem;
class Puzzle;
class Voxel;
}  // namespace bt

namespace bt::sliding {

/// True when the puzzle is on the sliding grid.
[[nodiscard]] bool isSliding(const Puzzle& puzzle);

/// True when the shape is a start/goal shape (its name begins "sg:").
[[nodiscard]] bool isStartGoalShape(const Voxel* v);
/// The number n of a start/goal shape "sg:<n>:"; 0 for another shape.
[[nodiscard]] int startGoalNumber(const Voxel* v);
/// The name the user gave a start/goal shape (after "sg:<n>:"); the whole
/// name of another shape.
[[nodiscard]] std::string startGoalUserName(const Voxel* v);
/// Sets the user's part of a start/goal shape's name, keeping its number;
/// the whole name of another shape.
void setStartGoalUserName(Voxel* v, const std::string& user);
/// The name shown: "(START/GOAL n)" and the user's name for a start/goal
/// shape, the name for another.
[[nodiscard]] std::string displayName(const Voxel* v);
/// True for a shape whose name begins "__sggoal:" (hidden from the lists).
[[nodiscard]] bool isHiddenSlidingShape(const Voxel* v);

/// Adds a start/goal shape of sx by sy cells, one layer, all filled, with
/// the next free number. Returns its index.
unsigned int addStartGoalShape(Puzzle& puzzle, unsigned int sx, unsigned int sy);

/// One problem per start/goal shape, its result locked to that shape: the
/// problems of no start/goal shape go, a missing one is added
/// ("START/GOAL n"), every piece stamped on a shape is a part of its
/// problem (at least one), and the start locks are refreshed.
void syncSlidingProblems(Puzzle& puzzle);

/// True when the piece is stamped on the start or goal of the problem's
/// result (a start/goal shape): it must stay a part.
[[nodiscard]] bool shapeIsRequired(const Puzzle& puzzle, const Problem& problem,
                                   unsigned int shapeId);

/// Toggles the mark of shapeId on one cell of a start/goal shape, the
/// start or (goal) the goal. False when the cell is not floor or the shape
/// is beyond the 63 that can be marked.
bool toggleCellMark(Voxel* tray, int x, int y, unsigned int shapeId, bool goal);

/// Marks the whole of a piece as its start (goal false) or its goal on a
/// start/goal shape, as the piece is drawn: its anchor cell, the leftmost
/// cell of its top row (y rises upwards, as in the grid editor), at (x, y).
/// Replaces the piece's earlier start or goal. Empty on success, else why
/// it does not fit, and nothing changes.
std::string stampPiece(Voxel* tray, const Voxel* piece, unsigned int shapeId, int x, int y,
                       bool goal);

/// The tray cells stampPiece would mark, whether they fit or not.
[[nodiscard]] std::vector<std::pair<int, int>> stampCells(const Voxel* piece, int x, int y);

/// Why stampPiece would refuse, or empty when the piece fits; changes nothing.
[[nodiscard]] std::string stampFits(const Voxel* tray, const Voxel* piece, unsigned int shapeId,
                                    int x, int y, bool goal);

/// True when shapeId's start or goal is exactly where stampPiece would put it.
[[nodiscard]] bool stampIsAt(const Voxel* tray, const Voxel* piece, unsigned int shapeId, int x,
                             int y, bool goal);

/// After piece shapeId was edited: removes its start and goal from every
/// start/goal shape where they are no longer the piece's shape. Returns the
/// start/goal shapes that lost one; an edit that leaves the shape as it
/// was, such as moving it in its grid, removes nothing.
std::vector<unsigned int> dropChangedStamps(Puzzle& puzzle, unsigned int shapeId);

/// Removes shapeId's start or goal; true when it had one.
bool clearStamp(Voxel* tray, unsigned int shapeId, bool goal);

/// The piece whose start or goal covers (x, y), or ~0u.
[[nodiscard]] unsigned int stampAt(const Voxel* tray, int x, int y, bool goal);

/// Empty when the start/goal shape trayShape describes a puzzle that can be
/// solved, else what is wrong: a start or goal that is not the whole piece,
/// a goal without a start, or no piece with both a start and a goal.
[[nodiscard]] std::string startGoalError(const Puzzle& puzzle, unsigned int trayShape);

/// Keeps the colours of the pieces in step with the start stamps of the
/// problem's start/goal shape, so the assembler places a piece with a start
/// only there; and the puzzle's colours cover the stamps (the assembler
/// skips colour checks with none).
void refreshStartLocks(Puzzle& puzzle, const Problem& problem);

/// Renumbers the marks of the start/goal shapes before shape shapeId is
/// removed (its own go), or after shapes a and b swapped places.
void noteShapeRemoved(Puzzle& puzzle, unsigned int shapeId);
void noteShapeSwap(Puzzle& puzzle, unsigned int a, unsigned int b);

/// The older model: makes sure a sliding puzzle has a filled tray (the
/// result), a goal map shape and a problem that refers to both; with no
/// shapes yet, a tray of width by height. Safe to call repeatedly.
void ensureSetup(Puzzle& puzzle, unsigned int width = 6, unsigned int height = 6);

/// The colour (from 1) a piece is painted in; 0 when it is neutral.
[[nodiscard]] unsigned int pieceColor(const Puzzle& puzzle, unsigned int shapeId);

/// True when the piece has a coloured start on the problem's tray.
[[nodiscard]] bool hasStart(const Puzzle& puzzle, const Problem& problem, unsigned int shapeId);

/// True when the piece has a coloured goal on the problem's goal map.
[[nodiscard]] bool hasGoal(const Puzzle& puzzle, const Problem& problem, unsigned int shapeId);

/// The older model: puts a piece on the start tray as drawn, its hotspot at
/// (ax, ay, 0), with a colour of its own on piece and tray. False when it
/// would leave the floor or cover another start.
bool placeStart(Puzzle& puzzle, Problem& problem, unsigned int shapeId, int ax, int ay);

/// Takes a piece's start off (the piece is neutral again).
void clearStart(Puzzle& puzzle, Problem& problem, unsigned int shapeId);

/// The older model: puts the piece's goal at (ax, ay, 0) on the goal map;
/// the piece keeps its colour (a free piece stays neutral).
bool placeGoal(Puzzle& puzzle, Problem& problem, unsigned int shapeId, int ax, int ay);

/// Takes the piece's goal off the goal map.
void clearGoal(Puzzle& puzzle, Problem& problem, unsigned int shapeId);

/// Takes off the start that covers (x, y); true when there was one.
bool clearStartAt(Puzzle& puzzle, Problem& problem, int x, int y);

/// Places the piece's goal at (ax, ay), or takes it off when it is there.
bool toggleGoal(Puzzle& puzzle, Problem& problem, unsigned int shapeId, int ax, int ay);

/// Sets the problem's maximum of holes: floor cells less the pieces'
/// cells (0 when the pieces need more).
void syncMaxHoles(const Puzzle& puzzle, Problem& problem);

/// Empty when every start and goal stamp set fits inside its piece; else
/// a message naming each piece marked on more cells than it has.
[[nodiscard]] std::string stampOverflowMessage(const Puzzle& puzzle, const Problem& problem);

/// Makes every shape of a sliding puzzle one layer deep, pieces and
/// start/goal shapes alike, keeping the first layer (z = 0): pieces slide
/// flat on a flat tray. Returns the shapes that were changed.
std::vector<unsigned int> flattenPieces(Puzzle& puzzle);

/// The number of floor (non-empty) cells of a tray.
[[nodiscard]] unsigned int floorCells(const Voxel& tray);

/// What a sliding solve sets up first (the solve thread did it as a run
/// started): the problem's maximum of holes and the start locks. Changes
/// shapes, colours and the problem; true when anything changed.
bool prepareForSolving(Puzzle& puzzle, Problem& problem);

/// True when prepareForSolving would change anything (without changing it).
[[nodiscard]] bool needsPreparing(const Puzzle& puzzle, const Problem& problem);

}  // namespace bt::sliding
