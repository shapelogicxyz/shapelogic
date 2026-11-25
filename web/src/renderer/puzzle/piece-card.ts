// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Puzzle workspace's right card: the selected piece (its cells; its
// copies in the puzzle, or a count range with the switch between the two;
// its exclusive group: None, G1…, + New, and for a piece of several
// copies how many are in the group, the original's groups editor), the
// exclusive groups with their members (delete a group), and the color
// rules: rows the piece color, columns the result color, a ticked cell
// allowing a piece cell of the one on a result cell of the other, with the
// presets "each color on itself", "allow all" and "clear". The matrix has
// the puzzle's constraint colors (the original's constraints window); the
// mockup's had a "Default" row and column, but a cell without a color
// fits any (problem.setColorRule takes colors only). From
// docs/mockup/index.html: `renderPzSel`, `renderPzGroups`,
// `renderPzRules`, and the #pzSel, #pzGroups and #pzRules handlers.

import './piece-card.css';
import type { Color } from '../../engine/api.generated.ts';
import { byId, esc } from '../shell/dom.ts';
import { ic } from '../shell/icons.ts';
import { stepperHtml, switchHtml, wireSteppers } from '../shell/widgets.ts';
import { countText, groupOf, groupsOf, inGroup, membersOf, partOf } from './counts.ts';
import type { PuzzleWorkspace } from './workspace.ts';

export class PieceCard {
  private readonly ws: PuzzleWorkspace;

  constructor(ws: PuzzleWorkspace) {
    this.ws = ws;
    this.install();
  }

  render(): void {
    this.renderSel();
    this.renderGroups();
    this.renderRules();
  }

  // the mockup's renderPzSel
  private renderSel(): void {
    const ws = this.ws;
    const d = ws.detail;
    const box = byId('pzSel');
    const x = ws.store.snapshot?.shapes.find((s) => s.id === ws.sel);
    box.dataset.shape = x?.id ?? '';
    if (d === null || x === undefined || x.id === d.result) {
      box.innerHTML =
        '<div class="pz-empty" style="padding:4px 0">Select a piece in the lists or click it in the 3D view to set how many copies the puzzle uses.</div>';
      return;
    }
    const part = partOf(d, x.id);
    const range = ws.isRange(x.id);
    const min = part?.min ?? 0;
    const max = part?.max ?? 0;
    // a sliding piece a start or goal needs keeps its count (Part.required)
    const req = part?.required ?? false;
    const stp = (fld: 'min' | 'max', v: number, lab?: string): string =>
      stepperHtml({
        key: fld,
        value: v,
        min: 0,
        less: 'Decrease',
        more: 'Increase',
        label: lab ?? 'Copies',
        ...(lab === undefined ? { cls: 'sz' } : { cls: 'sz inl', inline: lab }),
        ...(req
          ? { locked: 'Its start or goal on the start/goal shape needs exactly this piece' }
          : {}),
      });
    const sliding = ws.store.snapshot?.grid === 'sliding';
    const c = ws.chip(x.id);
    const count = range
      ? `<div class="lab">Count range</div><div class="rng"><div class="f">${stp('min', min, 'Min')}</div><div class="f">${stp('max', max, 'Max')}</div></div>`
      : `<div class="lab">Copies in the puzzle</div><div class="cnt1"><span class="mute" style="font-size:calc(12px + var(--fs))">${max > 0 ? 'Fixed count' : 'Not in this puzzle'}</span>${stp('max', max)}</div>`;
    let group = '';
    // a sliding puzzle has no groups (the original grayed Groups out)
    if (part !== undefined && !sliding) {
      const g = groupOf(part);
      const gs = groupsOf(d)
        .map((n) => {
          const k = inGroup(part, n);
          const some = k > 0 && k < part.max;
          return `<button data-grp="${String(n)}" class="${k > 0 ? 'on' : ''}" title="${some ? `${String(k)} of ${String(part.max)} copies in G${String(n)}` : `Group ${String(n)}`}">G${String(n)}${some ? `<span class="gc">×${String(k)}</span>` : ''}</button>`;
        })
        .join('');
      const many =
        g > 0 && part.max > 1
          ? `<div class="gw"><span class="mute" style="font-size:calc(12px + var(--fs))">Copies in G${String(g)}</span>${stepperHtml(
              {
                key: `group:${String(g)}`,
                value: inGroup(part, g),
                min: 0,
                max: part.max,
                less: 'Fewer copies in the group',
                more: 'More copies in the group',
                label: 'Copies in the group',
                cls: 'sz',
              },
            )}</div>`
          : '';
      group = `<div class="lab">Exclusive group</div><div class="gseg"><button data-grp="0" class="${g === 0 ? 'on' : ''}">None</button>${gs}<button data-grp="new" title="Create a new group and put this piece in it">+ New</button></div>${many}`;
    }
    box.innerHTML = `<div class="hd"><span class="chip" style="background:${c.bg};color:${c.fg}">${esc(x.label)}</span><div><b>${esc(x.name) || 'Piece'}</b><span class="m">${String(x.fixed + x.variable)} voxels · ${String(x.fixed)} fixed · ${String(x.variable)} variable</span></div></div>
 ${count}
 ${req ? '' : switchHtml({ on: range, id: 'pzRange', style: 'margin-top:12px', text: 'Allow a range (min–max) instead of a fixed count', tip: 'Let the puzzle use from a minimum to a maximum of this piece, rather than a fixed count' })}
 ${group}`;
  }

