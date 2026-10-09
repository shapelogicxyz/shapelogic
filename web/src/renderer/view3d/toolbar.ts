// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 3D view's toolbar and its Display menu. From docs/mockup/index.html:
// the toolbar (`vt`: Orbit, Pan, Edit, Display, Focus; `tbn`), `bDisp`'s
// popover (Show: axes, grid boundary, active layer slab and dim other
// layers when the voxel editor is on screen; Projection; Voxel colour),
// the mode buttons' handler (the navigation hint follows the mode),
// `renderEditBtn` and the Minimal density's auto-hiding toolbar (shown near
// the view's top edge, on focus, while its menu is open, briefly after a
// mode key). Added: the render styles of the original (docs/webui-spec.md
// section 7.4: bevelled voxels, flat with edges, and the wire frame) and
// "Show through solid pieces" (x-ray), which the mockup offered in the
// Solver only. The Edit button waits for the editor (stage 2).

import type { MeshStyle } from '../../engine/api.generated.ts';
import { toolIcon } from '../shell/cursors.ts';
import { byId } from '../shell/dom.ts';
import { ic } from '../shell/icons.ts';
import type { IconName } from '../shell/icons.ts';
import { editorVisible, layout, tab } from '../shell/layout.ts';
import { closePop, openPop, popOpen, refreshPop } from '../shell/popover.ts';
import { prefs } from '../shell/prefs.ts';

/** The Display menu's choices (the mockup's `st.disp`, `st.persp`, `st.colorView`). */
export interface Display {
  axes: boolean;
  bounds: boolean;
  layer: boolean;
  dim: boolean;
  persp: boolean;
  /** The constraint colours (the mockup's "Voxel colour"). */
  colourView: boolean;
  style: MeshStyle;
  xray: boolean;
}

/** The navigation mode of a plain left drag. */
export type NavMode = 'orbit' | 'pan';

const tbn = (icon: IconName, caption: string): string => `${ic(icon, 20)}<span>${caption}</span>`;

const HINT = {
  orbit: 'Drag orbit · Shift/middle-drag pan · Wheel zoom',
  pan: 'Drag pan · Shift-drag orbit · Middle-drag pan · Wheel zoom',
} as const;

/** What the toolbar asks of the view. */
export interface ToolbarHost {
  display: Display;
  mode(): NavMode;
  setMode(m: NavMode): void;
  /** A Display choice changed. */
  changed(): void;
  toggleFocus(): void;
}

export class Toolbar {
  private readonly host: ToolbarHost;
  private readonly vt = byId('vt');
  private hideTimer = 0;

  constructor(host: ToolbarHost) {
    this.host = host;
    this.vt.innerHTML =
      `<button class="tbn on" data-m="orbit" title="Left-drag orbits. Shift-drag pans. Middle-drag always pans.">${tbn('orbit', 'Orbit')}<i class="kb">O</i></button>` +
      `<button class="tbn" data-m="pan" title="Left-drag pans. Shift-drag orbits. Middle-drag always pans.">${tbn('pan', 'Pan')}<i class="kb">P</i></button><span class="sep"></span>` +
      `<button class="tbn ent-only" id="bEdit" disabled></button><span class="sep ent-only"></span>` +
      `<button class="tbn" id="bDisp" title="Display options" aria-haspopup="menu">${tbn('eye', 'Display')}</button><span class="sep"></span>` +
      `<button class="tbn" id="bF3D" title="Focus 3D viewport (Esc to exit)"></button>`;
    this.renderEdit();
    this.renderFocus();
    this.vt.addEventListener('click', (e) => {
      const b = (e.target as Element).closest<HTMLElement>('[data-m]');
      if (b !== null) {
        this.host.setMode(b.dataset.m === 'pan' ? 'pan' : 'orbit');
      }
    });
    byId('bDisp').addEventListener('click', (e) => {
      if (popOpen()) {
        closePop();
        return;
      }
      openPop(e.currentTarget as Element, this.displayHtml(), (k) => {
        this.choose(k);
        refreshPop(this.displayHtml());
      });
    });
    byId('bF3D').addEventListener('click', () => {
      this.host.toggleFocus();
    });
    this.installAutoHide();
  }

  /** Shows the mode. */
  renderMode(): void {
    for (const b of this.vt.querySelectorAll<HTMLElement>('[data-m]')) {
      const on = b.dataset.m === this.host.mode();
      b.classList.toggle('on', on);
      b.setAttribute('aria-pressed', String(on));
    }
    byId('navHint').textContent = HINT[this.host.mode()];
    byId('cv').classList.toggle('pan', this.host.mode() === 'pan');
    this.flashShow();
  }

  /** Shows the focus button as the layout has it. */
  renderFocus(): void {
    const f3 = layout.focus === '3d';
    const b = byId('bF3D');
    b.innerHTML = tbn(f3 ? 'unfocus' : 'focus', f3 ? 'Exit' : 'Focus');
    b.classList.toggle('on', f3);
    b.setAttribute('aria-pressed', String(f3));
  }

  // the 3D edit button (renderEditBtn), waiting for the editor
  private renderEdit(): void {
    const b = byId('bEdit');
    b.innerHTML = `${toolIcon('fixed', null)}<span>Edit</span><i class="kb">1</i>`;
    b.title = '3D edit mode — a plain click uses the active voxel tool (with the voxel editor)';
  }

