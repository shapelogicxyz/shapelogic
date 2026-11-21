// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Log File Manager's Export… choices (the last 100 lines, all of the
// log, each with its size) and its line at the bottom left about where to
// report a problem (log-files.ts). Free of the DOM, so the unit tests
// drive it.

import type { LogExportSizes } from '../../engine/app-bridge.ts';
import { sizeText } from './cache-text.ts';

/** Where a problem is reported: the project's issues, or this address. */
export const REPORT_EMAIL = 'shapelogic.xyz@gmail.com';

/** The line at the bottom left: where to send the lines of a problem. */
export const REPORT_HTML = `Copy or export relevant log lines to open an issue in <a href="#" class="lf-issues" title="Open the project’s issues on GitHub in the browser">GitHub Issues</a> or as an email to <span class="lf-mail">${REPORT_EMAIL}</span>`;

/** One of Export…'s choices: its label, and the newest lines it writes (undefined: all). */
export interface ExportChoice {
  label: string;
  lines: number | undefined;
}

/**
 * Export…'s choices: the last lines (when the log has more than they are)
 * and all of the log, each with its size.
 */
export function exportChoices(s: LogExportSizes): ExportChoice[] {
  const all: ExportChoice = {
    label: `All ${s.lines.toLocaleString()} ${s.lines === 1 ? 'line' : 'lines'} (${sizeText(s.bytes)})`,
    lines: undefined,
  };
  if (s.lines <= s.tailLines) {
    return [all];
  }
  return [
    {
      label: `Last ${s.tailLines.toLocaleString()} lines (${sizeText(s.tailBytes)})`,
      lines: s.tailLines,
    },
    all,
  ];
}
