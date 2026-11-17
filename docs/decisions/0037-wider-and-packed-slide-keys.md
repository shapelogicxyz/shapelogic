# 0037: Slide keys past 128 bits: copies as one number, then wider keys

**Status:** accepted; `core/shapelogic/solve/slide_key.h` (`BigKey`,
`keyField`, `keyAt`, `keyClear`), `core/shapelogic/solve/sliding_search.cpp`
(`SlideSpace::Pack`, `layout`, `rank`, `unrank`, `positions`, `encode`,
`lift`, `put`, `SlideMover::expandFrom` and `emitted`, `chainToPath`,
`eachKey`, `kPackedFormat`), `core/shapelogic/solve/slide_levels.h`
(`kMaxVarint`), `core/shapelogic/solve/sliding_search.h` (`SlidePacking`,
`SlideSearch::packing`, `minKeyBits`, `keyWidth`, `keyBits`, `packedKeys`),
`cli/` (`--slide-packing`, `--slide-key-bits`, the key line of
`--slide-trace`)

## Context
The level search packs an arrangement into a key, a field for each piece
that holds the code of its position (decision 0032). The key was a built-in
integer of 64 bits, or of 128 for boards that need more. A board that
needed more than 128 fell to the original search over text keys: one
thread, in memory only at about 200 bytes an arrangement, from the start
alone, with no depth to show and nothing saved when it stops.

The user's "Abe-ClimbPro24" is such a board with no grid to narrow it: 23
pieces on 64 cells, each with 41 to 64 places, so 6 bits each and 138 in
all. Both its problems stopped at the memory limit after about 9.7 million
arrangements, and said "0 solutions" where nothing had been proved.

Two things were in the way, and they are separate: the width of the key,
and how much of it a board with many copies of a piece wastes.

- 138 bits is ten too many, and 20 of them say nothing. The search does not
  tell the copies of a piece apart (`canon` keeps their codes in rising
  order), yet each copy had a field of its own: four copies with 64 places
  each took 24 bits for one of the 635,376 ways of choosing four places,
  which 20 bits number.
- `unsigned __int128` is the widest integer the compilers have.
  `_BitInt(256)` is not there to use: GCC has `_BitInt` in C only, and
  clang, which takes it in C++ as an extension, stops at 128 bits on arm64
  and on WebAssembly (tried with Apple clang 21, clang 23 and GCC 16).

## Decision
**Packed keys: the copies of a piece are one number.** With packed keys a
group of copies (`SlideSpace::copies`) has no field for each copy. The codes
its copies hold are all different (two copies never share a position), so
they are one of the ways of choosing so many of the piece's codes, and the
key holds that way's place among them all, by the combinatorial number
system: with the codes rising, c1 < c2 < ... < ck, the number is
C(c1, 1) + C(c2, 2) + ... + C(ck, k) (`rank`; `unrank` takes the greatest
code first, each the greatest that adds no more than is left). The binomials
are a table a group (`Pack::choose`), so a number is k look-ups to make and
a short binary search a copy to read. A group whose number would pass 63
bits, or of more than 16 copies, is kept as several packs, the lowest codes
in the first.

A packed key is the same whichever copy sits where, so `canon` has nothing
to do, and the search is the same search: the same arrangements a level,
the same fewest moves.

**The mover works on positions, and keys are made whole.** A piece alone is
moved by `lift` and `put`: the key without the piece (packed: without its
group's number, the other copies' codes kept aside), then with the piece put
down at each place it reaches. Not packed that is the mask and the or it
always was. A group of nested pieces is told from its positions (`encode`)
when the keys are packed, as copies in one group cannot be written one by
one: the first would land on the second's old place.

**Which copy is which comes from the positions, not the key.** A packed key
does not say which copy sits where, and two things need it. `undoable` asks
whether the same pieces are nested in the mover after a group move: with
packed keys it reads the flood's own positions (`next_`). And the path keeps
every piece's identity from step to step: `chainToPath` now follows the
positions (`expandFrom`, `emitted`) where it followed keys that were not
canon'd. Both are one code path for packed keys and not.

