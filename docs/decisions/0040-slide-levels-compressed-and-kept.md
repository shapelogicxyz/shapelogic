# 0040: Slide levels on disk: compressed files, a measured need, and a search kept when a write fails

**Status:** accepted; `core/shapelogic/base/compress.h` (`BlockCompressor`,
`BlockDecompressor`), `core/shapelogic/solve/slide_levels.h` (`FileOptions`,
`kChunkBytes`, `kLookupChunkBytes`, `kKeptCompression`, `kRunCompression`,
`WriteGate`, the chunks of `KeySet`),
`core/shapelogic/solve/sliding_search.cpp` (`SetStore::make`,
`takePeakDisk`, `Merge::kept`, `runMerge`, `LevelSearch::run`, `pin`,
`noteWork`, `nextLevelNeed`, `saveFailed`),
`core/shapelogic/solve/sliding_search.h` (`SlideSearch::compression`,
`levelWorkRatio`, `failWriteAt`, `measureLevelBytes`, `kSlideCompression`),
`core/shapelogic/solve/solve_session.cpp` (`onSlidingAssembly`, `launch`,
`slidingSummary`), `core/shapelogic/base/system.cpp` (`readFileAt`),
`cmake/Dependencies.cmake` (zstd 1.5.7), `cli/` (`--slide-compress`, the
log lines of `--slide-trace`, `cut`, `reason` and `saved` of `--json`)

## Context
The user's "Climb Pro 24" (23 pieces, a known solution of 227 moves) was
searched from both ends with 1.2 TB of disk to use. After 11.9 hours, 219
of the 227 moves deep, with 891 GB of levels on the disk and 324 GB free,
the search ended 35 minutes into its next level, and the CLI printed
`"solutions":0`. Nothing was left on the disk.

Five things came together.

- **Making a level takes far more disk than the level.** The threads write
  everything the level's moves reach as sorted runs, every arrangement many
  times over and the two levels before among them; the runs are merged in
  stages of 32, and only the last merge takes the older levels out. The
  search took the next level to need five times the newest level's bytes
  (263 GB against 324 GB free), which was not enough here, and a stage of
  the merge kept all its inputs until the whole stage was done.
- **A failed write threw the search away.** A search that fills its budget
  stops between two levels and is saved. One whose write fails ended as
  Disk, not saved, and its folder went with it, the autosave of a few
  minutes before included.
- **The levels kept for tracing back were thinned too late.** Every other
  one goes when the levels kept plus five times the newest pass the budget;
  the test missed by 8 GB while the old levels took 890 GB of the disk.
- **The CLI's `--json` did not say why.** `"solutions":0` is what a proof
  prints too.
- **The app said "no solution" after it had said the search was cut.** The
  user's two runs of this puzzle in the app ended the same way, and the
  app's log has both: after 10.8 hours, 228 moves deep from both ends
  (92.2 billion arrangements), and after 9 hours, 138 moves deep from the
  start alone (73,674,878,466), *Could not write the search's files … The
  search could not be saved*, then *1 start layout was not searched
  completely*. But the crash recovery copy had asked for a save point, so
  the run ended paused at a save point, and the app carried the solve on
  by itself. The next run had no start left to search (the cut one was
  counted and passed over), counted its cut starts from nothing, finished,
  and put *Every reachable arrangement was searched: this puzzle has no
  solution* in the place of the first note. The solve was kept as solved,
  with no solutions.

Neither of those runs, nor this one, searched everything. (The 138-move
run's count fits this run's levels: its start levels 1 to 135 hold
57,785,644,014, and levels 136 and 137, growing as the levels before them
did, would bring that to about 73.67 billion.)

The files themselves had room to give. A level is sorted keys kept as the
gaps between them, about 8 bytes an arrangement where a key takes 16. The
gaps still repeat: on a level of 126,888,601 arrangements (1.10 GB) zstd
took a third off, gzip the same more slowly.

| On the whole file | Smaller by | Time, all cores |
|---|---|---|
| zstd -1 | 30.4% | 0.9 s |
| zstd -3 | 34.5% | 0.7 s |
| zstd -9 | 38.4% | 4.3 s |
| zstd -19 | 42.5% | 62 s |
| gzip -6 | 35.9% | 33 s (one core) |

A run of the same level's making (1.05 GB) went down by 44.9% at zstd -3.

