---
title: Play Tab
description: "Play a sliding puzzle by hand on the Play tab: drag the pieces, count moves the way the solver does, undo and redo, play your moves back, export and import them, and save the board as a problem's Start or Goal."
---

The **Play** tab (`Ctrl+4`, `⌘4` on a Mac) lets you slide the pieces of a
[sliding puzzle](/docs/puzzle-types/sliding/) yourself instead of having the
Solver find the answer. The tab appears in the top bar, to the right of
Solver, while a sliding puzzle is open.

The problem list and the buttons are on the left; the board is in the
middle. Beside it are the number of moves made, **Reset**, **Undo**,
**Redo**, **Hide Goal Shadows**, **Hide Goal Lines**, **Hide Grid**,
**Hide Piece Labels** and **Simple Pieces**, and the playback bar is below.

## Problem

Click a problem to play it. Its pieces appear on the board where its
**Start** puts them. Its **Goal** tints the cells of the tray's floor where
each piece must end, and outlines them in the piece's colour; the outlines
stay visible when pieces stand on them.

**Hide Goal Shadows** takes the goal's tint off the floor, and **Hide Goal
Lines** takes its outlines off the board; hide both to play without seeing
where the pieces should go. Each button then reads **Show Goal Shadows** or
**Show Goal Lines**, which brings them back. **Hide Grid** does the same for
the lines between the cells on the tray's floor (**Show Grid** brings them
back), and **Hide Piece Labels** for the names written on the pieces
(**Show Piece Labels**). **Simple Pieces** draws the pieces flat and square in plain colours,
in a flat tray with square corners; **3D Pieces** brings back the rounded
pieces with their sides showing. Each choice holds for every problem you
play until you change it.

A problem can be played once every piece has a start. If a piece has none,
the tab says which one and offers **Go to Puzzle**, where the Start and
Goal are placed.

A problem without a Goal can still be played: the moves are counted, and
nothing is ever solved.

## Moving the pieces

Drag a piece with the mouse. It follows the pointer through the free
cells and stops against the walls and the other pieces; it never jumps
over anything. It moves with the pointer from the first pixel, so it can
sit between cells while you drag; let go and it settles into the nearest
cell it can reach, which makes the move.

Moves are counted the way the Solver counts them:

- One piece going anywhere it can reach is **one move**, even round a
  corner.
- Dragging the **same piece again** straight away goes on with that move:
  the count doesn't change.
- Putting the piece back where the move began takes the move off.

So your total can be compared with the fewest moves the
[Solver](/docs/solver-tab/) finds.

When the problem has a Goal and the pieces reach it, the board shows
**Solved!** with the number of moves.

### Nested slides

If the problem's Solver options allow **nested slides**, a piece carries
what is nested in it when it cannot move alone. Hold `Shift` while you
drag to carry the nested pieces even when the piece could move alone.

### Boards with more than one layer

A board with several layers is shown one layer at a time; pick the layer
at the top right of the board. Pieces stay in their layer when played by
hand, even if the problem allows vertical moves.

## Undo, Redo and playback

**Reset** puts the pieces back at the Start, as ⏮ does: the moves are
kept, so **Redo** makes them again, until you make a new move.
**Undo** takes the last move back and **Redo** makes it again
(`Ctrl+Z` and `Shift+Ctrl+Z`). On this tab these keys act on your moves,
not on edits to the puzzle. Making a new move after an Undo drops the
moves that were undone.

The bar under the board plays your moves back:

| Button | Does |
|---|---|
| ⏮ / ⏭ | Back to the Start / to the last move (`Shift+←` / `Shift+→`) |
| ◀ / ▶ | One move back / forward (`←` / `→`) |
| Play | Plays the moves from where the board is (`Space`; `Esc` pauses) |
| Slider | Goes to any move |

**Restart** throws the moves away and puts the pieces back at the Start.

## Save position as Start or Goal

**Save position as Start** makes the position on the board the problem's
Start: every piece starts where it is now. The game then begins again from
that position.

**Save position as Goal** makes it the problem's Goal: every piece must
end where it is now. To leave some pieces free, take their goals off on
the [Puzzle tab](/docs/puzzle-tab/).

Either one replaces what the problem had, and removes the solutions the
Solver found for it; ShapeLogic asks first when there is something to
lose. It is one undo step: use **Edit ▸ Undo** on another tab to take it
back. Pause a running solve before saving a position.

## Export and import moves

**Export moves…** writes your moves to a `.slmoves` file. Someone else
with the same puzzle can open it with **Import moves…** to watch or
continue your game.

A moves file is taken only by a problem with the same board and the same
Start. If one of its moves can't be made, ShapeLogic says which move and
why, and nothing changes. After an import the board is at the Start:
press **Play** to watch the moves.

Your moves are not saved in the puzzle file. Export them to keep them.
