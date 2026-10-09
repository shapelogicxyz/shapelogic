// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interface's line icons. From docs/mockup/index.html: the icon table
// `I` and `ic` (24 x 24, stroked with the current colour).

/** The icons' SVG bodies by name. */
export const ICONS = {
  plus: '<path d="M12 5v14M5 12h14"/>',
  copy: '<rect x="9" y="9" width="11" height="11" rx="2"/><path d="M5 15V6a2 2 0 0 1 2-2h9"/>',
  trash:
    '<path d="M4 7h16M10 11v6M14 11v6M6 7l1 12a2 2 0 0 0 2 2h6a2 2 0 0 0 2-2l1-12M9 7V4h6v3"/>',
  tag: '<path d="M3 12V4a1 1 0 0 1 1-1h8l9 9-9 9z"/><circle cx="7.5" cy="7.5" r="1.2"/>',
  grip: '<circle cx="9" cy="6" r="1.2" fill="currentColor"/><circle cx="15" cy="6" r="1.2" fill="currentColor"/><circle cx="9" cy="12" r="1.2" fill="currentColor"/><circle cx="15" cy="12" r="1.2" fill="currentColor"/><circle cx="9" cy="18" r="1.2" fill="currentColor"/><circle cx="15" cy="18" r="1.2" fill="currentColor"/>',
  chevD: '<path d="M6 9l6 6 6-6"/>',
  solid: '<rect x="5" y="5" width="14" height="14" rx="2" fill="currentColor"/>',
  dashed: '<rect x="5" y="5" width="14" height="14" rx="2" stroke-dasharray="3 3"/>',
  eraser:
    '<path d="M8 20h12M4.5 14.5l8-8a2 2 0 0 1 2.8 0l3.2 3.2a2 2 0 0 1 0 2.8L12 20H8l-3.5-3.2a2 2 0 0 1 0-2.3z"/>',
  toggle: '<path d="M7 4v13M4 14l3 3 3-3M17 20V7M14 10l3-3 3 3"/>',
  selbox: '<rect x="4" y="4" width="16" height="16" rx="1" stroke-dasharray="3 3"/>',
  cursor: '<path d="M5 3l14 7-6 2-2 6z"/>',
  orbit:
    '<path d="M20 12a8 8 0 1 1-2.5-5.8M20 4v5h-5"/><circle cx="12" cy="12" r="1.6" fill="currentColor"/>',
  pan: '<path d="M12 3v18M3 12h18M9 6l3-3 3 3M9 18l3 3 3-3M6 9l-3 3 3 3M18 9l3 3-3 3"/>',
  zoom: '<circle cx="11" cy="11" r="6"/><path d="M20 20l-4.5-4.5M11 8v6M8 11h6"/>',
  fit: '<path d="M4 9V4h5M20 9V4h-5M4 15v5h5M20 15v5h-5"/>',
  cube: '<path d="M12 3l8 4.5v9L12 21l-8-4.5v-9zM12 12l8-4.5M12 12v9M12 12L4 7.5"/>',
  eye: '<path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
  focus: '<path d="M14 4h6v6M10 20H4v-6M20 4l-7 7M4 20l7-7"/>',
  unfocus: '<path d="M20 10h-6V4M4 14h6v6M14 10l7-7M3 21l7-7"/>',
  panelL: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M9 4v16"/>',
  panelR: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M15 4v16"/>',
  shrink: '<path d="M9 4v5H4M15 4v5h5M9 20v-5H4M15 20v-5h5"/>',
  center: '<circle cx="12" cy="12" r="3"/><path d="M12 3v4M12 17v4M3 12h4M17 12h4"/>',
  origin: '<path d="M5 19V5M5 19h14M5 19l7-7"/>',
  scale:
    '<rect x="3" y="13" width="8" height="8" rx="1"/><rect x="11" y="3" width="10" height="10" rx="1"/>',
  flip: '<path d="M12 3v18M8 7l-5 5 5 5zM16 7l5 5-5 5z"/>',
  flip2: '<path d="M3 12h18M7 8l5-5 5 5zM7 16l5 5 5-5z"/>',
  cw: '<path d="M20 12a8 8 0 1 1-2.5-5.8M20 4v5h-5"/>',
  ccw: '<path d="M4 12a8 8 0 1 0 2.5-5.8M4 4v5h5"/>',
  arL: '<path d="M19 12H5M11 6l-6 6 6 6"/>',
  arR: '<path d="M5 12h14M13 6l6 6-6 6"/>',
  holes:
    '<rect x="4" y="4" width="16" height="16" rx="2"/><rect x="9" y="9" width="6" height="6" fill="currentColor" opacity=".4"/>',
  more: '<circle cx="5" cy="12" r="1.5" fill="currentColor"/><circle cx="12" cy="12" r="1.5" fill="currentColor"/><circle cx="19" cy="12" r="1.5" fill="currentColor"/>',
  check: '<path d="M5 12l5 5 9-10"/>',
  sun: '<circle cx="12" cy="12" r="4"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M5 5l2 2M17 17l2 2M5 19l2-2M17 7l2-2"/>',
  minus: '<path d="M5 12h14"/>',
  brush: '<path d="M18 3l3 3-9 9-3-3z"/><path d="M9 12c-3 0-5 2-5 5 0 2 1 3 3 3 3 0 5-2 5-5"/>',
  edit: '<path d="M4 20h4L19 9l-4-4L4 16z"/><path d="M13 7l4 4"/>',
  gear: '<circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M4.2 4.2l2.1 2.1M17.7 17.7l2.1 2.1M2 12h3M19 12h3M4.2 19.8l2.1-2.1M17.7 6.3l2.1-2.1"/>',
  close: '<path d="M6 6l12 12M18 6L6 18"/>',
  puzzle:
    '<path d="M10 4.5a2 2 0 1 1 4 0V7h3a1 1 0 0 1 1 1v3h-1.5a2 2 0 1 0 0 4H18v3a1 1 0 0 1-1 1h-3v-1.5a2 2 0 1 0-4 0V19H7a1 1 0 0 1-1-1v-3h1.5a2 2 0 1 0 0-4H6V8a1 1 0 0 1 1-1h3z"/>',
  play: '<path d="M8 5l11 7-11 7z"/>',
  warn: '<path d="M12 4l9 16H3z"/><path d="M12 10v4M12 17.2v.1"/>',
  okc: '<circle cx="12" cy="12" r="9"/><path d="M8 12.5l3 3 5-6"/>',
  ptplus: '<path d="M5 3l12 6-5 2-2 5z"/><path d="M17 15v6M14 18h6"/>',
} as const;

/** An icon's name. */
export type IconName = keyof typeof ICONS;

/** An icon as an inline SVG of the given size. */
export function ic(name: IconName, size = 16): string {
  return `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${ICONS[name]}</svg>`;
}
