// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Placements dialog of a Tilted Square puzzle (decision 0045, stage G
// of docs/TILTED-SQUARE-PLAN.md): every board the turned grid can give in
// the tray (tray.placements), each with its picture (tray.preview at its
// shift) and its whole cells, and, once Count assemblies has run, how many
// ways a problem's pieces go on it (the job tray.fitPlacements, told by
// tray.fitProgress and tray.fitEnded). Use puts the grid there: one
// tray.set, one undo step. Opened from Tilt & tolerance (Placements…) and
// from the Solver when a solve found no assembly (Try every placement,
// which starts the count at once). The engine does the work; the words are
// state/placements.ts.

import './placements-dialog.css';
import type { PlacedBoard, SolveOptions, Tray } from '../../engine/api.generated.ts';
import { EngineError } from '../../engine/errors.ts';
import type { Store } from '../state/store.ts';
import {
  ROWS_AT_ONCE,
  boardNote,
  fitState,
  fitSummary,
  fitText,
  listText,
  useSettings,
  wholeText,
  type Fit,
} from '../state/placements.ts';
import { trayPicture, unfitIds, unfitNote } from '../state/tilted.ts';
import { readOptions, solveOptions } from '../solver/settings.ts';
import { closeDialog, openDialog } from '../shell/dialog.ts';
import { esc } from '../shell/dom.ts';
import { flash, reportError } from '../shell/status.ts';

/** What the dialog needs of the page. */
export interface PlacementsHost {
  store: Store;
  /** Runs a mutating call and waits for its revision; reports a failure. */
  mutate<T extends { revision: number }>(doing: string, call: () => Promise<T>): Promise<T | null>;
}

/** How the dialog opens. */
export interface PlacementsRequest {
  /** The problem whose pieces are tried; the first problem when left out. */
  problem?: string | null;
  /** The solve's options (the Solver's, as shown); the problem's stored ones when left out. */
  options?: SolveOptions;
  /** Count the assemblies at once (the Solver's Try every placement). */
  start?: boolean;
}

const ID = 'placementsDlg';
const PICTURE_PX = 76;

