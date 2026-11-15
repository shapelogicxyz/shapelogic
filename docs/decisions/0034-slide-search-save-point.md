# 0034: The slide search's save point

**Status:** accepted; `core/shapelogic/solve/sliding_search.{h,cpp}`
(`SlideSearch::savePoint`, `LevelSearch::loop`),
`core/shapelogic/solve/solve_session.{h,cpp}` (`pauseAtSavePoint`,
`savePointRequested`, `slideSavePointCut_`, `atSavePoint_`),
`core/shapelogic/solve/solve_progress.h` (`SolveProgress::atSavePoint`),
`api/` (`solve.savePoint`, `SolveProgress.atSavePoint`),
`web/src/renderer/solver/autosave.ts` (`Autosave`: `worksOn`, `join`,
`carriesOn`)

## Context
The crash recovery journal (decision 0024) saves a running solve by pausing
it at a save point, writing the rescue copy and carrying it on. A sliding
solve had no save point short of its search's end (decision 0010: the
assembly search stops, the search under way finishes), which on a large
board is the whole solve, so the journal paused it instead (`solve.pause`).
A pause cuts the level being expanded short: its arrangements are taken
back off the count and the level is searched again when the search is
carried on.

That holds while a level is shorter than the time between two saves. The
time between two saves is 50 times what the last one cost, never under a
minute. A save after a finished level writes that level to the search's
folder and costs seconds, so the next is minutes away; a save that finds
nothing new to write costs about a second, so the next comes a minute
later. Once a level took longer than the wait that followed a save, that
save cut it, the save after it was cheap, and from then on every save came
a minute after the last and threw the level away again.

The user's "Abe 1 to 8 Heart" (nested slides from both ends, decision 0033)
reached 178 moves, levels of 14 minutes and 218 GB on disk, and stood there
for seven and a half hours: `state.bin` written every 61 seconds, no level
file after the first cut one, the count on screen never moving. Nothing was
lost, and nothing was gained.

## Decision
**The level search has a save point: the end of the level it is on.**
`SlideSearch::savePoint` is a second stop token. The level search polls it
only between two levels, after a level of the run: asked for, the level
under way is finished and the search stops there (`Stopped`), saved as a
pause saves it. Carried on, it has nothing to search again, and every run
that stops at a save point ends at least a level further than it began. No
cadence of saves, however short against a level, can hold the search in
place: that is a property of the search now, not of when its caller asks.

It is polled only with somewhere to save (a work folder), and not by the
search over text keys: those run to their end at a save point, as before.
A stop asked for meanwhile wins and cuts the level, as a pause must.

**The session.** A sliding run gives its searches the assembly search's
token as their save point (`pauseAtSavePoint` stops that token, as it
always did). A start whose search stops there goes back as pending, not
counted, and is the run's last: the assembly search stops with it, and no
note is told, the search not being through. A start left pending by an
earlier run is searched even when the save point was asked for before its
search began, so a run carried on gets somewhere.

**`SolveProgress::atSavePoint`.** With a level taking minutes, a save point
is nearly always pending when a run ends, for whatever reason. A run that
ends Paused at the save point and for nothing else says so in its last
progress; one that paused because its search ran out of disk, or was
paused by the user, does not. Whoever asked for the save point carries on
only a run that says so. Without it the journal would carry on a search
that had just stopped for a full disk, over and over, and the user would
never see why. The disk is checked before the save point for the same
reason: a save point must not hide a full disk.

**The journal** (`autosave.ts`) asks a sliding solve for a save point and
waits, however long the level takes; the search is shown searching
meanwhile, not saving. What the save cost counts from the run's end: the
wait cost the search nothing. A run that finished or failed while the
journal waited is not the journal's: its note is told as any run's.

**A save the user asks for** (File ▸ Save) does not wait for a level's end,
which may be an hour away: it pauses the search, which is carried on redoing
the level cut short, once, by the user's choice. Asked for while the
journal waits, it takes the journal's place (the search is paused, the
puzzle saved to its file instead of the rescue copy); asked for in the
moment the journal writes, it follows it. It was refused before ("A save is
under way already"), which was a moment then and would be a level now.

## Consequences
- Saved searches are untouched: `state.bin` and the level files are written
  and read as before (`BTSLSV01` and `BTSLSV02`), and a search is named as
  before. A search paused by an earlier build, the stalled one included, is
  carried on by this one from where it stood.
- The rescue copy of a deep sliding solve is written once a level, not once
  a minute. The search's own save into its folder is as it was (after a
  level, by `SaveCadence`), so a crash still loses at most the level under
  way; the rescue copy holds the start to carry on from, and Continue takes
  the search up from its folder.
- `solve.savePoint` on a sliding solve can take as long as a level does.
  A client that cannot wait asks for `solve.pause`.
- `SolveProgress` has one more field; the fake engine's recordings were
  recorded again.
- Other grids are as they were, but for one thing: the journal now carries
  on only a run whose progress says `atSavePoint`, where it carried on any
  run it found paused.