## Decision
**The files are compressed with zstd, chunk by chunk.** A file's blocks (128
keys each, unchanged) are gathered into chunks of `kChunkBytes` (256 KB)
and each chunk is written as a zstd frame with a checksum. A block's offset
in the index counts the bytes before it as they were before compressing,
and a table of the chunks (where each begins among those bytes, and where
in the file) follows the index, so a block is still read by itself: its
chunk is read and decompressed, and kept by the reader for the blocks after
it. A chunk zstd cannot shrink is written as it is; the two lengths in the
table tell. A set in memory keeps its blocks as they are. The file ends
`BTSLKEY3`.

- Chunks, not the whole file, because the search reads a level from many
  places at once (each thread merges a range of keys) and looks single
  arrangements up. On a quarter of the level above, chunks of 256 KB at
  zstd -9 take 35.8% off; 64 KB 34.2%; 16 KB 32.1%.
- A search with nested slides looks keys up in its older levels a few at a
  time, each in a different chunk of a large level. Its files take chunks
  of `kLookupChunkBytes` (16 KB), so that a key looked up decompresses 16
  KB and not 256.
- Level 9 (`kSlideCompression`, the A/B `SlideSearch::compression` and
  `--slide-compress`; 0 writes the chunks as they are) for the levels as
  the last merge writes them, every thread a part: nearly all that a long
  search keeps. Level 1 (`kRunCompression`) for what is read once and
  deleted, the runs and the stages of their merge, which are most of what
  is written; and for what one thread writes while the search waits for
  it: a level that leaves memory, a save's files (a pause must not take
  minutes), the merged visited sets of nested slides. Level 9 ran at
  about 255 MB/s on all ten cores here, level 1 about five times as fast. `Merge::kept` and
  `SetStore::make`'s `kept` say which.
- Sets are joined as before without decoding their keys: a file's chunks
  are copied as they are, whatever they were written with.
- The library is zstd 1.5.7, pinned and always built from its source as a
  static library (a zstd found on the machine would be a shared library the
  packaged app does not carry), one thread a call, without its dictionary
  builder or old formats. `base/compress.h` is the only code that sees it.
  zlib, already linked, compresses a level at 30 to 80 MB/s a core, too
  slow for a search that writes hundreds of GB an hour.

**Saved searches of earlier versions.** At first a level file that ends
`BTSLKEY2` was not read, and the saved search was deleted, as any that
could not be carried on. Decision 0041 replaced both: such a search is
carried on, its files written again compressed, and no saved search is
deleted for being unreadable. A search saved with one compression level is
carried on with another.

**The need of a level is measured.** The store keeps the most its finished
files took (`takePeakDisk`: a set counts when it is finished, while the
sets it was made from are still there). Each level that goes to the disk
gives a ratio: that peak, less what the search held before the level, for
each byte of the level. The next level is taken to need the greatest ratio
of the last four levels, with a quarter added, times the bytes this level
has, growing as it grew on the one before (`nextLevelNeed`); five times the
level, as before, until a level of at least 1 MB has been measured. The
search stops and saves itself (Disk) when the disk has less free than that
need, a save's writes and the reserve.

**A merge lets its inputs go as it merges them.** A stage of the merge
frees each group of 32 runs when the group is merged, and the last merge
frees its inputs before the level's parts are joined. The runs and the
stages are then on the disk once, not twice.

**The levels kept are thinned at half the budget.** Every other level kept
for tracing back goes (without nested slides) once those levels take half
the disk budget, or leave less of the budget or of the disk's free space
than the next level needs. Tracing back across a gap is a search of as many
moves as the gap is wide, from one arrangement: seconds, against the hours
the levels took. The levels thinned out are files the last save names, so
the search saves again at once and they go.

**A search that cannot write a file is saved all the same.** When a write
fails the search ends (Disk) as before. By then the level it was making has
been given up and its runs are deleted, which is the space that ran out.
The search then saves itself as it stands, at its last whole level. If
that cannot be written either, the files its last save named are all still
there (they stay until the next save names others: `pin`), and the folder
is kept as that save has it; a search carried on pins what it read, so the
same holds before its first save. Only a search with neither is thrown
away. `SlideSearch::saved` is set, the error names the folder, and
`diskHeldToFree` says that more room, not a larger budget, is what helps.
An autosave that fails ends the search the same way (it ended as Memory,
with the folder deleted). The merge of two visited sets no longer takes
them out of the list before the merged set is whole.

