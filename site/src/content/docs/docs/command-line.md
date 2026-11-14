---
title: Command line tool
description: "shapelogic_cli: solve puzzle files from a terminal or a script, every option explained with examples."
---

`shapelogic_cli` solves ShapeLogic and BurrTools puzzle files without the
app's window. It runs the same solvers as the app, so it gives the same
answers. Use it to:

- solve puzzles in scripts and batch jobs, or on a server with no display;
- get a machine-readable result (`--json`) for your own tools;
- solve a file and save the solutions back into it (`--save`), as the app's
  **Solve** does;
- run long sliding and Panex searches that can be stopped and carried on;
- print text drawings of assemblies and disassembly steps.

It is the successor of BurrTools' `burrTxt` and `burrTxt2`. The options and
the printed text follow `burrTxt`, and `burrTxt2`'s options are available
under [`--save`](#--save-solve-and-write-the-solved-file).

**Contents**

- [Getting the tool](#getting-the-tool)
- [Quick start](#quick-start)
- [Usage](#usage)
- [Options at a glance](#options-at-a-glance)
- [Solving](#solving)
  - [What a solve does](#what-a-solve-does) · [Output](#output) · [The level text](#the-level-text)
  - [`-d`](#-d-solve-completely) · [`-q`](#-q-quiet) · [`-s`](#-s-print-the-assemblies) · [`-p`](#-p-print-the-moves) · [`-n`](#-n-no-newline) · [`-o`](#-o-choose-the-problem) · [`-r`](#-r-reduce-the-placements) · [`-t`](#-t-threads) · [`--progress`](#--progress)
- [Brick puzzles](#brick-puzzles-1): [`--solver`](#--solver-complete-or-fast) · [`-R`](#-r-rotations) · [`-C`](#-c-strict-colours)
- [Sliding puzzles](#sliding-puzzles-1): [`--solver`](#--solver-one-solver) · [`--nested-slides`, `--partial-nested`](#--nested-slides-and---partial-nested) · [`--vertical-moves`](#--vertical-moves) · [`--optimized-paths`](#--optimized-paths) · [`--any-orientation`, `--mirrored-orientations`](#--any-orientation-and---mirrored-orientations) · [`--high-memory`](#--high-memory)
- [Stacking puzzles](#stacking-puzzles-1): [`--solver`](#--solver-stacking-or-panex) · [Long Panex searches](#long-panex-searches-stop-and-carry-on)
- [`--json`: machine-readable output](#--json-machine-readable-output)
- [`--save`: solve and write the solved file](#--save-solve-and-write-the-solved-file)
- [`-x`: take the saved solutions apart again](#-x-take-the-saved-solutions-apart-again)
- [`-a`: read information from the file](#-a-read-information-from-the-file)
- [Reading the drawings](#reading-the-drawings)
- [Exit codes](#exit-codes)
- [Environment variables](#environment-variables)
- [Benchmark and comparison switches](#benchmark-and-comparison-switches)
- [Recipes](#recipes)
- [Coming from BurrTools](#coming-from-burrtools)

## Getting the tool

`shapelogic_cli` ships inside the app, next to the solver engine. You don't
download it separately.

**macOS.** Choose **ShapeLogic ▸ Install Command Line Tool…**, then
**Install**. This makes `/usr/local/bin/shapelogic_cli` a link to the copy
inside the app. macOS asks for your password because that folder belongs to
the system. Open a new terminal window and run:

```sh
shapelogic_cli --version
```

The link follows the app when it is updated in place. The same dialog has a
**Remove** button to delete the link again. Without installing, you can run it
by its full path:

```sh
/Applications/ShapeLogic.app/Contents/Resources/shapelogic_cli --version
```

**Windows and Linux.** Choose **Help ▸ Command Line Tool…**. The dialog
shows the full path of the tool on your computer. Installing it into the
command path from the app is not available on these systems yet, so run it by
that path, or add its folder to your `PATH` yourself. On Windows the file is
`shapelogic_cli.exe`.

## Quick start

```sh
# Find the assemblies and check which ones can be taken apart
shapelogic_cli -d CubeInCage.xmpuzzle

# The same, printing only the summary line
shapelogic_cli -dq CubeInCage.xmpuzzle

# One line of JSON for scripts
shapelogic_cli --json CubeInCage.xmpuzzle

# Solve and write the solutions into a new file, as the app's Solve does
shapelogic_cli -d --save CubeInCage-solved.xmpuzzle CubeInCage.xmpuzzle
```

With the example puzzle from [Getting started](/docs/getting-started/), the
second command prints:

```text
using assembler 1
96 assemblies and 1 solutions found with 2475 iterations in 0.005 seconds
```

## Usage

```text
shapelogic_cli [options] file [options]
```

- **One file per run.** Options may come before or after it. Giving two file
  names is an error.
- **Short options combine.** `-dq` is the same as `-d -q`, and `-dRq` the same
  as `-d -R -q`. The two short options that take a value, `-o` and `-a`, must
  be given on their own: `-o 2`, not `-do 2`.
- **Option values** are the next argument: `-t 4`, `--solver panex`,
  `--save out.xmpuzzle`.
- **The puzzle type comes from the file.** A file is a brick, sliding or
  stacking puzzle (see [Puzzle types](/docs/puzzle-types/)). It decides which
  solvers and options apply. *Brick* here covers every grid that is assembled
  and taken apart: cubes, triangular prisms, spheres, rhombic,
  tetrahedra-octahedra, hexagonal prisms and cairo prisms. Options that don't apply to the file's type are refused
  with a message, not ignored.

The tool has four modes. Which one runs depends on the options:

| Mode | Chosen by | What it does |
|---|---|---|
| [Solve](#solving) | the default | Assembles the puzzle, optionally takes each assembly apart (`-d`), and prints the results. The file is not changed. |
| [Solve and save](#--save-solve-and-write-the-solved-file) | `--save FILE` | Solves as the app does, keeps the best solutions, and writes the solved puzzle to `FILE`. |
| [Retake](#-x-take-the-saved-solutions-apart-again) | `-x` | Takes apart again the solutions already saved in the file. |
| [Ask](#-a-read-information-from-the-file) | `-a s0`, `-a s1`, `-a c` | Prints the saved solutions or the comment, without solving. |

## Options at a glance

### For every puzzle type

| Option | Meaning |
|---|---|
| `-d` | Solve completely: take apart (brick), find the slide path (sliding) or the rod transfers (stacking). Only what solves is printed. |
| `-q` | Quiet: print only the statistics. |
| `-p` | Print the moves of each solution as drawings (brick, sliding) or a list of transfers (stacking). |
| `-s` | Print each assembly as a drawing. |
| `-n` | No newline after the summary line. |
| `-o n` | Solve problem `n` of the file, counting from 0. The default is 0. |
| `-o all` | Solve every problem in the file. |
| `-x` | Take the file's saved solutions apart again, instead of solving. |
| `-a s0` | Print the saved solutions: the pieces each one uses. |
| `-a s1` | Print the saved solutions with a drawing of each assembly. |
| `-a c` | Print the file's comment. |
| `--solver TYPE` | Choose the solver. The choices depend on the puzzle type. |
| `-t n` | Use `n` worker threads. `0`, the default, uses every core. |
| `--json` | Print one JSON object with the result. Implies `-d -q -r`. |
| `--progress` | Print a progress line to stderr once a second. |
| `--save FILE` | Solve as the app does and write the solved puzzle to `FILE`. |
| `-h`, `--help [TYPE]` | Show the help; with `brick`, `sliding` or `stacking`, also that type's options. |
| `--version` | Print the version. |

### Brick puzzles

| Option | Meaning |
|---|---|
| `-r` | Reduce the placements before solving. |
| `-R` | Also try 90° piece rotations when taking apart. Implies `-d`. |
| `-C` | Strict colour rules: a coloured cell fits only a result cell of the same colour. |
| `--solver` | `"Complete (ShapeLogic)"` (default, also `classic`) or `"Fast (Crowell)"` (also `crowell`). |

### Sliding puzzles

| Option | Meaning |
|---|---|
| `-r` | Reduce the placements before finding the start layouts. |
| `--nested-slides` | A piece carries the pieces nested inside its outline. |
| `--partial-nested` | Also carry pieces only partly inside. Implies `--nested-slides`. |
| `--vertical-moves` | On a tray more than one layer deep, pieces may also move up and down. |
| `--optimized-paths` | On a board of blocks that fits a grid, pieces stop only on that grid: a much smaller search that may miss the fewest moves. |
| `--any-orientation` | A piece with no start and no goal may be placed in any quarter turn. |
| `--mirrored-orientations` | Those pieces may also be placed mirrored. Implies `--any-orientation`. |
| `--high-memory` | Keep up to half the computer's memory of arrangements in memory instead of about 2 GB. |
| `--solver` | `"Sliding Puzzle Solver"` (`sliding`), the only one, so it never needs to be given. |
| `--restart` | Throw away the saved searches of this problem's starts and start over. |

### Stacking puzzles

| Option | Meaning |
|---|---|
| `--high-memory` | Allow up to half the computer's memory instead of about 2 GB. |
| `--solver` | `"Stacking Solver"` (`stacking`, the default) or `"Panex Solver"` (`panex`). |
| `--restart` | Throw away a saved Panex search and start over. |
| `--panex-dir DIR` | Keep saved Panex searches in `DIR`. |
| `--panex-disk GB` | The disk space saved Panex levels may take. |

### With `--save`

| Option | Meaning |
|---|---|
| `--restart` | Drop what the file found before and start over. |
| `--limit n` | Keep the `n` best solutions. The default is 10; `0` keeps all. |
| `--count-only` | Count the solutions but keep none. |
| `--keep-mirrors` | Keep solutions that are mirror images of others. |
| `--keep-rotations` | Keep solutions that are rotations of others. |
| `--drop-disassemblies` | Keep only each solution's level, not its moves. |

The [benchmark switches](#benchmark-and-comparison-switches) at the end of
this page change how the search runs, never its answers.

## Solving

Without `-x`, `-a` or `--save`, the tool solves the puzzle and prints what it
found. Nothing is written to disk.

### What a solve does

1. **Assemble.** The assembler finds every way the pieces fill the result
   shape. Each way is an *assembly*.
2. **Solve each assembly**, only with `-d`:
   - **Brick:** the disassembler checks whether the assembly can be taken
     apart, and finds the moves. An assembly that comes apart is a
     *solution*.
   - **Sliding:** the assemblies are the *start layouts*, the ways the
     pieces fit their marked starts. Each one is searched to the goal. A start
     that reaches the goal is a solution, counted only if it reaches a goal
     picture not already reached in as few moves.
   - **Stacking:** there is no assembler. The file's start stacks are the
     one "assembly", searched to the goal stacks.

Without `-d` only the first step runs, and the solution count is 0. A stacking
puzzle without `-d` says so: `(use -d to search for the moves)`.

### Output

A solve without `-q` first prints ` The puzzle:` and a drawing of every shape
and problem (see [reading the drawings](#reading-the-drawings)). Then, for
each solution, `-s` draws the assembly, a `level:` line gives its moves, and
`-p` draws every step. At the end comes the summary line:

```text
96 assemblies and 1 solutions found with 2475 iterations in 0.005 seconds
```

- **assemblies:** how many ways the pieces fill the result.
- **solutions:** how many of them could be taken apart (or reached the
  goal). Without `-d` this is 0.
- **iterations:** how many steps the assembler's search took. It depends on
  the solver and the options, so use it to compare runs of the same puzzle, not
  as a property of the puzzle.
- **seconds:** the time from the start of the assembly to the end.

Stacking puzzles print `1 assemblies and 1 solutions found in 0.001 seconds`,
with no iterations, then a line about the search (see
[Stacking puzzles](#stacking-puzzles-1)).

Some lines go to **stderr**, not stdout, so that stdout stays clean:
`using assembler 0` or `using assembler 1` (which assembler the solver chose
for the problem), `removed … rows and … columns` from `-r`, and the
`--progress` lines.

### The level text

Each solution's `level:` line describes its disassembly as numbers separated
by dots, one per separation:

```text
level: 4.9.3
```

The first number is the number of moves needed before the first piece or
group of pieces comes free. This is the puzzle's **level**: a *level 4*
puzzle needs 4 moves to remove the first part. Each later number is the moves
for the next separation. Here 4, then 9, then 3: 16 moves in all.

With `-R`, a number may be followed by `R` and the number of 90° turns in that
separation: `4R0.9R0.6R1` is 4 slides, then 9, then 6 slides and 1 turn.

For sliding and stacking puzzles the level text is a single number, the total
number of moves: `level: 81`.

With `-q` the `level:` lines are left out, unless `-o all` is given.

### `-d`: solve completely

```sh
shapelogic_cli -d PelikanBurr.xmpuzzle
```

For brick, Triangular Prism, Rhombic, Tetrahedra-Octahedra, Hexagonal Prism
and Cairo Prism puzzles, `-d` turns on the disassembler, so the tool reports
which assemblies come apart and how. Only the assemblies that solve are
printed. (`burrTxt` takes only cubes and triangular prisms apart.)

Spheres puzzles have no disassembler. With `-d` they still assemble, and
report 0 solutions. To list their assemblies, leave out `-d` and use `-s`.

### `-q`: quiet

```sh
shapelogic_cli -dq SolidSixPieceBurrs.xmpuzzle
```

Leaves out the puzzle drawing and the `level:` lines, and prints only the
summary. `-q` also turns off `-p` and `-s` given *before* it. A `-p` or `-s`
given after `-q` is kept: `-qp` prints the move drawings but no level lines.

### `-s`: print the assemblies

```sh
shapelogic_cli -dqs CubeInCage.xmpuzzle
```

Draws each solution's assembly inside the result shape, one letter per piece.
Without `-d`, it draws every assembly found.

### `-p`: print the moves

```sh
shapelogic_cli -dp CubeInCage.xmpuzzle
```

For brick and sliding puzzles, `-p` draws every state of each solution's
disassembly, inside a box larger than the result so the pieces have room to
move out. The output is wide and long: send it to a file and view it with line
wrapping off.

```sh
shapelogic_cli -dp CubeInCage.xmpuzzle > cubeincage-moves.txt
```

For stacking puzzles `-p` lists the transfers instead:

```text
level: 7
  1: S3 from rod 1 to rod 3
  2: S2 from rod 1 to rod 2
  3: S3 from rod 3 to rod 2
  4: S1 from rod 1 to rod 3
  5: S3 from rod 2 to rod 1
  6: S2 from rod 2 to rod 3
  7: S3 from rod 1 to rod 3
```

A piece used more than once is named with its copy, as in `S2 copy 1`. A
Panex pocket column is called `pocket`.

### `-n`: no newline

Leaves out the newline at the end of the summary line, so a script can add
its own text to the same line.

```sh
for f in *.xmpuzzle; do
  shapelogic_cli -dqn "$f" 2>/dev/null; echo "  $f"
done
```

### `-o`: choose the problem

A file can hold several problems. They are numbered from 0, in the order the
app's **Puzzle** workspace lists them.

```sh
shapelogic_cli -d -o 2 MyPuzzles.xmpuzzle   # the third problem
shapelogic_cli -dq -o all MyPuzzles.xmpuzzle
```

With `-o all`, each problem's results start with a `problem: <name>` line,
and its `level:` lines are printed even with `-q`. A number past the last
problem is an error that says how many the file has.

### `-r`: reduce the placements

```sh
shapelogic_cli -dqr PelikanBurr.xmpuzzle
```

Before searching, removes placements that can never be part of a solution.
This takes a little time up front and usually makes the search itself much
faster. On stderr it reports what it removed:

```text
removed 3011 rows and 291 columns
```

The answers are the same with or without `-r`. Only the iterations and the
time change. `--json` and `--save` always reduce.

### `-t`: threads

```sh
shapelogic_cli -dq -t 4 SolidSixPieceBurrs.xmpuzzle
```

Sets how many worker threads the assembler and, for sliding and Panex
searches, the search may use. `0` (the default) uses every core. Use a
lower number to leave room for other work, or `-t 1` to compare against a
single-threaded run. The answers do not depend on the number of threads.

### `--progress`

```sh
shapelogic_cli -d --progress SolidSixPieceBurrs.xmpuzzle
```

Prints a line to stderr once a second while a solve runs: the time so far,
how much of the assembly is done, how many assemblies were found, and, while
an assembly is being taken apart, how far that has got. For example:

```text
   12.0 s  assembly  41.3%, 18342 found  disassemble 18342: level 3: 120/544, 90211 positions
```

Short solves end before the first line. With `--save`, the line shows the same
progress the app's **Solver** workspace shows, with an estimate of the time
left once it has one.

## Brick puzzles

These options apply to brick puzzles: cubes, triangular prisms and the other
assembly grids. Run `shapelogic_cli -h brick` to list them.

### `--solver`: Complete or Fast

| Solver | Names accepted | What it does |
|---|---|---|
| **Complete (ShapeLogic)** | `"Complete (ShapeLogic)"`, `complete`, `classic`, `"BurrTools Classic"` | The default. A complete take-apart: if an assembly can be taken apart by moving pieces along the grid's axes, it finds how. |
| **Fast (Crowell)** | `"Fast (Crowell)"`, `fast`, `crowell`, `"Andrew Crowell"` | Andrew Crowell's take-apart, with heuristics for 90° moves. Often faster on large puzzles. |

Names ignore case, spaces, hyphens, underscores and brackets, so
`--solver crowell` and `--solver "Fast (Crowell)"` are the same. Quote a name
that has spaces.

```sh
shapelogic_cli --json --solver crowell PelikanBurr.xmpuzzle
```

### `-R`: rotations

```sh
shapelogic_cli -dR CubeInCage.xmpuzzle
```

Lets the disassembler also turn pieces by 90° on their way out, as well as
slide them. Some assemblies that cannot be taken apart by sliding alone come
apart this way, so `-R` can find more solutions, at the cost of time. The
example puzzle has 1 solution without `-R` and 2 with it. `-R` implies `-d`.
The level text then shows the turns (see [the level text](#the-level-text)).

### `-C`: strict colours

```sh
shapelogic_cli -dC puzzle.xmpuzzle
```

Colour rules in a puzzle limit which piece cells may sit on which result
cells. By default a coloured piece cell also fits a neutral (uncoloured)
result cell, and a neutral piece cell fits any result cell. With `-C`, a cell
fits only a result cell of exactly the same colour: coloured on the same
colour, neutral on neutral.

## Sliding puzzles

A sliding puzzle's solve has two parts. The assembler finds the *start
layouts*, every way the pieces fit their marked starts on the tray. With `-d`,
each layout is then searched move by move, breadth first, until the goal is
reached. So the first solution found is the one with the fewest moves. Run
`shapelogic_cli -h sliding` to list the options.

```sh
shapelogic_cli -dq slide_klotski.xmpuzzle
shapelogic_cli --json slide_klotski.xmpuzzle
```

```text
{"assemblies":1,"solutions":1,"dotlevel":"81","level":81,"totalmoves":81,"solvetime":0.010}
```

### `--solver`: one solver

Sliding puzzles have one solver, the **Sliding Puzzle Solver** (`--solver
sliding` or `--solver "Sliding Puzzle Solver"`, which is also what runs
without `--solver`). It has no limit on the arrangements it looks at: it
finds the fewest moves, or proves there is no solution. It uses every core
(`-t n` to limit it). Its memory use stays level: once the arrangements
outgrow the search memory (see [`--high-memory`](#--high-memory)), they go to
disk, so free disk space is what limits it:

- It writes to the folder in `SHAPELOGIC_SLIDE_DIR`, or else to `sliding` in
  the user's cache folder (see [environment variables](#environment-variables)).
  A small search never touches the disk. The files go when the search ends.
- `SHAPELOGIC_SLIDE_DISK_GB` limits the disk it may take. By default it may
  use half the free space, at most 200 GB. A larger value is held to the free
  space, less 2 GB or a twentieth of it, so the disk never fills up. On a Mac
  the free space includes what macOS frees when it is needed, as Finder
  counts it.
- As the disk fills, it keeps fewer of the older moves (without nested
  slides), and works out the missing ones again when it traces the path back.
- When the goal is a single arrangement (every piece has a goal position),
  it also searches back from the goal until the two ends meet: with
  `--nested-slides` from the first move, otherwise once the search grows
  wide. With nested slides this needs whole nesting (not `--partial-nested`)
  and no piece that could hold more than six others; otherwise the search
  goes from the start alone.

However much disk it may take, it stops while at least 100 MB, plus the
room to save its search, is still free on the disk.

If a start's search fills its disk space, the search is saved and the run
stops there. The tool says so after the summary:

```text
1 start layout(s) not searched completely: The search filled the disk space it may use (40.0 GB in /Users/me/Library/Caches/ShapeLogic/sliding), so solutions may be missing
the search stopped there and is saved: free some disk space or raise SHAPELOGIC_SLIDE_DISK_GB, then run the same command again to carry it on, or add --restart to start over
```

Running the same command again carries the saved search on; `--restart`
throws it away. Saved searches nobody carries on for 30 days are deleted.

When every reachable arrangement was searched and none reaches the goal, the
tool says so:

```text
every reachable arrangement was searched: no solution (without --nested-slides)
```

The hint in brackets means that turning on nested slides might still find one.

### `--nested-slides` and `--partial-nested`

```sh
shapelogic_cli -d --nested-slides pocket.xmpuzzle
shapelogic_cli -d --partial-nested pocket.xmpuzzle
```

By default every piece slides on its own. With `--nested-slides`, a piece
also carries the pieces nested inside its outline, such as a piece sitting in
another piece's pocket. Pieces that only touch never move together.

`--partial-nested` goes further: a piece also carries pieces that are only
partly inside its outline, some of their cells inside it and the rest
sticking out. It implies `--nested-slides`.

These change which moves are allowed, so they can change the answer. Use the
same setting as the puzzle's design.

### `--vertical-moves`

```sh
shapelogic_cli -d --vertical-moves two-layer-tray.xmpuzzle
```

On a tray more than one layer deep, a piece may also move up and down, lifted
over the other pieces and put down somewhere else. Without it every piece
slides only in its own layer. One layer deep, it changes nothing.

### `--optimized-paths`

```sh
shapelogic_cli -d --nested-slides --optimized-paths board-of-blocks.xmpuzzle
```

For a board that divides into a grid of cells that its blocks fill, such as
an 18 by 18 board of 6 by 6 blocks. Every piece then stops only on that grid:
blocks on its lines, smaller pieces where they fit in a block's hole or where
they start or end. They still pass through the cells in between. The search
is much smaller, so a board that takes hours can take seconds. But it limits
the search space: the path it finds may have more moves than the fewest, and
when it finds none, a full search may still find one. Before solving it prints
the grid (`optimized paths: the pieces stop only on a 6-cell grid`), or that
the board fits none, in which case every stop is searched. The Solver tab's
**Automatically use optimized paths** is the same option.

### `--any-orientation` and `--mirrored-orientations`

```sh
shapelogic_cli -d --any-orientation spare-pieces.xmpuzzle
```

A piece keeps the orientation it is drawn in: it is placed on the board that
way and never turns as it slides. With `--any-orientation`, a piece that has
neither a start nor a goal on the board may be placed in any of its quarter
turns in its layers, each one a different start. `--mirrored-orientations`
also lets those pieces be placed mirrored, and implies `--any-orientation`. A
piece with a start or a goal is always placed as drawn.

### `--high-memory`

```sh
shapelogic_cli -d --high-memory big-slider.xmpuzzle
```

The search normally keeps about 2 GB of arrangements in memory and writes the
rest to disk. `--high-memory` lets it keep up to half of the computer's
memory, which is faster. If it has nowhere to write (no cache folder and no
`SHAPELOGIC_SLIDE_DIR`), memory is its limit, and when it runs out the
message ends with `(try --high-memory)`.

## Stacking puzzles

Stacking puzzles have no assembler: the start and goal stacks come from the
file. A move is one disc going from the top of one rod to the top of another.
With Panex columns, a disc of size *n* sits at most *n* places below the top of
its rod. `-d` searches for the fewest moves. Run
`shapelogic_cli -h stacking` to list the options.

```sh
shapelogic_cli -dp stack_hanoi_3.xmpuzzle
```

```text
level: 7
  1: S3 from rod 1 to rod 3
  …
1 assemblies and 1 solutions found in 0.001 seconds
stack search: 7 moves deep, 8 stackings, peak 0 MB in memory
```

The last line reports the search: how many moves deep it went, how many
stackings it found, the most memory it held and, for a Panex search, the disk
it used. It starts with `stack search:`, or `panex search:` when the Panex
Solver was asked for. When no path exists the tool says
`every reachable stacking was searched: no solution`. A problem whose stacks
break the rod set's rules prints the reason, for example
`The start and goal use different discs: the goal is missing S2.`

### `--solver`: Stacking or Panex

| Solver | Names accepted | What it does |
|---|---|---|
| **Stacking Solver** | `stacking`, `"Stacking Solver"` | The default. Fewest rod transfers, for any rod set. |
| **Panex Solver** | `panex`, `"Panex Solver"` | Fewest rod transfers for Panex columns, built for large towers. |

The **Panex Solver** searches from both ends at once, holds only the newest
levels in memory, and uses every core (`-t n` to limit it). It refuses puzzles
it can't handle, with the reason.

When the default Stacking Solver is chosen and the puzzle fits the Panex
search, the tool uses the Panex search for it anyway, as the app does. So
saved searches and Ctrl-C (below) work for those puzzles too.

### Long Panex searches: stop and carry on

A large Panex puzzle can take hours. The search **saves itself**:

- when you press **Ctrl-C** (press it a second time to quit at once, without
  saving);
- when it runs out of memory;
- as it runs: about a minute in, and after that as often as saving costs no
  more than about 2% of the search's time (never less than a minute apart).

To carry on, run **the same command** again. The tool prints
`continuing <the saved search>` and picks up where it stopped. When a search
stops before the end, the tool says:

```text
stopped; it is saved: run the same command again to carry on, or add --restart to start over
```

Each saved search has its own folder, named from a fingerprint of the puzzle
and problem, so several can be saved at once.

| Option | Meaning |
|---|---|
| `--restart` | Throw away the saved search for this problem and start over. Does not need `--save`. |
| `--panex-dir DIR` | Keep saved searches in `DIR`. The default is `SHAPELOGIC_PANEX_DIR`, or `panex` in the user's cache folder. |
| `--panex-disk GB` | The disk space saved levels may take, in GB. The default is `SHAPELOGIC_PANEX_DISK_GB`, or half the free space, at most 200 GB. Less disk makes tracing the path back at the end slower. |
| `--high-memory` | Hold up to half the computer's memory in stackings, instead of stopping at about 2 GB. |

```sh
# Start, or carry on, a big Panex search on a fast disk
shapelogic_cli -d --solver panex --panex-dir /Volumes/Fast/panex --panex-disk 100 panex8.xmpuzzle

# Forget the saved search and start again
shapelogic_cli -d --solver panex --restart panex8.xmpuzzle
```

## `--json`: machine-readable output

```sh
shapelogic_cli --json PelikanBurr.xmpuzzle
```

```json
{"assemblies":12,"solutions":1,"dotlevel":"99.3.4.2","level":99,"totalmoves":108,"solvetime":0.050}
```

`--json` solves completely and prints exactly one line of JSON on stdout,
with nothing else there. It implies `-d`, `-q` and `-r`. It reports one
problem: the one `-o n` names, 0 by default.

| Field | Type | Meaning |
|---|---|---|
| `assemblies` | number | Assemblies found (for stacking puzzles, 1: the start). |
| `solutions` | number | Solutions found. |
| `dotlevel` | string or `null` | The [level text](#the-level-text) of the best solution. |
| `level` | number or `null` | The best solution's level: the moves before the first part comes free. |
| `totalmoves` | number or `null` | All the moves of the best solution, slides and turns. |
| `moves` | number or `null` | Only with `-R`: the best solution's slides. |
| `rotations` | number or `null` | Only with `-R`: the best solution's turns. |
| `solvetime` | number | Seconds the solve took. |

- **The best solution** is the one with the highest level, then the most total
  moves. Among equals, the first found is kept. For sliding and stacking
  puzzles it is the solution with the fewest moves, and all three numbers are
  that count.
- When there is no solution, `dotlevel`, `level` and `totalmoves` (and `moves`
  and `rotations` with `-R`) are `null`.
- When the puzzle cannot be set up at all, for example the pieces have too
  many cells for the result, the reason goes to stderr, nothing goes to stdout,
  and the exit code is 1.

You can add brick options such as `-R`, `-C`, `-t` and `--solver`, and the
sliding and stacking search options. `--json` cannot be combined with `-a`,
`-x`, `-o all`, `-p` or `-s`.

```sh
# The levels of every puzzle in a folder, as CSV
echo "file,assemblies,solutions,level,totalmoves"
for f in puzzles/*.xmpuzzle; do
  shapelogic_cli --json "$f" |
    jq -r --arg f "$f" '[$f, .assemblies, .solutions, .level, .totalmoves] | @csv'
done
```

## `--save`: solve and write the solved file

```sh
shapelogic_cli -d --save CubeInCage-solved.xmpuzzle CubeInCage.xmpuzzle
```

`--save FILE` solves the way the app's **Solve** button does, then writes the
whole puzzle, with its solutions, to `FILE`. Open that file in the app to
browse the solutions and play the disassemblies. This is BurrTools'
`burrTxt2`.

How it differs from a plain solve:

- **The placements are always reduced** (as `-r`).
- **Solutions are sorted by moves** and only the best are kept: 10 by
  default (see `--limit`).
- **It carries on a paused solve.** If the file holds a solve that the app
  paused, `--save` continues it instead of starting over.
- **A solved problem is skipped**, with a message, unless `--restart` is
  given:

  ```text
  problem 0 (g) is already solved: 96 assemblies, 1 solutions
  use --restart to solve it again
  ```

- **It prints little.** `outputting into FILE` at the start, and for each
  problem a line such as
  `problem 0: 96 assemblies and 1 solutions found in 0.002 seconds`. `-q`
  leaves both out.

`FILE` may be the input file itself, to solve it in place. Write to a new
file if you want to keep the original.

`--save` works with `-d`, `-R`, `-C`, `-t`, `-o` (including `-o all`),
`--solver`, `--progress`, `-q` and the sliding and stacking search options,
plus these:

| Option | Meaning |
|---|---|
| `--restart` | Drop what the file found before (solutions and any paused state) and start over. |
| `--limit n` | Keep the `n` best solutions, by moves. The default is 10. `--limit 0` keeps them all. |
| `--count-only` | Count the assemblies and solutions, but keep none in the file. |
| `--keep-mirrors` | Keep solutions that are mirror images of solutions already found. By default they are dropped as duplicates. |
| `--keep-rotations` | Keep solutions that are rotations of solutions already found. By default they are dropped. |
| `--drop-disassemblies` | Keep only each solution's level, not its moves, so the file stays smaller. |

`--save` cannot be combined with `--json`, `-a`, `-x`, `-p` or `-s`. The
options in the table above are only accepted with `--save`, except
`--restart` on stacking puzzles.

```sh
# Solve every problem and keep all the solutions
shapelogic_cli -d -o all --restart --limit 0 --save all-solutions.xmpuzzle puzzle.xmpuzzle

# Count only, on 8 threads, showing progress
shapelogic_cli -d --count-only -t 8 --progress --save counted.xmpuzzle puzzle.xmpuzzle
```

On the Spheres grid, which has no disassembler, `--save` with `-d` is refused:

```text
Sorry, this space grid doesn't have a disassembler (yet)
drop -d to just assemble the puzzle
```

Leave out `-d` to save just the assemblies.

For stacking puzzles, `--save` with `-d` searches the transfers as the app's
Solve does. A saved Panex search carries on unless you give `--restart`, and
Ctrl-C pauses it and saves the file.

## `-x`: take the saved solutions apart again

```sh
shapelogic_cli -x CubeInCage-solved.xmpuzzle
```

Instead of solving, `-x` takes each solution already saved in the file and
solves it again: brick assemblies are taken apart, sliding starts are searched
to the goal, and a stacking problem with a saved solution is searched again.
For each one that solves it prints a `level:` line with its number of moves.

It's useful for checking a file's saved solutions against the current solver,
or for drawing their moves with `-p` or `-s`:

```sh
shapelogic_cli -x -p CubeInCage-solved.xmpuzzle > moves.txt
```

`-x` takes `-o`, `-p`, `-s`, `-q`, `-t`, `-R` and `--solver`. On grids without
a disassembler it prints nothing.

## `-a`: read information from the file

`-a` prints information already in the file, without solving.

```sh
shapelogic_cli -a c CubeInCage.xmpuzzle
```

| Option | Prints |
|---|---|
| `-a c` | The puzzle's comment. |
| `-a s0` | Every problem's saved solutions: for each, the shapes it uses. |
| `-a s1` | The same, with a drawing of each assembly in the result shape. |

`-a s0` lists every problem in the file, not only the one `-o` names:

```text
problem 0
001: S2 S3 S3 S4 S5 S6
```

A shape that appears twice was used twice. Stacking problems have no result
shape, so `-a s1` draws nothing for them.

## Reading the drawings

The drawings (from a solve without `-q`, and from `-s`, `-p` and `-a s1`) are
text pictures of 3D shapes.

```text
shape 3:
 +---+ +---+ bx 0-2 by 0-2 bz 0-1 h: 0 0 0
 ! aa! !   !
 !  a! !   !
 !aaa! !  a!
 +---+ +---+
```

- Each framed box is one **layer**, in Z order from left to right. Inside a
  box, X runs to the right and Y runs down.
- **In shape drawings** a letter is the cell's value: `a` is a fixed cell, `b` a
  variable cell, and later letters are cells with a colour. A space is empty.
- **In assembly and move drawings** each letter is one piece, `a` for the first
  piece placed, `b` for the second, and so on.
- `bx`, `by` and `bz` are the shape's bounds: the first and last filled cell
  along each axis. `h:` is its hotspot, the reference point of its placements.

After the shapes, each problem is listed with its result shape and its piece
shapes, by the numbers in the `shape n:` headings (counted from 0, so
`shape 0` is S1 in the app).

The move drawings from `-p` show every state of the disassembly, one row of
layers per state. The box is larger than the result, so the pieces have room
to move out.

## Exit codes

| Code | Meaning |
|---|---|
| `0` | Success. This includes a solve with 0 solutions, and, outside `--json`, a puzzle that cannot be set up (the reason is printed on stdout). |
| `1` | No file was given. With `--json`, the puzzle cannot be set up or solved. A search stopped before its end, or the solver failed with `--save`. |
| `2` | A usage error: an unknown or misplaced option, a missing value, options that don't go together or don't apply to the file's puzzle type, a problem number that doesn't exist, or a file that can't be read or written. |
| `130` | A second Ctrl-C during a Panex search quit at once. |

Messages about usage errors go to stderr and start with `shapelogic_cli:`.

A script should check the exit code and, in text mode, the solution count:
`0 solutions` is a successful answer, not an error.

## Environment variables

| Variable | Used for | Default |
|---|---|---|
| `SHAPELOGIC_SLIDE_DIR` | Where the Sliding Puzzle Solver writes what outgrows its memory, and its saved searches. | `sliding` in the cache folder |
| `SHAPELOGIC_SLIDE_DISK_GB` | The disk the Sliding Puzzle Solver may take, in GB. | Half the free space, at most 200 GB |
| `SHAPELOGIC_PANEX_DIR` | Where Panex searches are saved. `--panex-dir` overrides it. | `panex` in the cache folder |
| `SHAPELOGIC_PANEX_DISK_GB` | The disk saved Panex levels may take, in GB. `--panex-disk` overrides it. | Half the free space, at most 200 GB |

The **cache folder** is:

| System | Folder |
|---|---|
| macOS | `~/Library/Caches/ShapeLogic` |
| Windows | `%LOCALAPPDATA%\ShapeLogic` |
| Linux | `$XDG_CACHE_HOME/shapelogic`, or `~/.cache/shapelogic` |

Saved Panex searches can be large. Delete the `panex` folder to remove them
all, or use `--restart` for one problem. Saved sliding searches go after 30
days unused; delete the `sliding` folder to remove them sooner, or use
`--restart` for one problem.

## Benchmark and comparison switches

These switches change how a search runs, for measuring speed and for
comparing against the original BurrTools. **The answers stay the same.** You
don't need them to solve puzzles.

**Brick puzzles:**

| Switch | Effect |
|---|---|
| `--no-simd` | Use only the plain search and scalar loops, not the vector (SIMD) code. |
| `--no-row-check` | Check each assembly found against its rotations as a built assembly, not on the rows of the placement matrix. |
| `--split-tasks N` | Split a search on several threads into at least `N` tasks before it starts. `0`, the default, lets the assembler choose (the tasks split further while they are searched). |
| `--split-depth N` | Let that split go at most `N` pieces deep. `0`, the default, lets the assembler choose. |
| `--no-level-threads` | Take each assembly apart on one thread, instead of spreading its levels over several. |
| `--no-fast-analysis` | Use the take-apart's slower reference path for analysing moves. |
| `--no-fast-rotations` | Use the slower reference path for rotations (with `-R`). |
| `--no-rotation-anchor` | Use the slower reference path for anchoring rotations (with `-R`). |
| `--derived-checks` | On cubes and triangular prisms, take apart with the movement checks worked out from the cells' shapes, as the rhombic, tetra-octa, hexagonal and Cairo grids do, instead of the original's tables. |
| `--vector-memory MB` | The most memory the vector search of assembler 1 may take. Default 256; `0` turns that search off. |
| `--original-drawings` | Draw `-p`'s parts as `burrTxt` did, with the wrong shapes below the first separation. Only for comparing output with `burrTxt`. |

**Sliding puzzles:**

| Switch | Effect |
|---|---|
| `--slide-legacy` | The original search over text keys. |
| `--slide-apart` | Treat identical pieces as different, instead of searching them as one. |
| `--slide-stops-apart` | With `--optimized-paths` and `--nested-slides`: search each place a small piece can stop inside a grid cell on its own, instead of all of them as one. |
| `--slide-plain-keys` | With `--optimized-paths`: number every position of a piece in the search's keys, as a full search does, instead of only the places it can stop. The same search in more memory; a large board may then fall back to the original search. |
| `--slide-one-end` | Search from the start only, instead of from the start and the goal until they meet. The same number of moves, usually after far more arrangements. A saved search that already runs from both ends is carried on as it is. |
| `--slide-trace` | Print each level of the search to stderr. |
| `--slide-grid N` | `--optimized-paths` on a grid of `N` cells, whether or not the board fits it. Its answers are a restricted search's. |

**Stacking puzzles:**

| Switch | Effect |
|---|---|
| `--no-tower-rule` | Search the classic tower too, instead of using its known recursion. |
| `--panex-log` | Print each level of a Panex search to stderr. |

## Recipes

**Solve everything in a folder and report the level of each:**

```sh
for f in ~/Puzzles/*.xmpuzzle; do
  printf '%s: ' "$(basename "$f")"
  shapelogic_cli --json "$f" 2>/dev/null || echo "(could not solve)"
done
```

**Compare the two brick solvers on one puzzle:**

```sh
shapelogic_cli --json --solver classic puzzle.xmpuzzle
shapelogic_cli --json --solver crowell puzzle.xmpuzzle
```

**Check whether rotations open more assemblies:**

```sh
shapelogic_cli --json    puzzle.xmpuzzle | jq .solutions
shapelogic_cli --json -R puzzle.xmpuzzle | jq .solutions
```

**Solve a long job in the background and open the result in the app later:**

```sh
nohup shapelogic_cli -d --limit 0 --save solved.xmpuzzle puzzle.xmpuzzle > solve.log 2>&1 &
```

**Draw the first solution of problem 1 to a text file:**

```sh
shapelogic_cli -d -o 1 -s -p puzzle.xmpuzzle > problem1.txt
```

**Print a file's comment and list its saved solutions:**

```sh
shapelogic_cli -a c  puzzle.xmpuzzle
shapelogic_cli -a s0 puzzle.xmpuzzle
```

## Coming from BurrTools

`shapelogic_cli` reads the same `.xmpuzzle` files, and gives the same answers.

- **`burrTxt`'s options** are the same, and so is the text it prints. Scripts
  that read `burrTxt` output keep working with the program name changed.
- **`burrTxt2`** is `shapelogic_cli --save FILE`. Its single-letter options are
  long options here:

  | `burrTxt2` | `shapelogic_cli --save` |
  |---|---|
  | `-R` (restart) | `--restart` |
  | `-c` (count only) | `--count-only` |
  | `-m` (keep mirrors) | `--keep-mirrors` |
  | `-r` (keep rotations) | `--keep-rotations` |
  | `-p` (drop disassemblies) | `--drop-disassemblies` |

- **The solver names** of BurrTools, `"BurrTools Classic"` and
  `"Andrew Crowell"`, are still accepted by `--solver`. The BurrTools 2 solver
  is gone.
- **The Panex search's settings** have command line options as well as
  environment variables: `--panex-dir`, `--panex-disk`, `--panex-log` and
  `--no-tower-rule`.
- **Fixed:** `-p` draws the right pieces below the first separation (BurrTools
  drew the wrong shapes there; `--original-drawings` brings that back for
  comparisons). `-a s1` on a stacking puzzle, and `-d` on grids without a
  disassembler, now work instead of crashing.
