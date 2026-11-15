# 0036: Several windows, a file owned by one

**Status:** accepted; `web/src/main/index.ts` (`createWindow`, `cascaded`,
`closeNextForQuit`, `before-quit`, `second-instance`),
`web/src/main/path-owners.ts` (`PathOwners`), `web/src/main/rescue.ts`
(`RescueSlots`), `web/src/main/app-ipc.ts` (`bt:claim-path`,
`bt:new-window`, `bt:prefs`), `web/src/main/menu.ts` (New Window, the
Window menu), `web/src/renderer/shell/files.ts` (read-only Save),
`web/src/renderer/shell/persist.ts` (settings from another window)

## Context
The main process was built per window from the start (decision 0013: one
`shapelogic_engine` per window, menus from the focused window, a close
asked by its own page), but a second window was only ever opened when none
was open. People wanted a second puzzle open beside the first: to edit one
while another solves, or to compare two. Three things assumed one window:

- **The rescue copy.** There was one, `__rescue.xmpuzzle` in the app's data
  directory. Two windows would overwrite and remove each other's, the
  copy the crash recovery journal writes during a solve among them.
- **The same file twice.** Nothing stopped one file from being open in two
  windows. Each window's engine has the document in memory, so two solves
  of it ran side by side, and the last Save lost the other window's
  solutions. Decision 0035's lock only covers the folders of sliding and
  Panex searches.
- **The quit.** A quit was held by the first window's question and went on
  when that window closed. Electron also sends `close` to every other
  window at once, so several questions came up together, and a Cancel in
  one did not stop the others from closing.

Several processes, one per window, would have needed the store and the
rescue copy split by process. The single-instance lock exists because a
second process would take the first one's rescue copy for one left by a
crash, and offer it or remove it.

## Decision
**One process with many windows.** The single-instance lock stays. A
second start with files hands them to the running app, as before. A
second start without a file (Shift-click on the Windows taskbar, the
launcher again, `open -n`) asks it for a new window. **File ▸ New Window**
(`Shift+Ctrl+N`) opens one with an empty puzzle, cascaded from the focused
window. New and Open still replace the document of the window they are
used in. The Window menu lists the windows: on macOS this is macOS's own
(`windowMenu`); elsewhere the main process builds the list.

**The first window to hold a file owns it; another holds it read-only.**
The main process keeps the owners (`PathOwners`):

- A window holds the file its menu state names.
- It also claims the file it has just opened (`bt:claim-path`, once the
  open has succeeded), so opening the same file again counts as a new
  hold.
- A read-only window's Save is Save As, with "<name> copy" proposed.
- The main process refuses that window's `document.save`, with a path or
  without, before the engine sees the call. The renderer's mark is only a
  mirror of this.
- No window may save over a file another window owns.
- When the owner lets the file go (it closes, or opens another file), a
  read-only window stays read-only: its copy may be older than the file on
  disk. Opening the file again in that window makes it the owner.

Editing and solving stay allowed in a read-only window. Save As keeps that
window's work.

**One rescue copy per window.** Each window takes a slot as it opens and
keeps it while it lives, across reloads and engine restarts.

- Slot 0 is `__rescue.xmpuzzle`, the earlier versions' one copy, so an
  upgrade offers the old copy as it did. Slot n is `__rescue-n.xmpuzzle`.
- A new window takes the lowest free slot that holds a copy, so each copy
  left by a crash is offered in one window only. The next New Window
  offers the next copy.
- A copy a window leaves as it closes (not answered, or not saved because
  the engine was not running) waits for the next window.
- The cache manager lists every slot's copy.

**A quit asks the windows one at a time.** `before-quit` is held while any
window is open. The focused window is asked first, then each of the others
in turn as the one before it closes. A Cancel, on a page or in the native
question of a window that does not answer, ends the quit and leaves the
windows not yet asked open. The quit goes through once no window is left.

**The settings are the app's.** A window that stores its settings sends
them to the others (`bt:prefs`). Those windows take them without storing
them again, and send their engine any engine settings that changed. A
layout stays each window's own.

## Consequences
- Each window's solves use the whole thread and search memory settings
  (decision 0028), so two large solves at once share the computer's cores
  and memory. The guide says so.
- A file changed on disk by something other than a window (the CLI, the
  original program) is not noticed: the owner saves over it, as before.
- Each window's engine is a separate process with its own memory (about
  the size of one engine).