  // the mockup's renderPzGroups: the engine's groups and their members
  private renderGroups(): void {
    const ws = this.ws;
    const d = ws.detail;
    const groups = groupsOf(d);
    byId('pzGroupsSec').style.display =
      groups.length > 0 && ws.store.snapshot?.grid !== 'sliding' ? '' : 'none';
    byId('pzGroups').innerHTML = groups
      .map((g) => {
        const mem = membersOf(d, g);
        return `<div class="pzg" data-g="${String(g)}"><div class="gh" title="Pieces in a group are exclusive">Group ${String(g)}<span class="mute dhelp" style="font-weight:400;font-size:calc(11px + var(--fs))">pieces in a group are exclusive</span><span class="sp"></span><button class="ib" data-delg="${String(g)}" title="Remove group">${ic('trash', 14)}</button></div><div class="mem">${mem
          .map((p) => {
            const s = ws.store.snapshot?.shapes.find((x) => x.id === p.shape);
            const c = ws.chip(p.shape);
            const n = inGroup(p, g);
            return `<span class="ptile" data-u="${p.shape}" style="background:${c.bg};color:${c.fg}" title="${String(n)} of ${countText(p)}"><b>${esc(s?.label ?? p.shape)}</b>${s !== undefined && s.name !== '' ? `<span class="pl">${esc(s.name)}</span>` : ''}<span class="pb">×${String(n)}</span></span>`;
          })
          .join('')}</div></div>`;
      })
      .join('');
  }

  // the mockup's renderPzRules over the puzzle's constraint colors
  private renderRules(): void {
    const ws = this.ws;
    const colors: Color[] = ws.store.snapshot?.colors ?? [];
    const rules = ws.detail?.colorRules ?? [];
    const on = (p: string, r: string): boolean =>
      rules.some((x) => x.piece === p && x.result === r);
    const sw = (c: Color, t: string): string =>
      `<span class="sw2" title="${t} ${esc(c.label)}" style="background:${c.rgb}"></span>`;
    const el = byId('pzRules');
    if (ws.detail === null) {
      el.innerHTML = '';
      return;
    }
    if (colors.length === 0) {
      el.innerHTML =
        '<div class="cap">No constraint colors in this puzzle: a piece cell may sit on any result cell. Colors are added in the Shapes palette.</div>';
      return;
    }
    const cols = `grid-template-columns:30px repeat(${String(colors.length)},34px)`;
    el.innerHTML = `<div class="cap">Columns = <b>result</b> color, rows = <b>piece</b> color. A ticked cell lets a piece voxel of the row color sit on a result voxel of the column color.</div><div class="mx"><div class="mxr" style="${cols}"><span></span>${colors.map((c) => sw(c, 'Result color')).join('')}</div>${colors
      .map(
        (c) =>
          `<div class="mxr" style="${cols}">${sw(c, 'Piece color')}${colors
            .map((r) => {
              const a = on(c.id, r.id);
              return `<button class="mxc ${a ? 'on' : ''}" data-p="${c.id}" data-r="${r.id}" aria-pressed="${a}" title="Piece ${esc(c.label)} on result ${esc(r.label)}: ${a ? 'allowed' : 'not allowed'}"></button>`;
            })
            .join('')}</div>`,
      )
      .join(
        '',
      )}</div><div class="btnrow"><button class="btn sm" data-rule="eachOnItself" title="Tick only the diagonal: a piece voxel of each color sits only on a result voxel of the same color">Each color on itself</button><button class="btn sm" data-rule="allowAll" title="Tick every cell: any piece color may sit on any result color">Allow all</button><button class="btn sm" data-rule="clear" title="Untick every cell: a colored piece voxel then fits no colored result voxel">Clear</button></div>`;
  }

  // ---- events ----

  private install(): void {
    const ws = this.ws;
    const sel = byId('pzSel');
    const toggleRange = (): void => {
      const u = ws.sel;
      if (u !== null) {
        void ws.setRange(u, !ws.isRange(u));
      }
    };
    sel.addEventListener('click', (e) => {
      const t = e.target as Element;
      const u = ws.sel;
      if (u === null) {
        return;
      }
      if (t.closest('#pzRange') !== null) {
        toggleRange();
        return;
      }
      const gb = t.closest<HTMLElement>('button[data-grp]');
      if (gb !== null) {
        const v = gb.dataset.grp;
        void ws.setGroup(u, v === 'new' ? 'new' : Number(v));
      }
    });
    // the count or the range, and the copies in the piece's group
    wireSteppers(sel, {
      set: (k, v) => {
        const u = ws.sel;
        if (u === null) {
          return;
        }
        if (k === 'min' || k === 'max') {
          void ws.setCount(u, k, v);
        } else if (k.startsWith('group:')) {
          void ws.setGroupCount(u, Number(k.slice('group:'.length)), v);
        }
      },
    });
    byId('pzGroups').addEventListener('click', (e) => {
      const b = (e.target as Element).closest<HTMLElement>('[data-delg]');
      if (b !== null) {
        void ws.deleteGroup(Number(b.dataset.delg));
      }
    });
    byId('pzRules').addEventListener('click', (e) => {
      const t = e.target as Element;
      const c = t.closest<HTMLElement>('.mxc');
      if (c?.dataset.p !== undefined && c.dataset.r !== undefined) {
        void ws.setColorRule(c.dataset.p, c.dataset.r, !c.classList.contains('on'));
        return;
      }
      const r = t.closest<HTMLElement>('[data-rule]')?.dataset.rule;
      if (r === 'eachOnItself' || r === 'allowAll' || r === 'clear') {
        void ws.setColorRules(r);
      }
    });
  }
}