**Wider keys: `BigKey<W>`.** An unsigned integer of W 64-bit words with the
operations the level search and its key sets ask of a key: order, the bit
operations, shifts, sums and differences (the key sets keep the gaps between
keys as varints), and the fields read and written from the one or two words
they lie in (`keyField`, `keyAt`, `keyClear`, which a built-in key answers
with a shift and a mask). The level search is a template on its key, so it
runs on these unchanged. The longest varint follows the key (`kMaxVarint`).

**The order (`eachKey`).** A code for each piece in 64 bits, then in 128:
those searches are bit for bit what they were, and a search saved by an
earlier build is carried on. Then packed in 128 bits, then packed in 192,
256 and 512 bits, then the search over text keys. Packed comes before wider
because a wider key costs more for every arrangement held and sorted
(below). Loose stops (decision 0031) are never packed: their moves work on
the fields, and a loose position is not one place. With them the wider keys
hold a code for each piece.

**Saved searches.** The name of a search already mixes the key's size, and
now mixes `kPackedFormat` when the keys are packed, so a search is carried
on only by one with the same keys.

`SlideSearch::packing` (`Auto`, `Always`, `Never`) and `minKeyBits` are the
A/B switches (`--slide-packing`, `--slide-key-bits`). The search says what
it ran in: `keyWidth`, `keyBits`, `packedKeys`; `--slide-trace` prints it,
and the application's debug log has a line for it.

## Measurements
Release build, MacBook Air M4, one run each, nothing else running.

"Abe-ClimbPro24", problem 1 (every piece has a goal, so from both ends):
118 bits packed, 138 with a code for each piece.

| | Packed, 128-bit key | A code each, 192-bit key | Before (text keys) |
|---|---|---|---|
| Level 60 from the start (769,965 arrangements) | 1.3 s | 1.5 s | |
| Level 80 from the start (10,572,862) | 34.4 s | 41.1 s | |
| Memory at level 75 from the start | 749 MB | 876 MB | |
| After about 53 s | 137 moves deep (83 from the start, 54 from the goal) | 135 moves deep (81 and 54) | after 56 s, stopped at the memory limit: 9.7 million arrangements, no depth |

The levels are the same size, level for level, packed and not.

Boards that fit 64 or 128 bits, against the build before, two runs each:

| | Before | Now |
|---|---|---|
| "Abe 1 to 6 Heart", problem 1 (nested slides, optimized paths) | 10.0 s, 10.2 s | 9.5 s, 10.0 s |
| problem 2 | 20.8 s, 20.4 s | 20.1 s, 20.2 s |
| problem 3 | 6.9 s, 6.6 s | 6.6 s, 7.0 s |
| "Abe-ClimbGame15", problem 1 | 39.0 s, 48.4 s | 41.6 s, 35.6 s |

## Consequences
- A board past 128 bits runs on the level search: every core, the disk, the
  search from both ends, pause and carry on, the moves deep and the
  Performance Analysis. Only one past 512 bits packed, or one of more than
  64 pieces with nested slides, still goes to the search over text keys.
- A board with many copies of a piece often stays in the 128-bit key.
- A "no solution" of such a board is now a proof: the search goes through
  every arrangement, where the search over text keys stopped at its memory.
- The plain searches are unchanged: the same keys, the same saved searches,
  the same speed.
- Tests: `BigKey` against the 128-bit integer and across its words, and in
  the key sets; a board of 135 bits searched packed in 128, with a code each
  in 192, and packed in 192, 256 and 512, from one end and from both, with
  the same levels; packed against not packed on Klotski, the grid boards and
  the nested boards, on one thread and three; the moves made backwards with
  packed keys; a packed and a wide search saved and carried on, and not
  taken up by a search with other keys; the CLI's switches.
- Soft spots: the executable grows by about 1.2 MB (the level search is
  built for three more keys). A wide key's shifts and gaps work on every
  word; the fields are read from one or two. With loose stops a board past
  128 bits is not packed. Packing in the 64-bit key before going to 128
  (`packing` `Always` does it) might be worth it for the memory, and is not
  measured.
