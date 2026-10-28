// SPDX-License-Identifier: GPL-3.0-or-later
//
// The left card's "Edit shape" part: Transform (Flip · Rotate · Nudge),
// Fit & scale, Repair & surface. The buttons are the engine's
// (`shape.taskSet`: each grid's tasks in the original's order, with its
// group, its tooltip and whether it can go to every shape), placed as the
// mockup draws each grid's set (the matrix of the cube, rhombic and
// tetrahedra-octahedra grids; the sphere and prism grids' rotations and
// nudge pads); a task the layout does not place gets a plain button of its
// group, so nothing the engine offers is lost. Run through `shape.task`;
// the counts and a blocked nudge as flash messages. From
// docs/mockup/index.html: `renderInsp`, `renderTransform` (`tb`, `fr`,
// `pad`), `abtnSet`, the .insp click handler and its messages (`remap`'s
// "Blocked", `doFill`'s "Filled n interior voxels", the surface's "n inner
// voxels updated", `doMin`'s "Already at minimum resolution").

import type { Grid, Methods, ShapeTask } from '../../engine/api.generated.ts';
import { isEngineError } from '../../engine/errors.ts';
import { meshArrays } from '../../engine/frames.ts';
import type { MeshArrays } from '../../engine/frames.ts';
import type { Store } from '../state/store.ts';
import { $$, byId, esc } from '../shell/dom.ts';
import { ic } from '../shell/icons.ts';
import { flash, reportError } from '../shell/status.ts';
import type { Axis, EditorState } from './editor-state.ts';
import { G } from './glyphs.ts';
import type { SurfOp } from './glyphs.ts';
import { chipColours } from './shapes-list.ts';

/** A task of the set: what the engine says of it. */
export type TaskInfo = Methods['shape.taskSet']['result']['tasks'][number];

/** What the inspector asks of the workspace. */
export interface InspectorHost {
  store: Store;
  state: EditorState;
  refuse(): boolean;
  /** The document's task set (null until loaded). */
  tasks(): readonly TaskInfo[] | null;
  /** "Apply to all shapes" changed. */
  applyAllChanged(): void;
  /** Counts a call as an edit under way until it settles. */
  track<T>(p: Promise<T>): Promise<T>;
  /** Shows the shape as a button would leave it, over the shape in the 3D view (null: none). */
  showGhost(arrays: MeshArrays | null): void;
}

// where each grid draws its transform tasks (the mockup's renderTransform)
type Row = {
  a: Axis;
  flip: ShapeTask;
  rot: [ShapeTask, string, number][];
  nudge?: [ShapeTask, ShapeTask];
};
const MATRIX: Row[] = [
  {
    a: 'x',
    flip: 'flip12',
    rot: [
      ['rotate6', '−90°', -1],
      ['rotate7', '+90°', 1],
    ],
    nudge: ['nudge1', 'nudge0'],
  },
  {
    a: 'y',
    flip: 'flip13',
    rot: [
      ['rotate9', '−90°', -1],
      ['rotate8', '+90°', 1],
    ],
    nudge: ['nudge3', 'nudge2'],
  },
  {
    a: 'z',
    flip: 'flip14',
    rot: [
      ['rotate10', '−90°', -1],
      ['rotate11', '+90°', 1],
    ],
    nudge: ['nudge5', 'nudge4'],
  },
];
const PRISM_ROWS: Row[] = [
  { a: 'x', flip: 'flip12', rot: [['rotate6', '180°', 1]] },
  { a: 'y', flip: 'flip13', rot: [['rotate9', '180°', 1]] },
  {
    a: 'z',
    flip: 'flip14',
    rot: [
      ['rotate11', '−60°', -1],
      ['rotate10', '+60°', 1],
    ],
  },
];
type Pad = [label: string, arrows: [string, ShapeTask][], cls: string];
const SPHERE_PADS: Pad[] = [
  [
    'UP LAYER',
    [
      ['→', 'nudge0'],
      ['↑', 'nudge1'],
      ['←', 'nudge2'],
      ['↓', 'nudge3'],
    ],
    '',
  ],
  [
    'IN LAYER',
    [
      ['↗', 'nudge4'],
      ['↖', 'nudge5'],
      ['↙', 'nudge27'],
      ['↘', 'nudge28'],
    ],
    '',
  ],
  [
    'DOWN LAYER',
    [
      ['→', 'nudge29'],
      ['↑', 'nudge30'],
      ['←', 'nudge31'],
      ['↓', 'nudge32'],
    ],
    '',
  ],
];
const PRISM_PADS: Pad[] = [
  [
    'IN PLANE',
    [
      ['↖', 'nudge1'],
      ['↗', 'nudge0'],
    ],
    'two6',
  ],
  [
    '',
    [
      ['←', 'nudge3'],
      ['→', 'nudge2'],
    ],
    'two6',
  ],
  [
    '',
    [
      ['↙', 'nudge28'],
      ['↘', 'nudge27'],
    ],
    'two6',
  ],
  [
    'Z',
    [
      ['↓', 'nudge5'],
      ['↑', 'nudge4'],
    ],
    'two6',
  ],
];

