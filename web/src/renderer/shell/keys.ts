// SPDX-License-Identifier: GPL-3.0-or-later
//
// The keyboard and mouse map: ONE table (AGENTS.md, "Keys come from the one
// table"). Each row is a row of docs/webui-spec.md section 6 as the
// mockup's help list words it (docs/mockup/index.html: `HELP`, `kbKeys`,
// `kbRow`, and the keydown handlers' keys); the settings dialog's
// "Keyboard shortcuts" page and the F1 dialog (help.ts) print it, the
// shell's keydown handler (keyboard.ts) dispatches from its chords, and the
// menus show its keys (`accelerator`) without registering their own. Rows
// whose actions come with a later stage have no chords yet: that stage
// adds them here. One press has one action, named for what the press means
// wherever it acts (`delete`; `prev` and `next` for ↑ and ↓, a shape or a
// solution; `home`, `page.up`, `activate`...): each workspace says what it
// does there (keyboard.ts tries their handlers in turn), and a row whose
// keys an earlier row already holds names no chord of its own. The shapes
// list's rows act while the list has the focus, the Solver's while its
// solutions list, slider or move seekbar has it, the explorers' while their
// drawer has it.

/** What a key does; keyboard.ts maps each to a handler. */
export type KeyAction =
  | 'workspace.entities'
  | 'workspace.puzzle'
  | 'workspace.solver'
  | 'help.keys'
  | 'settings'
  | 'card.left'
  | 'card.right'
  | 'focus3d'
  | 'escape'
  | 'view.orbit'
  | 'view.pan'
  /** Home: the home view; the first solution, or the start of the moves on the seekbar. */
  | 'home'
  /** End: the last solution, or the end of the moves on the seekbar. */
  | 'end'
  | 'view.fit'
  | 'undo'
  | 'redo'
  | 'tool.fixed'
  | 'tool.variable'
  | 'tool.erase'
  | 'tool.paint'
  | 'edit3d'
  /** PgUp: the next layer; 8 solutions back. */
  | 'page.up'
  /** PgDn: the previous layer; 8 solutions forward. */
  | 'page.down'
  /** ↑: the previous shape or solution. */
  | 'prev'
  /** ↓: the next shape or solution. */
  | 'next'
  | 'shape.earlier'
  | 'shape.later'
  | 'shape.rename'
  /** Enter, Space: rename the shape (Enter, the list); play or pause; cycle a piece chip. */
  | 'activate'
  /** ←, →: one move back or forward; the previous or next piece chip. */
  | 'move.back'
  | 'move.forward'
  /** Shift+←, Shift+→: jump to assembled or fully apart. */
  | 'move.start'
  | 'move.end'
  | 'shape.duplicate'
  | 'piece.more'
  | 'piece.fewer'
  | 'delete'
  /** The File menu's keys (stage 6): they act in a text field too. */
  | 'file.new'
  | 'file.open'
  | 'file.save'
  | 'file.saveAs'
  | 'file.close';

/**
 * A key press. `mod` is Ctrl, or Cmd on macOS (the mockup takes either);
 * the other modifiers must be up unless named. `key` is compared without
 * case; `code` (a physical key) is tried too, for layouts whose `key`
 * differs (Ctrl+[ on a German keyboard).
 */
export interface Chord {
  key: string;
  code?: string;
  mod?: boolean;
  shift?: boolean;
  alt?: boolean;
}

/** One row of the map. */
export interface KeyRow {
  /** The group (webui-spec.md's "Where") and the mockup's note on it. */
  group: string;
  sub?: string;
  /**
   * The keys as shown: alternatives split by " / ", steps by " · ", keys
   * joined by "+"; a part starting with @ is a mouse action.
   */
  keys: string;
  /** What it does. */
  does: string;
  /** The presses that do it, each with its action. */
  chords?: readonly { chord: Chord; action: KeyAction }[];
}

const mod = (key: string, code?: string): Chord =>
  code === undefined ? { key, mod: true } : { key, code, mod: true };

