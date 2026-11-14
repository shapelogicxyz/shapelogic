# 0032: A grid's slide keys number only the stops

**Status:** accepted; `core/shapelogic/solve/sliding_search.cpp`
(`PosTable::code`, `PosTable::row`, `SlideSpace::posOf`, `field`, `with`,
`kCompactFormat`, `findSlidePath`'s build order and saved pass),
`core/shapelogic/solve/sliding_search.h` (`SlideSearch::plainKeys`,
`SlidingOptions::plainKeys`), `cli/` (`--slide-plain-keys`)

## Context
The level search packs an arrangement into a key of 64 or 128 bits, a field
for each piece. `buildTables` sized the field for every position the piece
fits the empty tray at. With optimized paths (decision 0029) a piece only
ever rests at its stops on the grid, a small part of those positions, and
the rest of the numbers were never in a key.

The user's "Abe 1 to 8 Heart" (a 24x18 tray as a 4x3 grid of 6x6 cells, 20
pieces) needed about 170 bits that way. It missed the 128-bit key and fell
to the original search: about 11,000 arrangements a second on one thread, in
memory only, with no depth to show.

The build also went a key width at a time: loose stops (decision 0031) and
then the stops apart in 64 bits, then the same in 128. With narrower fields
that order would cost a board its loose stops without a word, when they
need a little over 64 bits and the stops apart fit 64.

## Decision
**A key holds a code, not a row.** With a grid, `PosTable::code` numbers the
rows a key can hold: the piece's stops and its loose classes. `PosTable::row`
is the way back. The field is as wide as the codes need. Without a grid
every row is a stop and the two maps are the identity, so those keys are bit
for bit what they were.

**One place converts.** `SlideSpace::posOf` gives the row a key holds for a
piece, `field` the code, and `with` writes the code of a row. The mover
works on rows throughout, as before.

**Group floods pass through rows that are not stops.** A group slides
through positions where a member is between stops. `expand` and `floodGroup`
flood on rows and pack a key only for a shift where every member rests.

**Build order.** Loose stops in 64 bits, loose in 128, stops apart in 64,
stops apart in 128, then the original search. The loose stops are worth more
than the narrow key: about a third of the arrangements (decision 0031)
against twice the bytes for each.

**Saved searches are carried on as they were begun.** The fingerprint mixes
`kCompactFormat` only when the keys are compact, so a search without a grid
has the name it had, and a grid search saved by an earlier build keeps its
own. When the problem has saved folders and the search is to be carried on,
`findSlidePath` first looks for a saved search of each form the tables can
take (loose or apart, compact or plain, 64 or 128 bits) and carries on the
one it finds, with the tables built that way. Only then is a new search
begun, compact.

`SlideSearch::plainKeys` (`SlidingOptions::plainKeys`, `shapelogic_cli
--slide-plain-keys`) is the A/B switch: every position numbered, as before.

## Measurements
Release build, MacBook Air M4, nested slides, optimized paths, one run each.

"Abe 1 to 6 Heart", Problem 3, stopped after 100 s. The levels are the same
size, level for level, with compact keys, with plain keys and in the build
before this.

| | Compact | Plain | Before |
|---|---|---|---|
| Time to level 39 (21,982,038 arrangements) | 84.8 s | 87.5 s | 91.3 s |
| Memory at level 38 | 621 MB | 1,152 MB | 1,152 MB |

With every position numbered this problem takes the 128-bit key; compact,
it fits 64 bits with its loose stops.

"Abe 1 to 8 Heart", Board 1: the key is 100 bits with the stops apart. The
level search runs where the original search did: 59 levels in 105 s, level
59 holding 20,403,742 arrangements, from the start alone.

A search of 6 Heart Problem 3 saved by the build before this (stopped on its
disk limit at level 37) was carried on by this build from level 38, with the
level sizes of an uninterrupted run.

## Consequences
- The same search, arrangement for arrangement, in fewer bits: about half
  the memory and disk on 6 Heart, and boards as big as 8 Heart on the level
  search, with its threads, its disk and its depth.
- A board whose loose stops need 65 to 128 bits keeps them.
- Tests: every grid fixture and ten generated boards give the same levels
  and the same fewest moves with compact and plain keys, loose and apart,
  on one thread and three; a generated board of blocks too big for 128 bits
  plain is solved by the level search and agrees with the original search;
  a board whose loose stops need the wide key runs loose; a search saved
  with plain keys is carried on, and one saved compact is not picked up by
  a plain search.
- Soft spots: a saved folder that turns out damaged is wiped and the search
  begun again in the form the folder had, which for one saved by an earlier
  build means plain keys. A full search (no grid) of a big board still
  misses 128 bits and falls to the original search.