// the Fit and Repair buttons: the mockup's picture and words for each task
const ABTN: Partial<Record<ShapeTask, { g: () => string; t: string; sm: string; short: string }>> =
  {
    prune: {
      g: () => G.prune(30),
      t: 'Prune grid to fit piece',
      sm: 'Shrink the grid to the piece’s bounding box',
      short: 'Prune',
    },
    centre: { g: () => G.center(30), t: 'Center', sm: 'Centre on the grid', short: 'Center' },
    toOrigin: { g: () => G.origin(30), t: 'To origin', sm: 'Corner at 0, 0, 0', short: 'Origin' },
    minimize: {
      g: () => G.div(2, 30),
      t: 'Minimize',
      sm: 'Reduce resolution, same shape',
      short: 'Minimize',
    },
    fillHoles: {
      g: () => G.fill(30),
      t: 'Fill interior holes',
      sm: 'Fill empty voxels not visible from outside with fixed voxels',
      short: 'Fill holes',
    },
    gridScale: {
      g: () => G.gridScale(30),
      t: 'Grid scale',
      sm: 'Scale up, keeping the surface and each voxel’s edges',
      short: 'Grid scale',
    },
  };
const SURF: [SurfOp, string, string, ShapeTask, ShapeTask][] = [
  ['fixed', 'Make fixed', 'solid', 'insideFixed', 'outsideFixed'],
  ['variable', 'Make variable', 'optional', 'insideVariable', 'outsideVariable'],
  ['clear', 'Clear color', 'remove paint', 'insideDecolour', 'outsideDecolour'],
];

function layoutOf(grid: Grid): 'matrix' | 'spheres' | 'prisms' {
  return grid === 'spheres' ? 'spheres' : grid === 'prisms' ? 'prisms' : 'matrix';
}

export class Inspector {
  private readonly host: InspectorHost;
  private placed = new Set<ShapeTask>();
  /** The Transform button the pointer is on, and the newest preview asked for. */
  private hovered: ShapeTask | null = null;
  private ghostToken = 0;

  constructor(host: InspectorHost) {
    this.host = host;
    this.install();
  }