/** The map, in the order the help prints it. */
export const KEY_MAP: readonly KeyRow[] = [
  {
    group: 'Everywhere',
    keys: 'Ctrl+1 / Ctrl+2 / Ctrl+3',
    does: 'Switch to Shapes / Puzzle / Solver',
    chords: [
      { chord: mod('1', 'Digit1'), action: 'workspace.entities' },
      { chord: mod('2', 'Digit2'), action: 'workspace.puzzle' },
      { chord: mod('3', 'Digit3'), action: 'workspace.solver' },
    ],
  },
  {
    group: 'Everywhere',
    keys: 'F1',
    does: 'Keyboard shortcuts (this list)',
    chords: [{ chord: { key: 'F1' }, action: 'help.keys' }],
  },
  {
    group: 'Everywhere',
    keys: 'Ctrl+,',
    does: 'Settings',
    chords: [{ chord: mod(',', 'Comma'), action: 'settings' }],
  },
  {
    group: 'Everywhere',
    keys: 'Ctrl+[ / Ctrl+]',
    does: 'Collapse or expand the left / right card',
    chords: [
      { chord: mod('[', 'BracketLeft'), action: 'card.left' },
      { chord: mod(']', 'BracketRight'), action: 'card.right' },
    ],
  },
  {
    group: 'Everywhere',
    keys: 'Esc',
    does: 'Step back: close dialog → close menu → cancel rename → pause playback → exit focus mode',
    chords: [{ chord: { key: 'Escape' }, action: 'escape' }],
  },
  {
    group: 'Everywhere',
    keys: 'Ctrl+Z / Shift+Ctrl+Z',
    does: 'Undo / redo (the status bar names the step)',
    chords: [
      { chord: { key: 'z', code: 'KeyZ', mod: true, shift: false }, action: 'undo' },
      { chord: { key: 'z', code: 'KeyZ', mod: true, shift: true }, action: 'redo' },
    ],
  },
  {
    group: 'Everywhere',
    keys: 'Ctrl+N / Ctrl+O',
    does: 'New puzzle (its grid is chosen once) / Open a puzzle',
    chords: [
      { chord: mod('n', 'KeyN'), action: 'file.new' },
      { chord: mod('o', 'KeyO'), action: 'file.open' },
    ],
  },
  {
    group: 'Everywhere',
    keys: 'Ctrl+S / Shift+Ctrl+S',
    does: 'Save / Save as',
    chords: [
      { chord: { key: 's', code: 'KeyS', mod: true, shift: false }, action: 'file.save' },
      { chord: { key: 's', code: 'KeyS', mod: true, shift: true }, action: 'file.saveAs' },
    ],
  },
  {
    group: 'Everywhere',
    keys: 'Ctrl+W',
    does: 'Close the window (asks about unsaved changes)',
    chords: [{ chord: mod('w', 'KeyW'), action: 'file.close' }],
  },
  { group: 'Everywhere', keys: 'Tab / Shift+Tab', does: 'Move keyboard focus between controls' },
  {
    group: 'Everywhere',
    keys: 'Enter / Space',
    does: 'Activate the focused button, switch or chip',
  },

  {
    group: '3D view',
    sub: 'every workspace',
    keys: '@Left-drag',
    does: 'Orbit (Orbit mode) or pan (Pan mode)',
  },
  { group: '3D view', keys: 'Shift+@Left-drag', does: 'The other of orbit / pan' },
  { group: '3D view', keys: '@Middle-drag', does: 'Pan' },
  { group: '3D view', keys: '@Wheel', does: 'Zoom' },
  {
    group: '3D view',
    keys: 'O / P',
    does: 'Orbit mode / Pan mode',
    chords: [
      { chord: { key: 'o' }, action: 'view.orbit' },
      { chord: { key: 'p' }, action: 'view.pan' },
    ],
  },
  {
    group: '3D view',
    keys: 'Home',
    does: 'Home view — default orientation, framed',
    chords: [{ chord: { key: 'Home' }, action: 'home' }],
  },
  {
    group: '3D view',
    keys: 'F',
    does: 'Fit the shape to the view, orientation kept',
    chords: [{ chord: { key: 'f' }, action: 'view.fit' }],
  },
  { group: '3D view', keys: '@Click view-cube face, edge or corner', does: 'Turn to that view' },
  { group: '3D view', keys: '@Double-click a view-cube face', does: 'Upright view of that face' },
  {
    group: '3D view',
    keys: '@Point at the top of the view / Tab / O / P / E / F',
    does: 'Minimal density: show the auto-hidden toolbar',
  },

  {
    group: 'Shapes',
    sub: 'drawing',
    keys: '1 / 2 / 3 / 4',
    does: 'Tool: Fixed / Variable / Erase / Paint',
    chords: [
      { chord: { key: '1', code: 'Digit1' }, action: 'tool.fixed' },
      { chord: { key: '2', code: 'Digit2' }, action: 'tool.variable' },
      { chord: { key: '3', code: 'Digit3' }, action: 'tool.erase' },
      { chord: { key: '4', code: 'Digit4' }, action: 'tool.paint' },
    ],
  },
  {
    group: 'Shapes',
    keys: 'E',
    does: '3D edit mode (toggle) — a plain click uses the active tool',
    chords: [{ chord: { key: 'e' }, action: 'edit3d' }],
  },
  {
    group: 'Shapes',
    keys: 'Shift+@Click',
    does: 'Quick tool 1 — fixed voxel (3D view and 2D grid)',
  },
  { group: 'Shapes', keys: 'Alt+@Click', does: 'Quick tool 2 — variable voxel (⌘ on macOS)' },
  { group: 'Shapes', keys: 'Ctrl+@Click', does: 'Quick tool 3 — erase voxel' },
  {
    group: 'Shapes',
    keys: '@Right-click / @Right-drag',
    does: '2D grid: erase, whatever tool is active',
  },
  {
    group: 'Shapes',
    keys: '@Drag across cells',
    does: '2D grid: apply the tool to every cell entered (one undo step)',
  },
  {
    group: 'Shapes',
    keys: 'PgUp / PgDn',
    does: 'Next / previous layer',
    chords: [
      { chord: { key: 'PageUp' }, action: 'page.up' },
      { chord: { key: 'PageDown' }, action: 'page.down' },
    ],
  },

  {
    group: 'Shapes',
    sub: 'shapes list',
    keys: '↑ / ↓',
    does: 'Previous / next shape (list focused)',
    chords: [
      { chord: { key: 'ArrowUp' }, action: 'prev' },
      { chord: { key: 'ArrowDown' }, action: 'next' },
    ],
  },
  {
    group: 'Shapes',
    sub: 'shapes list',
    keys: 'Alt+↑ / Alt+↓',
    does: 'Move the shape earlier / later — shapes renumber',
    chords: [
      { chord: { key: 'ArrowUp', alt: true }, action: 'shape.earlier' },
      { chord: { key: 'ArrowDown', alt: true }, action: 'shape.later' },
    ],
  },
  {
    group: 'Shapes',
    sub: 'shapes list',
    keys: 'F2 / Enter / @Double-click',
    does: 'Rename the shape label; Enter commits, Esc cancels',
    chords: [
      { chord: { key: 'F2' }, action: 'shape.rename' },
      { chord: { key: 'Enter' }, action: 'activate' },
    ],
  },
  {
    group: 'Shapes',
    sub: 'shapes list',
    keys: 'Del',
    does: 'Delete the shape',
    chords: [{ chord: { key: 'Delete' }, action: 'delete' }],
  },
  {
    group: 'Shapes',
    sub: 'shapes list',
    keys: 'Ctrl+D',
    does: 'Duplicate the shape',
    chords: [{ chord: mod('d', 'KeyD'), action: 'shape.duplicate' }],
  },
  { group: 'Shapes', sub: 'shapes list', keys: '@Right-click a row', does: 'Shape menu' },
  { group: 'Shapes', sub: 'shapes list', keys: '@Drag a row', does: 'Reorder' },

  {
    group: 'Puzzle',
    keys: '+ / −',
    does: 'One more / one fewer copy of the selected piece',
    // = and _ too (the same keys without and with Shift), as the mockup took them
    chords: [
      { chord: { key: '+', code: 'Equal' }, action: 'piece.more' },
      { chord: { key: '-', code: 'Minus' }, action: 'piece.fewer' },
    ],
  },
  // Del is the action `delete` of the Entities row: the Puzzle workspace says what it does here
  { group: 'Puzzle', keys: 'Del', does: 'Remove the selected piece from the puzzle' },
  { group: 'Puzzle', keys: '@Double-click a chip / +', does: 'Add one copy from All pieces' },
  {
    group: 'Puzzle',
    keys: '@Drag a chip in / @Drag a tile out',
    does: 'Add a piece / remove it from the puzzle',
  },
  {
    group: 'Puzzle',
    keys: '@Right-click a tile or chip',
    does: 'Piece menu — one more, one fewer, remove, set as result',
  },
  {
    group: 'Puzzle',
    keys: '@Click a piece in the 3D view',
    does: 'Select it (the view rotates each piece in place)',
  },

  // the Solver's keys: Enter, ↑ ↓, Home, PgUp PgDn and Del are the actions of the rows above
  {
    group: 'Solver',
    sub: 'focus on the solutions list, solution slider or move seekbar',
    keys: 'Space / Enter',
    does: 'Play / pause the disassembly',
    chords: [{ chord: { key: ' ', code: 'Space' }, action: 'activate' }],
  },
  {
    group: 'Solver',
    keys: '← / →',
    does: 'One move back / forward',
    chords: [
      { chord: { key: 'ArrowLeft', shift: false }, action: 'move.back' },
      { chord: { key: 'ArrowRight', shift: false }, action: 'move.forward' },
    ],
  },
  {
    group: 'Solver',
    keys: 'Shift+← / Shift+→',
    does: 'Jump to assembled / fully disassembled',
    chords: [
      { chord: { key: 'ArrowLeft', shift: true }, action: 'move.start' },
      { chord: { key: 'ArrowRight', shift: true }, action: 'move.end' },
    ],
  },
  { group: 'Solver', keys: '↑ / ↓', does: 'Previous / next solution' },
  {
    group: 'Solver',
    keys: 'Home / End',
    does: 'First / last solution (on the seekbar: start / end of the moves)',
    chords: [{ chord: { key: 'End' }, action: 'end' }],
  },
  { group: 'Solver', keys: 'PgUp / PgDn', does: '8 solutions back / forward' },
  { group: 'Solver', keys: 'Del', does: 'Remove the selected solution (list; asks first)' },
  { group: 'Solver', keys: 'Esc', does: 'Pause playback' },
  { group: 'Solver', keys: '@Double-click a row', does: 'Select and play' },
  { group: 'Solver', keys: '@Click a column header', does: 'Sort; click again to reverse' },
  {
    group: 'Solver',
    keys: 'Space / Enter · ← / →',
    does: 'Piece chip: cycle solid → wireframe → hidden · move between chips',
  },
  {
    group: 'Solver',
    keys: '@Orbit or click in the 3D view',
    does: 'Keyboard focus stays in the Solutions card',
  },

  // the explorers' keys: the actions of the rows above, while the explorer's drawer has the focus
  {
    group: 'Explorers',
    sub: 'Solver; focus in the explorer below the 3D view',
    keys: '↑ / ↓ · ← / →',
    does: 'Placements: previous / next placement · previous / next piece',
  },
  { group: 'Explorers', keys: 'Home / End', does: 'Placements: first / last placement' },
  { group: 'Explorers', keys: 'Space / Enter', does: 'Step: one step of the assembler' },
  {
    group: 'Explorers',
    keys: '↑ / ↓ · ← / → · Enter',
    does: 'Movements: previous / next position · back to its parent / its first move · analyse',
  },
  {
    group: 'Explorers',
    keys: '@Alt-drag a piece in the 3D view',
    does: 'Movements: the first valid move that takes the piece that way',
  },
  { group: 'Explorers', keys: 'Esc', does: 'Close the explorer' },

  { group: 'Dialogs and menus', keys: 'Esc', does: 'Close the dialog or menu' },
  { group: 'Dialogs and menus', keys: 'Enter', does: 'Commit an inline rename or a number field' },
  { group: 'Dialogs and menus', keys: 'Space / Enter', does: 'Toggle the focused switch' },
];

