// SPDX-License-Identifier: GPL-3.0-or-later
//
// The goal's picture for its window of its own (BtApp.goalWindow): a
// board's SVG drawing made to stand alone, as the page shows it. The
// stylesheet's rules are written into each element as it is drawn now
// (the theme's colours, what the buttons hide), references point inside
// the drawing, and the parts that only take the pointer are left out.
// The window shows it as an image: nothing in it runs or is fetched.

import type { GoalPicture } from '../../engine/app-bridge.ts';

/** The properties a board's drawing takes from the stylesheet. */
const PROPS = [
  'display',
  'opacity',
  'fill',
  'fill-opacity',
  'fill-rule',
  'stroke',
  'stroke-width',
  'stroke-opacity',
  'stroke-dasharray',
  'stroke-linecap',
  'stroke-linejoin',
  'paint-order',
  'filter',
  'clip-path',
  'mask',
  'color',
  'font-family',
  'font-size',
  'font-weight',
  'text-anchor',
  'dominant-baseline',
] as const;

// url("bt-app://app/index.html#pl-grain") as the drawing's own url(#pl-grain)
const ownUrl = (v: string): string =>
  v.replace(/url\(\s*["']?[^"')#]*#([^"')]+)["']?\s*\)/g, 'url(#$1)');

/** What a picture changes of the drawing, and what it leaves out. */
export interface PictureEdit {
  /** Selectors of the elements left out (matched before the classes go). */
  drop?: readonly string[];
  /** Changes the copy, its styles written in, before `drop` runs. */
  edit?: (copy: SVGSVGElement) => void;
}

/**
 * `svg` (a drawing in the page) as standalone markup: each element with
 * the styles it has now, changed by `how`, without what is hidden.
 */
export function standaloneSvg(svg: SVGSVGElement, how: PictureEdit = {}): string {
  const copy = svg.cloneNode(true) as SVGSVGElement;
  const from = [svg, ...svg.querySelectorAll('*')];
  const to = [copy, ...copy.querySelectorAll('*')];
  const hidden: Element[] = [];
  from.forEach((e, i) => {
    const c = to[i];
    if (c === undefined) {
      return;
    }
    const style = getComputedStyle(e);
    if (style.display === 'none' && e !== svg) {
      // what the buttons hide stays out of the picture
      hidden.push(c);
      return;
    }
    let own = c.getAttribute('style') ?? '';
    for (const p of PROPS) {
      const v = style.getPropertyValue(p);
      if (v !== '') {
        own += `;${p}:${ownUrl(v)}`;
      }
    }
    c.setAttribute('style', own.replace(/^;/, ''));
  });
  how.edit?.(copy);
  // what a picture leaves out: the parts that take the pointer, what is hidden
  for (const s of how.drop ?? []) {
    for (const e of copy.querySelectorAll(s)) {
      e.remove();
    }
  }
  for (const e of hidden) {
    e.remove();
  }
  for (const e of [copy, ...copy.querySelectorAll('*')]) {
    e.removeAttribute('class');
  }
  copy.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
  copy.setAttribute('xmlns:xlink', 'http://www.w3.org/1999/xlink');
  return new XMLSerializer().serializeToString(copy);
}

/** The page's background colour, for the goal window (the panel the board stands on). */
export function pageBackground(): string {
  const panel = getComputedStyle(document.documentElement).getPropertyValue('--panel').trim();
  return /^#[0-9a-f]{6}$/i.test(panel) ? panel : '#ffffff';
}

/** A picture of a drawing for the goal window. */
export function goalPicture(title: string, svg: SVGSVGElement, how: PictureEdit = {}): GoalPicture {
  const box = svg.viewBox.baseVal;
  return {
    title,
    svg: standaloneSvg(svg, how),
    width: Math.max(1, Math.round(box.width || svg.width.baseVal.value)),
    height: Math.max(1, Math.round(box.height || svg.height.baseVal.value)),
    background: pageBackground(),
  };
}
