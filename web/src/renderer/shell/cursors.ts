// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pictures of the editing tools: a pointer with the tool's badge for
// fixed and variable, the tool's own picture for erase and paint. From
// docs/mockup/index.html: `badgeSvg` and the toolbar-icon branch of
// `ptrSvg` (the cursors themselves come with the editor, stage 2).

/** An editing tool (docs/webui-spec.md section 3). */
export type Tool = 'fixed' | 'variable' | 'erase' | 'paint';

const isPtrTool = (t: Tool): boolean => t === 'fixed' || t === 'variable';

/** A tool's badge in a 12 x 12 box: `col` the drawing colour (null: Default), `ink` the lines. */
export function badgeSvg(tool: Tool, col: string | null, ink: string, opp?: string): string {
  if (tool === 'fixed' || tool === 'variable') {
    const fill = col ?? opp ?? 'none';
    const plus = (c: string): string =>
      `<path d="M6 3.1v5.8M3.1 6h5.8" stroke="${c}" stroke-width="1.4" stroke-linecap="round"/>`;
    if (tool === 'fixed') {
      return `<rect x=".6" y=".6" width="10.8" height="10.8" rx="2" fill="${fill}" stroke="${ink}" stroke-opacity="${col !== null ? 0.55 : 1}" stroke-width="1"/>${plus(ink)}`;
    }
    return `<rect x="1" y="1" width="10" height="10" rx="2" fill="${fill}" fill-opacity="${col !== null ? 0.32 : 1}" stroke="${col ?? ink}" stroke-width="1.6" stroke-dasharray="1.9 1.6"/>${plus(col ?? ink)}`;
  }
  if (tool === 'erase') {
    return `<path d="M1.7 8.8L6 2.3l4.4 3-3.9 5.6H3.7z" fill="${opp ?? 'none'}" stroke="${ink}" stroke-width="1.3" stroke-linejoin="round"/><path d="M4.6 5.7l4 2.9" stroke="${ink}" stroke-width="1.1" stroke-linecap="round"/>`;
  }
  return `<path d="M2 6.2L6 2.2l4.2 4.2-4 4z" fill="${opp ?? 'none'}" stroke="${ink}" stroke-width="1.2" stroke-linejoin="round"/><path d="M3.5 4.7C2 3.3 2.3 1.3 4.4 1" fill="none" stroke="${ink}" stroke-width="1" stroke-linecap="round"/><path d="M10.5 7.9c.8 1.2 1.4 1.9 1.4 2.6a1.4 1.4 0 0 1-2.8 0c0-.7.6-1.4 1.4-2.6z" fill="${col ?? ink}"/>`;
}

/** A tool as a toolbar icon (28 x 24, tinted by the current colour). */
export function toolIcon(tool: Tool, col: string | null, size = 26): string {
  const head = `<svg xmlns="http://www.w3.org/2000/svg" class="gx" width="${size}" height="${size}" viewBox="0 0 28 24" fill="none" aria-hidden="true">`;
  if (isPtrTool(tool)) {
    return (
      head +
      `<path d="M.8 .8V16.4L4.4 13.2L7 19.3L9.4 18.2L6.8 12.1H11.9Z" fill="currentColor" stroke="currentColor" stroke-width="1.1" stroke-linejoin="round"/><g transform="translate(12.5 .5)">${badgeSvg(tool, col, 'currentColor')}</g></svg>`
    );
  }
  return (
    head +
    `<g transform="translate(14 12) scale(1.75) translate(-6 -6)">${badgeSvg(tool, col, 'currentColor')}</g></svg>`
  );
}