**A search that ran out of disk always pauses the solve.** In a
`SolveSession` a start whose search ended as Disk goes back as pending and
the run ends, whether the search was saved or not; the solve is Paused,
not at a save point (nobody carries it on by itself), and the note says
which: *The search is saved: press Continue to carry it on*, or *The
search could not be saved: Continue searches this start again from the
beginning*. The starts a search gave up on for good (the memory limit,
with no folder) are the solve's count, not the run's: a solve carried on
keeps it, and only a solve begun again starts it from nothing.

**No read or write of more than 1 GB in one call.** macOS refuses a
`pread` of 2 GB or more, and `readFileAt` passed the whole length: the
index of a level of more than 17 billion arrangements (2 GB of first keys)
could not have been read back. A hidden test (`[.huge]`) reads 2.6 GB in
one call and builds, joins, reads and looks through a set of 9.19 billion
keys, more than 2^33.

**The CLI says why.** `--json` adds `cut` (the start layouts not searched
completely), `reason` and `saved` when a sliding search stopped short, and
nothing otherwise. `--slide-trace` also prints the search's log lines, the
debug ones included: each save, the disk and the need of the next level,
the levels thinned, and why a search stopped.

## Measurements
Release build, MacBook Air M4, one run each unless said, nothing else
running. "Climb Pro 24" from its start to the final board of the known
solution, so from both ends; the times are to the start end's level 101
(126,888,601 arrangements), the disk is what the search held then. The
levels have the same arrangements in every run, level for level.

| | Time | Disk |
|---|---|---|
| Before | 561 s, 603 s (two runs) | 15.9 GB, 17.0 GB |
| Now, `--slide-compress 0` | 615 s | 16.2 GB |
| Now, as decided: level 9 for the merge's levels, 1 for the rest | 601 s | 10.1 GB |
| Level 3 for everything kept, what one thread writes too | 679 s | 9.9 GB |
| Level 9 for everything kept, what one thread writes too | 735 s | 9.5 GB |

The last two rows are why one thread's writes get the fastest level: a
level that leaves memory, or the parts of one joined from memory, written
at level 9 by one thread held the search up for most of a minute each.
Up to here much of the search was in memory first; of a long search
nearly all is written by the merge.

Making a level took 3.7 to 4.3 times the bytes the level came to (levels
100 to 104: 3.68 GB at its most for a level of 1.01 GB), with the files
compressed and the merge's inputs let go as they are merged. The run that
failed needed more than 6.1 times (324 GB free, a level of 53 GB), with
neither.

Carried on with a budget of 6 GB (the saved search held 10.7 GB, 101 and
55 moves deep): the levels kept were thinned to every 8th at once, 4.3 GB
in all after the next level, the search went on to 106 and 59 moves, where
it held 7.0 GB, and stopped saved. Carried on again without the budget, its
next levels were those of the first run.

A real full disk: the search's folder on a 4 GB disk image, which another
file filled while the search ran. The write failed 85 and 44 moves deep;
the CLI printed `"cut":1,"reason":"Could not write the search's files in
…. The disk may be full.","saved":true`, and the folder held the search
as it was last saved, 84 and 43 moves deep. With the other file deleted
the same command carried it on to 92 and 48 moves, where it stopped, saved,
with 2.97 GB free against the 3.18 GB the next level and a save would take.

Not measured: the whole search to its 227 moves; a search with nested
slides large enough to go to the disk; the builds for Windows and Linux
(the WebAssembly build compiles).

## Consequences
- A level's file is about a third smaller, and the search writes less
  than half the bytes it did while it makes one.
- A full disk costs at most the level under way, and the app no longer
  turns a cut search into "no solution".
- "Climb Pro 24" can go on from 219 moves with the disk it had.
- A saved search of an earlier version is carried on (decision 0041).
- Tests: a key set's file at four compression levels and three chunk
  sizes, read from end to end, from a key on and looked up; sets joined in
  every mix of memory and files; a file cut short and one with a damaged
  chunk; the same search with its files compressed
  and not, and one saved with one level carried on with another; a write
  failed once at a dozen places of a search, each saved and carried on to
  the same path; a disk that stays full, with and without an earlier save;
  the levels thinned at half the budget; the CLI's switch; a session
  whose search cannot write, saved and not, paused for Continue; the cut
  starts kept over a pause; a set of more than 2^33 keys and a read of
  more than 2 GB (hidden: 4 GB of disk and memory).
- Soft spots: a read error (a damaged chunk, told by its checksum) ends
  the search with the message of a failed write. The index of a file is
  not compressed (about 4% of the file). The executables grow by zstd.
  A level being made may still pass the disk budget for a while, as it
  always could: the budget counts what the search keeps.