/** What a key press is made of (a KeyboardEvent has it). */
export interface KeyPress {
  key: string;
  code: string;
  ctrlKey: boolean;
  metaKey: boolean;
  shiftKey: boolean;
  altKey: boolean;
}

/** Whether a key press is the chord. */
export function matches(chord: Chord, e: KeyPress): boolean {
  const modDown = e.ctrlKey || e.metaKey;
  if ((chord.mod ?? false) !== modDown) {
    return false;
  }
  if ((chord.alt ?? false) !== e.altKey) {
    return false;
  }
  // Shift is free unless the chord names it: some layouts need it for "[" or
  // ",", and the mockup took O as well as o
  if (chord.shift !== undefined && chord.shift !== e.shiftKey) {
    return false;
  }
  return (
    e.key.toLowerCase() === chord.key.toLowerCase() ||
    (chord.code !== undefined && e.code === chord.code)
  );
}

/** The action of a key press, from the table; null when no row has it. */
export function actionOf(e: KeyPress): KeyAction | null {
  for (const row of KEY_MAP) {
    for (const c of row.chords ?? []) {
      if (matches(c.chord, e)) {
        return c.action;
      }
    }
  }
  return null;
}

/** The groups of the map in order, each with its rows (for the help). */
export function keyGroups(): { group: string; sub?: string; rows: KeyRow[] }[] {
  const out: { group: string; sub?: string; rows: KeyRow[] }[] = [];
  for (const row of KEY_MAP) {
    const last = out.at(-1);
    if (last?.group === row.group && (row.sub === undefined || row.sub === last.sub)) {
      last.rows.push(row);
    } else {
      out.push({
        group: row.group,
        ...(row.sub === undefined ? {} : { sub: row.sub }),
        rows: [row],
      });
    }
  }
  return out;
}

