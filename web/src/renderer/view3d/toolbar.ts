// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 3D view's toolbar and its Display menu. From docs/mockup/index.html:
// the toolbar (`vt`: Orbit, Pan, Edit, Display, Focus; `tbn`), `bDisp`'s
// popover (Show: axes, grid boundary, active layer slab and dim other
// layers when the voxel editor is on screen; Projection; Voxel colour),
// the mode buttons' handler (the navigation hint follows the mode),
// `renderEditBtn` and the Minimal density's auto-hiding toolbar (shown near
// the view's bottom edge, on focus, while its menu is open, briefly after a
// mode key). Added: the render styles of the original (docs/webui-spec.md
// section 7.4: bevelled voxels, flat with edges, the wire frame, and from
// deferred part 5 the STL-like one, section 10.3) and
// "Show through solid pieces" (x-ray), which the mockup offered in the
// Solver only. The Edit button shows the voxel editor's tool (stage 2).

import type { MeshStyle } from '../../engine/api.generated.ts';
import { toolIcon } from '../shell/cursors.ts';
import type { Tool } from '../shell/cursors.ts';
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
  /** The Edit button: 3D edit mode on or off. */
  toggleEdit(): void;
}

/** What the Edit button shows (the mockup's renderEditBtn). */
export interface EditButton {
  tool: Tool;
  /** The tool's name and key, e.g. "Fixed voxel", "1". */
  name: string;
  key: string;
  /** The drawing colour of the badge (null: Default). */
  colour: string | null;
  /** 3D edit mode is on. */
  on: boolean;
  /** A modifier's quick tool is shown for now. */
  tmp: boolean;
  enabled: boolean;
  /** The view's hint lines while edit mode is on (HTML). */
  help: readonly string[];
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
    byId('bEdit').addEventListener('click', () => {
      this.host.toggleEdit();
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

  /** The 3D edit button (the mockup's renderEditBtn). */
  renderEdit(e?: EditButton): void {
    const b = byId<HTMLButtonElement>('bEdit');
    const s = e ?? {
      tool: 'fixed',
      name: 'Fixed voxel',
      key: '1',
      colour: null,
      on: false,
      tmp: false,
      enabled: false,
      help: [],
    };
    const hint = byId('editHint');
    hint.hidden = !(s.on && s.enabled) || s.help.length === 0;
    hint.innerHTML = s.help.map((l) => `<div>${l}</div>`).join('');
    b.innerHTML = `${toolIcon(s.tool, s.colour)}<span>Edit</span><i class="kb">${s.key}</i>`;
    b.disabled = !s.enabled;
    b.classList.toggle('on', s.on);
    b.classList.toggle('tmp', s.tmp);
    b.setAttribute('aria-pressed', String(s.on));
    b.title = `${s.tmp ? 'Quick tool — ' : '3D edit mode — '}${s.name} (${s.key}). ${s.on ? 'Click edits with the active tool. Press E to leave edit mode.' : 'Press E to turn edit mode on; or hold Shift/Alt/Ctrl while clicking.'}`;
  }

  private displayHtml(): string {
    const d = this.host.display;
    const ck = (v: boolean): string => `<span class="ck">${v ? ic('check', 14) : ''}</span>`;
    const ed = editorVisible();
    const needs = ed ? '' : '<span class="dd2">needs Voxel editor</span>';
    const show =
      tab === 'entities'
        ? `<div class="ph">Show</div>
<button data-k="axes" data-keep="1" title="Show the x, y and z axes at the grid’s corner" role="menuitemcheckbox" aria-checked="${d.axes}">${ck(d.axes)}Axes</button>
<button data-k="bounds" data-keep="1" title="Outline the shape’s grid box" role="menuitemcheckbox" aria-checked="${d.bounds}">${ck(d.bounds)}Grid boundary</button>
<button data-k="layer" data-keep="1" title="Show the layer being edited as a slab in the 3D view" role="menuitemcheckbox" aria-checked="${d.layer}" ${ed ? '' : 'disabled'}>${ck(d.layer)}Active layer slab${needs}</button>
<button data-k="dim" data-keep="1" title="Fade the layers other than the one being edited" role="menuitemcheckbox" aria-checked="${d.dim}" ${ed ? '' : 'disabled'}>${ck(d.dim)}Dim other layers${needs}</button><hr>`
        : '';
    return `${show}<div class="ph">Projection</div>
<button data-k="persp" data-keep="1" title="Far things look smaller, as a camera sees them" role="menuitemradio" aria-checked="${d.persp}">${ck(d.persp)}Perspective</button>
<button data-k="ortho" data-keep="1" title="Parallel lines stay parallel: no perspective" role="menuitemradio" aria-checked="${!d.persp}">${ck(!d.persp)}Orthographic</button><hr>
<div class="ph">Voxel color</div>
<button data-k="cpiece" data-keep="1" title="Display normally, each piece in its shape’s color" role="menuitemradio" aria-checked="${!d.colourView}">${ck(!d.colourView)}Piece color</button>
<button data-k="cvox" data-keep="1" title="Display each voxel in its color constraint color" role="menuitemradio" aria-checked="${d.colourView}">${ck(d.colourView)}Voxel color</button><hr>
<div class="ph">Style</div>
<button data-k="drawing" data-keep="1" title="Draw each voxel separately, with bevelled edges" role="menuitemradio" aria-checked="${d.style === 'drawing'}">${ck(d.style === 'drawing')}Bevelled voxels</button>
<button data-k="flat" data-keep="1" title="Draw flat faces with edges" role="menuitemradio" aria-checked="${d.style === 'flat'}">${ck(d.style === 'flat')}Flat with edges</button>
<button data-k="wireframe" data-keep="1" title="Draw wire frames" role="menuitemradio" aria-checked="${d.style === 'wireframe'}">${ck(d.style === 'wireframe')}Wire frame</button>
<button data-k="stl" data-keep="1" role="menuitemradio" aria-checked="${d.style === 'stl'}" title="The pieces as the STL export makes them: chamfered, with a gap, in one color">${ck(d.style === 'stl')}Like the STL export</button>
<button data-k="xray" data-keep="1" title="See the pieces behind solid pieces" role="menuitemcheckbox" aria-checked="${d.xray}">${ck(d.xray)}Show through solid pieces</button>`;
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
      case 'stl':
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