  /** Draws the inspector for the selected shape and the document's task set. */
  render(): void {
    const store = this.host.store;
    const shape = store.shape;
    const head = byId('inspHead');
    const tasks = this.host.tasks();
    const grid = store.snapshot?.grid ?? 'bricks';
    if (shape === null || tasks === null) {
      head.innerHTML = '';
      byId('tmat').innerHTML = '';
      byId('fitBody').innerHTML = '';
      byId('toolsBody').innerHTML = '';
      return;
    }
    const { bg, fg } = chipColours(shape, store.indexOf(shape.id));
    head.innerHTML = `<span class="chip" style="background:${bg};color:${fg}">${esc(shape.label)}</span><span>Edit shape</span>`;
    const byTask = new Map(tasks.map((t) => [t.task, t]));
    this.placed = new Set();
    byId('tmat').innerHTML = this.transformHtml(grid, byTask);
    byId('fitBody').innerHTML = this.fitHtml(byTask, shape.fixed + shape.variable === 0);
    byId('toolsBody').innerHTML = this.toolsHtml(byTask);
    // the buttons were drawn again under the pointer (a click changed the
    // shape): the preview goes on with what the same button would do now
    const under = byId('tmat').querySelector<HTMLButtonElement>('button[data-task]:hover');
    this.preview((under?.dataset.task as ShapeTask | undefined) ?? null);
  }

  /** Previews a Transform button's task in the 3D view (null: ends the preview). */
  private preview(task: ShapeTask | null): void {
    const shape = this.host.store.selected;
    const token = ++this.ghostToken;
    this.hovered = task;
    if (task === null || shape === null) {
      this.host.showGhost(null);
      return;
    }
    this.host.store.engine
      .call('shape.taskPreview', { shape, task }, { coalesce: 'shape.taskPreview' })
      .then(
        (r) => {
          if (token === this.ghostToken) {
            this.host.showGhost(meshArrays(r));
          }
        },
        () => {
          // a preview is advisory: a nudge the grid refuses shows nothing
          if (token === this.ghostToken) {
            this.host.showGhost(null);
          }
        },
      );
  }

  private btn(t: TaskInfo | undefined, glyph: string, cap: string): string {
    if (t === undefined) {
      return '<span></span>';
    }
    this.placed.add(t.task);
    return `<button class="gbtn" data-task="${t.task}" title="${esc(t.label)}">${glyph}<span>${cap}</span></button>`;
  }

  // a task no layout places: a plain button with the engine's words
  private extra(byTask: Map<ShapeTask, TaskInfo>, groups: string[]): string {
    const rest = [...byTask.values()].filter(
      (t) => groups.includes(t.group) && !this.placed.has(t.task),
    );
    for (const t of rest) {
      this.placed.add(t.task);
    }
    return rest.length === 0
      ? ''
      : `<div class="xtasks">${rest.map((t) => `<button class="btn sm" data-task="${t.task}" title="${esc(t.label)}">${esc(t.label)}</button>`).join('')}</div>`;
  }

  private transformHtml(grid: Grid, byTask: Map<ShapeTask, TaskInfo>): string {
    const kind = layoutOf(grid);
    if (kind === 'matrix') {
      let h =
        '<div class="tmat"><span></span><span class="gh" style="grid-column:span 1">Flip</span><span class="gh">Rotate</span><span class="gh">Nudge</span>';
      for (const r of MATRIX) {
        const A = r.a.toUpperCase();
        h +=
          `<span class="al ax-${r.a}">${A}</span>` +
          this.btn(byTask.get(r.flip), G.flip(r.a, 0, 28), 'Flip');
        for (const [task, cap, sg] of r.rot) {
          h += this.btn(byTask.get(task), G.rotate(r.a, sg, 28), cap);
        }
        const [m, p] = r.nudge ?? ['nudge1', 'nudge0'];
        h +=
          this.btn(byTask.get(m), G.nudge(r.a, -1, 28), '−1') +
          this.btn(byTask.get(p), G.nudge(r.a, 1, 28), '+1');
      }
      return h + '</div>' + this.extra(byTask, ['Flip', 'Rotate', 'Nudge']);
    }
    const rows = kind === 'spheres' ? MATRIX : PRISM_ROWS;
    let h =
      '<div class="tmat" style="grid-template-columns:22px repeat(3,1fr)"><span></span><span class="gh" style="grid-column:span 1">Flip</span><span class="gh" style="grid-column:span 2">Rotate</span>';
    for (const r of rows) {
      const A = r.a.toUpperCase();
      h +=
        `<span class="al ax-${r.a}">${A}</span>` +
        this.btn(byTask.get(r.flip), G.flip(r.a, 0, 28), 'Flip');
      for (const [task, cap, sg] of r.rot) {
        h += this.btn(byTask.get(task), G.rotate(r.a, sg, 28), cap);
      }
      if (r.rot.length < 2) {
        h += '<span></span>';
      }
    }
    h += '</div><div class="sub2">Nudge</div>';
    for (const [label, arrows, cls] of kind === 'spheres' ? SPHERE_PADS : PRISM_PADS) {
      h += `<div class="npad ${cls}"><span class="rl">${label}</span>`;
      for (const [ch, task] of arrows) {
        const t = byTask.get(task);
        if (t === undefined) {
          h += '<span></span>';
        } else {
          this.placed.add(task);
          h += `<button class="nb" data-task="${task}" title="${esc(t.label)}">${ch}</button>`;
        }
      }
      h += '</div>';
    }
    return h + this.extra(byTask, ['Flip', 'Rotate', 'Nudge']);
  }