  private displayHtml(): string {
    const d = this.host.display;
    const ck = (v: boolean): string => `<span class="ck">${v ? ic('check', 14) : ''}</span>`;
    const ed = editorVisible();
    const needs = ed ? '' : '<span class="dd2">needs Voxel editor</span>';
    const show =
      tab === 'entities'
        ? `<div class="ph">Show</div>
<button data-k="axes" data-keep="1" role="menuitemcheckbox" aria-checked="${d.axes}">${ck(d.axes)}Axes</button>
<button data-k="bounds" data-keep="1" role="menuitemcheckbox" aria-checked="${d.bounds}">${ck(d.bounds)}Grid boundary</button>
<button data-k="layer" data-keep="1" role="menuitemcheckbox" aria-checked="${d.layer}" ${ed ? '' : 'disabled'}>${ck(d.layer)}Active layer slab${needs}</button>
<button data-k="dim" data-keep="1" role="menuitemcheckbox" aria-checked="${d.dim}" ${ed ? '' : 'disabled'}>${ck(d.dim)}Dim other layers${needs}</button><hr>`
        : '';
    return `${show}<div class="ph">Projection</div>
<button data-k="persp" data-keep="1" role="menuitemradio" aria-checked="${d.persp}">${ck(d.persp)}Perspective</button>
<button data-k="ortho" data-keep="1" role="menuitemradio" aria-checked="${!d.persp}">${ck(!d.persp)}Orthographic</button><hr>
<div class="ph">Voxel colour</div>
<button data-k="cpiece" data-keep="1" role="menuitemradio" aria-checked="${!d.colourView}">${ck(!d.colourView)}Piece colour</button>
<button data-k="cvox" data-keep="1" role="menuitemradio" aria-checked="${d.colourView}">${ck(d.colourView)}Voxel colour</button><hr>
<div class="ph">Style</div>
<button data-k="drawing" data-keep="1" role="menuitemradio" aria-checked="${d.style === 'drawing'}">${ck(d.style === 'drawing')}Bevelled voxels</button>
<button data-k="flat" data-keep="1" role="menuitemradio" aria-checked="${d.style === 'flat'}">${ck(d.style === 'flat')}Flat with edges</button>
<button data-k="wireframe" data-keep="1" role="menuitemradio" aria-checked="${d.style === 'wireframe'}">${ck(d.style === 'wireframe')}Wire frame</button>
<button data-k="xray" data-keep="1" role="menuitemcheckbox" aria-checked="${d.xray}">${ck(d.xray)}Show through solid pieces</button>`;
  }

  private choose(k: string): void {
    const d = this.host.display;
    switch (k) {
      case 'persp':
        d.persp = true;
        break;
      case 'ortho':
        d.persp = false;
        break;
      case 'cpiece':
        d.colourView = false;
        break;
      case 'cvox':
        d.colourView = true;
        break;
      case 'drawing':
      case 'flat':
      case 'wireframe':
        d.style = k;
        break;
      case 'axes':
      case 'bounds':
      case 'layer':
      case 'dim':
      case 'xray':
        d[k] = !d[k];
        break;
      default:
        return;
    }
    this.host.changed();
  }

  // Minimal density: the toolbar shows near the view's top edge, on focus,
  // while its menu is open, and briefly after a mode key
  private minimal(): boolean {
    return prefs.density === 'Minimal';
  }

  private show(): void {
    clearTimeout(this.hideTimer);
    this.vt.classList.add('show');
    byId('vthandle').classList.add('off');
  }

  private hideSoon(ms = 700): void {
    clearTimeout(this.hideTimer);
    this.hideTimer = window.setTimeout(() => {
      if (
        this.vt.matches(':hover') ||
        this.vt.querySelector(':focus-visible') !== null ||
        popOpen()
      ) {
        this.hideSoon(400);
        return;
      }
      this.vt.classList.remove('show');
      byId('vthandle').classList.remove('off');
    }, ms);
  }

  /** Shows the toolbar for a moment (after a mode key) in Minimal density. */
  flashShow(): void {
    if (this.minimal()) {
      this.show();
      this.hideSoon(1400);
    }
  }

  /** Back to hidden (the density changed). */
  reset(): void {
    clearTimeout(this.hideTimer);
    this.vt.classList.remove('show');
    byId('vthandle').classList.remove('off');
  }

  private installAutoHide(): void {
    const vp = byId('vp');
    vp.insertAdjacentHTML('beforeend', '<div class="vthandle" id="vthandle"></div>');
    vp.addEventListener('pointermove', (e) => {
      if (!this.minimal()) {
        return;
      }
      const r = vp.getBoundingClientRect();
      if (e.clientY - r.top < 72) {
        this.show();
      } else if (!this.vt.matches(':hover')) {
        this.hideSoon();
      }
    });
    vp.addEventListener('pointerleave', () => {
      if (this.minimal()) {
        this.hideSoon(500);
      }
    });
    this.vt.addEventListener('focusin', () => {
      if (this.minimal() && this.vt.querySelector(':focus-visible') !== null) {
        this.show();
      }
    });
    this.vt.addEventListener('focusout', () => {
      if (this.minimal()) {
        this.hideSoon();
      }
    });
    vp.addEventListener('pointerdown', (e) => {
      if (this.minimal() && (e.target as Element).closest('#vt') === null) {
        const a = document.activeElement;
        if (a instanceof HTMLElement && this.vt.contains(a)) {
          a.blur();
        }
      }
    });
  }
}
