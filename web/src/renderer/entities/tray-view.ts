// SPDX-License-Identifier: GPL-3.0-or-later
//
// A Tilted Square puzzle's board in the Entities workspace's grid area
// (decision 0045), in place of the voxel grid: the tray in its own frame,
// the turned cells it gives (whole in color, cut in gray) with its straight
// squares over them, seen two ways (the switch above it): Tray brings the
// squares out, Cells the turned cells. In either a click or a drag over
// squares makes walls (or, starting on a wall, opens squares); a drag is
// one tray.setWalls, so one undo step. The picture is drawn from what the
// engine says of the tray (state/tilted.ts).

import type { Tray } from '../../engine/api.generated.ts';
import type { Store } from '../state/store.ts';
import { byId } from '../shell/dom.ts';
import { squareOf, trayLine, trayPicture } from '../state/tilted.ts';
import type { TrayView } from '../state/tilted.ts';

/** What the view asks of the workspace. */
export interface TrayViewHost {
  store: Store;
  /** Whether editing is refused now (a solve runs); says so when it is. */
  refuse(): boolean;
  /** Runs a mutating call and waits for its revision; reports a failure. */
  mutate<T extends { revision: number }>(doing: string, call: () => Promise<T>): Promise<T | null>;
}

const VIEWS: readonly { view: TrayView; label: string; tip: string }[] = [
  {
    view: 'tray',
    label: 'Tray',
    tip: 'The tray’s straight squares brought out over the turned cells: click or drag to make walls',
  },
  {
    view: 'cells',
    label: 'Cells',
    tip: 'The turned cells the tray gives brought out (whole in color, cut by the edge or a wall in gray): click or drag over squares to make walls',
  },
];

export class TrayViewCard {
  private readonly host: TrayViewHost;
  /** The view chosen; kept while the app runs. */
  view: TrayView = 'cells';
  private shown: Tray | null = null;
  /** A drag under way: wall or open, and the squares it went over. */
  private drag: { wall: boolean; squares: Map<string, [number, number]> } | null = null;
  private drawnFor = '';

  constructor(host: TrayViewHost) {
    this.host = host;
    this.install();
    new ResizeObserver(() => {
      this.drawnFor = '';
      this.draw();
    }).observe(byId('trayArea'));
  }

  /** Shows a tray (null: the view is not in use). */
  show(tray: Tray | null): void {
    this.shown = tray;
    byId('boardViews').innerHTML =
      tray === null
        ? ''
        : `<div class="pseg bv" role="radiogroup" aria-label="Board view">${VIEWS.map(
            (v) =>
              `<button class="gbtn ${this.view === v.view ? 'on' : ''}" data-bv="${v.view}" role="radio" aria-checked="${String(this.view === v.view)}" title="${v.tip}"><span>${v.label}</span></button>`,
          ).join('')}</div><span class="mute bvline">${trayLine(tray)}</span>`;
    this.draw();
  }

  private draw(): void {
    const area = byId('trayArea');
    const t = this.shown;
    if (t === null) {
      area.innerHTML = '';
      this.drawnFor = '';
      return;
    }
    const room = Math.min(area.clientWidth, area.clientHeight || area.clientWidth);
    const px = Math.max(80, room - 12);
    const key = `${this.view}:${String(px)}:${t.cells}:${JSON.stringify(t.walls)}:${JSON.stringify(t.corners)}:${String(t.size)}`;
    if (key === this.drawnFor && this.drag === null) {
      return;
    }
    this.drawnFor = key;
    area.innerHTML = trayPicture(t, {
      px,
      view: this.view,
      margin: 6,
      id: 'trayClip',
      editable: true,
    });
    area.title = 'Click or drag over squares to make walls; start on a wall to open squares again';
  }

  // the square under a point of the page, when it is one of the picture's
  private squareAt(x: number, y: number): [number, number] | null {
    const el = document.elementFromPoint(x, y)?.closest<SVGElement>('[data-sq]') ?? null;
    return el === null || !byId('trayArea').contains(el) ? null : squareOf(el.dataset.sq);
  }

  private mark(sq: [number, number]): void {
    if (this.drag === null) {
      return;
    }
    const key = `${String(sq[0])},${String(sq[1])}`;
    if (!this.drag.squares.has(key)) {
      this.drag.squares.set(key, sq);
      byId('trayArea')
        .querySelector(`[data-sq="${key}"]`)
        ?.classList.add(this.drag.wall ? 'pending' : 'opening');
    }
  }

  private async finish(): Promise<void> {
    const d = this.drag;
    this.drag = null;
    if (d === null || d.squares.size === 0) {
      return;
    }
    await this.host.mutate(d.wall ? 'Walls' : 'Open squares', () =>
      this.host.store.engine.call('tray.setWalls', {
        squares: [...d.squares.values()],
        wall: d.wall,
      }),
    );
    this.drawnFor = '';
    this.draw();
  }

  private install(): void {
    byId('boardViews').addEventListener('click', (e) => {
      const b = (e.target as Element).closest<HTMLElement>('[data-bv]');
      if (b !== null) {
        this.view = b.dataset.bv as TrayView;
        this.show(this.shown);
      }
    });
    const area = byId('trayArea');
    area.addEventListener('pointerdown', (e) => {
      const t = this.shown;
      if (t === null || e.button !== 0) {
        return;
      }
      const sq = this.squareAt(e.clientX, e.clientY);
      if (sq === null || this.host.refuse()) {
        return;
      }
      e.preventDefault();
      area.setPointerCapture(e.pointerId);
      // a drag that starts on a wall opens squares; elsewhere it makes walls
      const onWall = t.walls.some(([x, y]) => x === sq[0] && y === sq[1]);
      this.drag = { wall: !onWall, squares: new Map() };
      this.mark(sq);
    });
    area.addEventListener('pointermove', (e) => {
      if (this.drag !== null) {
        const sq = this.squareAt(e.clientX, e.clientY);
        if (sq !== null) {
          this.mark(sq);
        }
      }
    });
    const end = (): void => {
      void this.finish();
    };
    area.addEventListener('pointerup', end);
    area.addEventListener('pointercancel', end);
  }
}