  private abtn(t: TaskInfo | undefined, disabled = false): string {
    const a = t === undefined ? undefined : ABTN[t.task];
    if (t === undefined || a === undefined) {
      return '';
    }
    this.placed.add(t.task);
    // the description is in the tooltip too: Compact and Minimal hide it on the button
    return `<button class="abtn" data-task="${t.task}" title="${esc(t.label)} — ${esc(a.sm)}" ${disabled ? 'disabled' : ''}>${a.g()}<span><b>${esc(a.t)}</b><small>${esc(a.sm)}</small><em class="sh">${esc(a.short)}</em></span></button>`;
  }

  private fitHtml(byTask: Map<ShapeTask, TaskInfo>, empty: boolean): string {
    const st = this.host.state;
    let h = `<div class="switch ${st.applyAll ? 'on' : ''}" data-applyall="1" role="switch" tabindex="0" aria-checked="${st.applyAll}" title="Prune, centre, to origin, minimize and scale every shape at once; grid size changes grow the other shapes as much (they never shrink)"><span class="tr"></span><span>Apply to all shapes</span></div>`;
    h += '<div class="sub2">Fit to grid</div>' + this.abtn(byTask.get('prune'), empty);
    h += `<div class="two" style="margin-top:6px">${this.abtn(byTask.get('centre'))}${this.abtn(byTask.get('toOrigin'))}</div>`;
    const mul = (['double', 'triple'] as const)
      .map((task, k) => {
        const t = byTask.get(task);
        if (t === undefined) {
          return '';
        }
        this.placed.add(task);
        const n = k + 2;
        return `<button class="gbtn" data-task="${task}" title="${esc(t.label)} — every voxel ${n}× per axis">${G.mul(n, 32)}<span>${n === 2 ? 'Double' : 'Triple'} ×${n}</span></button>`;
      })
      .join('');
    const min = this.abtn(byTask.get('minimize'));
    if (min !== '' || mul !== '') {
      h += `<div id="scaleBlock"><div class="sub2">Scale</div>${min}<div class="two" id="mulRow" style="margin-top:6px">${mul}</div></div>`;
    }
    return h + this.extra(byTask, ['Grid', 'Shape']);
  }