/** Opens the dialog; resolves when it is closed. */
export async function openPlacements(
  host: PlacementsHost,
  request: PlacementsRequest = {},
): Promise<void> {
  const { store } = host;
  const engine = store.engine;
  const snap = store.snapshot;
  if (snap?.tray == null) {
    return;
  }
  let listed: { boards: PlacedBoard[]; complete: boolean; inUse: number | null };
  try {
    listed = await engine.call('tray.placements');
  } catch (e) {
    reportError(e, 'Placements');
    return;
  }
  const boards = listed.boards;
  const problems = snap.problems;
  // what the dialog holds, changed by its handlers
  const at: {
    problem: string | null;
    fit: Fit | null;
    shown: number;
    open: boolean;
    /** Resolved when the fit under way ends. */
    ended: (() => void) | null;
  } = {
    problem: problems.find((p) => p.id === request.problem)?.id ?? problems[0]?.id ?? null,
    fit: null,
    shown: Math.min(boards.length, ROWS_AT_ONCE),
    open: true,
    ended: null,
  };
  const pictures = new Map<number, Tray>();

  /** Whether a fit is under way (asked each time: its events change it). */
  const counting = (): boolean => at.fit?.active === true;
  const root = (): HTMLElement | null => document.getElementById(ID);
  const part = <T extends HTMLElement = HTMLElement>(name: string): T | null =>
    root()?.querySelector<T>(`[data-plc="${name}"]`) ?? null;

  const rowHtml = (k: number): string => {
    const b = boards[k];
    if (b === undefined) {
      return '';
    }
    const t = pictures.get(k);
    const state = fitState(at.fit, k);
    const note = boardNote(k, listed.inUse);
    const inUse = k === listed.inUse;
    return (
      `<div class="plc-row${inUse ? ' inuse' : ''}" role="listitem" data-k="${String(k)}" data-fit="${state}">` +
      `<div class="plc-pic">${t === undefined ? '' : trayPicture(t, { px: PICTURE_PX, view: 'cells', id: `plcClip${String(k)}`, margin: 2 })}</div>` +
      `<div class="plc-t"><b>${esc(wholeText(b.whole))}</b><small>${esc(note)}</small></div>` +
      `<div class="plc-fit">${esc(fitText(at.fit, k))}</div>` +
      `<button class="btn sm" data-use="${String(k)}" ${inUse ? 'disabled' : ''} title="${inUse ? 'The grid lies here now' : 'Put the grid at this placement (one step, undone with Undo)'}">${inUse ? 'In use' : 'Use'}</button>` +
      '</div>'
    );
  };

  const drawList = (): void => {
    const list = part('list');
    if (list === null) {
      return;
    }
    const rows: string[] = [];
    for (let k = 0; k < at.shown; k++) {
      rows.push(rowHtml(k));
    }
    if (at.shown < boards.length) {
      rows.push(
        `<button class="btn sm plc-more" data-more title="Show the next boards of the list">Show ${String(Math.min(ROWS_AT_ONCE, boards.length - at.shown))} more of ${String(boards.length - at.shown)}</button>`,
      );
    }
    list.innerHTML = rows.join('');
  };

  const drawBar = (): void => {
    const run = part<HTMLButtonElement>('run');
    const sum = part('sum');
    const pick = part<HTMLSelectElement>('problem');
    const active = at.fit?.active === true;
    if (run !== null) {
      run.textContent = active ? 'Stop' : 'Count assemblies';
      run.title = active
        ? 'Stop trying the placements: what was counted stays'
        : 'Solve the problem on every board of the list and count the assemblies. Nothing is changed.';
      run.disabled = at.problem === null;
    }
    if (pick !== null) {
      pick.disabled = active;
    }
    if (sum !== null) {
      sum.textContent = fitSummary(at.fit);
    }
  };

  // the pictures come one after the other, the rows shown first
  let drawing = false;
  const fetchPictures = async (): Promise<void> => {
    if (drawing) {
      return;
    }
    drawing = true;
    try {
      for (let k = 0; at.open && k < at.shown; k++) {
        const b = boards[k];
        if (b === undefined || pictures.has(k)) {
          continue;
        }
        const t = await engine.call('tray.preview', {
          tray: { placement: 'custom', shiftX: b.shiftX, shiftY: b.shiftY },
        });
        pictures.set(k, t);
        const pic = root()?.querySelector<HTMLElement>(`.plc-row[data-k="${String(k)}"] .plc-pic`);
        if (pic != null) {
          pic.innerHTML = trayPicture(t, {
            px: PICTURE_PX,
            view: 'cells',
            id: `plcClip${String(k)}`,
            margin: 2,
          });
        }
      }
    } catch (e) {
      reportError(e, 'Placements');
    } finally {
      drawing = false;
    }
  };

  const optionsOf = async (id: string): Promise<SolveOptions> => {
    if (request.options !== undefined && id === request.problem) {
      return request.options;
    }
    // as the Solver would solve it: the problem's stored settings
    const d = await engine.call('problem.get', { problem: id });
    return solveOptions(readOptions(d.solverSettings, undefined, 'brick', false), {
      keepAtMost: 0,
      keepEach: 1,
    });
  };

  const count = async (): Promise<void> => {
    if (at.problem === null || at.fit?.active === true) {
      return;
    }
    const id = at.problem;
    const sum = part('sum');
    try {
      const options = await optionsOf(id);
      const { total } = await engine.call('tray.fitPlacements', { problem: id, options });
      at.fit = {
        active: true,
        complete: listed.complete,
        total,
        done: 0,
        current: 0,
        stopped: false,
        failure: null,
        boards: boards.map((b) => ({ ...b, counted: false, assemblies: 0 })),
      };
      drawBar();
      drawList();
    } catch (e) {
      // said in the dialog: the status bar is under it
      if (sum !== null) {
        sum.textContent =
          e instanceof EngineError
            ? e.errc === 'invalid_state'
              ? 'The problem is being solved: pause it first'
              : `${e.detail.charAt(0).toUpperCase()}${e.detail.slice(1)}`
            : String(e);
      }
    }
  };

  const stop = async (): Promise<void> => {
    if (at.problem === null || !counting()) {
      return;
    }
    const over = new Promise<void>((done) => {
      at.ended = done;
    });
    try {
      await engine.call('tray.fitPlacementsStop', { problem: at.problem });
    } catch {
      // it ended by itself meanwhile: its last event has come or comes
    }
    // its end may have been told while the call was answered
    if (counting()) {
      await over;
    }
  };

  const use = async (k: number): Promise<void> => {
    const b = boards[k];
    if (b === undefined) {
      return;
    }
    await stop();
    // the end of the fit is a revision: the page shows it before the change
    const unfit = unfitIds(store.snapshot?.shapes ?? []);
    const done = await host.mutate('Placement', () =>
      engine.call('tray.set', { tray: useSettings(b, k) }),
    );
    if (done === null) {
      return;
    }
    closeDialog(ID);
    const note = unfitNote(unfit, store.snapshot?.shapes ?? []);
    flash(note ?? `The grid lies at the placement chosen: ${wholeText(b.whole)}`, 6000);
  };

  const told = (p: { problem: string } & Omit<Fit, 'active'>, active: boolean): void => {
    if (!at.open || p.problem !== at.problem || at.fit === null) {
      return;
    }
    at.fit = { ...p, active };
    if (!active) {
      at.ended?.();
      at.ended = null;
    }
    drawBar();
    // the rows' words alone: the pictures stay
    for (let k = 0; k < at.shown; k++) {
      const row = root()?.querySelector<HTMLElement>(`.plc-row[data-k="${String(k)}"]`);
      const cell = row?.querySelector<HTMLElement>('.plc-fit');
      if (row != null && cell != null) {
        row.dataset.fit = fitState(at.fit, k);
        cell.textContent = fitText(at.fit, k);
      }
    }
    if (!active) {
      // the first board that takes the pieces is brought into sight
      root()?.querySelector('.plc-row[data-fit="fits"]')?.scrollIntoView({ block: 'nearest' });
    }
  };
  const offProgress = engine.on('tray.fitProgress', (p) => {
    told(p, true);
  });
  const offEnded = engine.on('tray.fitEnded', (p) => {
    told(p, false);
  });

  const options = problems
    .map(
      (p) =>
        `<option value="${esc(p.id)}" ${p.id === at.problem ? 'selected' : ''}>${esc(p.name === '' ? p.label : `${p.label} · ${p.name}`)}</option>`,
    )
    .join('');
  const closed = openDialog({
    id: ID,
    title: 'Placements',
    wide: true,
    body:
      `<p>${esc(listText(boards.length, listed.complete))}</p>` +
      '<div class="plc-bar">' +
      `<label class="plc-prob"${problems.length > 1 ? '' : ' hidden'}>Pieces of <select data-plc="problem" title="The problem whose pieces are tried on each board">${options}</select></label>` +
      '<button class="btn sm pri" data-plc="run"></button>' +
      '<span class="plc-sum" data-plc="sum" role="status"></span>' +
      '</div>' +
      '<div class="plc-list" data-plc="list" role="list"></div>',
    buttons: [{ key: 'close', label: 'Close', default: true }],
  });
  drawBar();
  drawList();
  void fetchPictures();
  const el = root();
  el?.addEventListener('click', (e) => {
    const t = e.target as Element;
    const useBtn = t.closest<HTMLButtonElement>('button[data-use]');
    if (useBtn !== null && !useBtn.disabled) {
      void use(Number(useBtn.dataset.use));
    } else if (t.closest('[data-more]') !== null) {
      at.shown = Math.min(boards.length, at.shown + ROWS_AT_ONCE);
      drawList();
      void fetchPictures();
    } else if (t.closest('[data-plc="run"]') !== null) {
      void (at.fit?.active === true ? stop() : count());
    }
  });
  el?.addEventListener('change', (e) => {
    const pick = (e.target as Element).closest<HTMLSelectElement>('[data-plc="problem"]');
    if (pick !== null && at.fit?.active !== true) {
      at.problem = pick.value;
      at.fit = null;
      drawBar();
      drawList();
    }
  });
  if (request.start === true) {
    void count();
  }
  await closed;
  at.open = false;
  offProgress();
  offEnded();
  // closed while counting: the fit is stopped (its end is not waited for)
  if (at.fit?.active === true && at.problem !== null) {
    engine.call('tray.fitPlacementsStop', { problem: at.problem }).catch(() => undefined);
  }
}