/** The actions that act while a text field has the focus (they hold Ctrl or Cmd). */
export const IN_TEXT: ReadonlySet<KeyAction> = new Set<KeyAction>([
  'file.new',
  'file.open',
  'file.save',
  'file.saveAs',
  'file.close',
]);

const ACCEL_KEYS: Readonly<Record<string, string>> = {
  ' ': 'Space',
  ',': ',',
  '[': '[',
  ']': ']',
  '+': 'Plus',
  '-': '-',
};

/**
 * The first chord of an action as Electron writes an accelerator
 * ("Shift+CmdOrCtrl+S", "F1"), for the menus to show; null when the table
 * has none or it cannot be written so.
 */
export function accelerator(action: KeyAction): string | null {
  for (const row of KEY_MAP) {
    for (const c of row.chords ?? []) {
      if (c.action !== action) {
        continue;
      }
      const k = c.chord.key;
      const key = ACCEL_KEYS[k] ?? (k.length === 1 ? k.toUpperCase() : /^F\d+$/.test(k) ? k : null);
      if (key === null) {
        return null;
      }
      return [
        c.chord.shift === true ? 'Shift' : '',
        c.chord.alt === true ? 'Alt' : '',
        c.chord.mod === true ? 'CmdOrCtrl' : '',
        key,
      ]
        .filter((x) => x !== '')
        .join('+');
    }
  }
  return null;
}