  private toolsHtml(byTask: Map<ShapeTask, TaskInfo>): string {
    let h = this.abtn(byTask.get('fillHoles'));
    const gs = this.abtn(byTask.get('gridScale'));
    if (gs !== '') {
      h += `<div style="margin-top:6px">${gs}</div>`;
    }
    const rows = SURF.filter(([, , , i, o]) => byTask.has(i) || byTask.has(o));
    if (rows.length > 0) {
      h += '<div class="sub2">Surface voxels</div><div class="surf" id="surf">';
      for (const [op, t, sm, inner, outer] of rows) {
        h += `<span class="sl"><b>${t}</b><small>${sm}</small></span>`;
        for (const [task, part] of [
          [inner, 'inner'],
          [outer, 'outer'],
        ] as const) {
          const ti = byTask.get(task);
          if (ti === undefined) {
            h += '<span></span>';
            continue;
          }
          this.placed.add(task);
          h += `<button class="gbtn" data-task="${task}" title="${esc(ti.label)} — ${part === 'inner' ? 'voxels not visible from outside' : 'voxels on the outer surface'}">${G.surf(op, part, 30)}<span>${part === 'inner' ? 'Inner' : 'Outer'}</span></button>`;
        }
      }
      h += '</div>';
    }
    return h + this.extra(byTask, ['Constrain', 'Tools']);
  }

  /** Runs a task on the selected shape (and every shape, when asked and allowed). */
  async run(task: ShapeTask): Promise<void> {
    const store = this.host.store;
    const shape = store.selected;
    const info = this.host.tasks()?.find((t) => t.task === task);
    if (shape === null || info === undefined || this.host.refuse()) {
      return;
    }
    const toAll = this.host.state.applyAll && info.toAll;
    try {
      const r = await this.host.track(
        store.engine
          .call('shape.task', { shape, task, ...(toAll ? { toAll } : {}) })
          .then(async (r) => {
            await store.whenRevision(r.revision);
            return r;
          }),
      );
      const n = r.cellsChanged;
      const s = n === 1 ? '' : 's';
      if (task === 'fillHoles') {
        flash(n > 0 ? `Filled ${n} interior voxel${s}` : 'No interior holes found');
      } else if (info.group === 'Constrain') {
        flash(`${n} ${task.startsWith('inside') ? 'inner' : 'outer'} voxel${s} updated`);
      } else if (task === 'minimize' && !r.changed) {
        flash('Already at minimum resolution');
      } else if (toAll) {
        flash(`${info.label}: every shape`);
      }
    } catch (e) {
      if (isEngineError(e, 'invalid_argument') && info.group === 'Nudge') {
        flash('Blocked: the piece would leave the grid');
      } else {
        reportError(e, info.label);
      }
    }
  }

  private install(): void {
    byId('insp').addEventListener('click', (e) => {
      const t = e.target as Element;
      const sw = t.closest<HTMLElement>('[data-applyall]');
      if (sw !== null) {
        this.toggleAll();
        return;
      }
      const b = t.closest<HTMLButtonElement>('button[data-task]');
      if (b !== null && !b.disabled) {
        void this.run(b.dataset.task as ShapeTask);
        return;
      }
      const sh = t.closest<HTMLElement>('.sec>button.sh');
      sh?.parentElement?.classList.toggle('closed');
    });
    const tmat = byId('tmat');
    tmat.addEventListener('mouseover', (e) => {
      const b = (e.target as Element).closest<HTMLButtonElement>('button[data-task]');
      if (b !== null && !b.disabled && b.dataset.task !== this.hovered) {
        this.preview(b.dataset.task as ShapeTask);
      }
    });
    tmat.addEventListener('mouseout', (e) => {
      const b = (e.target as Element).closest<HTMLButtonElement>('button[data-task]');
      if (b !== null && !b.contains(e.relatedTarget as Node | null)) {
        this.preview(null);
      }
    });
    byId('insp').addEventListener('keydown', (e) => {
      if (
        (e.key === ' ' || e.key === 'Enter') &&
        (e.target as Element).closest('[data-applyall]') !== null
      ) {
        e.preventDefault();
        this.toggleAll();
      }
    });
    for (const c of $$('#insp .sec>button.sh .chev')) {
      c.innerHTML = ic('chevD', 14);
    }
  }

  private toggleAll(): void {
    this.host.state.applyAll = !this.host.state.applyAll;
    this.host.applyAllChanged();
  }
}
