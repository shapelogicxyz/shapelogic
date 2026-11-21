// SPDX-License-Identifier: GPL-3.0-or-later
//
// Text for a template string of HTML, apart from the DOM helpers (dom.ts,
// which gives it on) so that the modules the unit tests run without a DOM
// can write markup too.

/** Text made safe for a template string of HTML. */
export function esc(text: string): string {
  return text.replace(/[&<>"']/g, (c) => {
    switch (c) {
      case '&':
        return '&amp;';
      case '<':
        return '&lt;';
      case '>':
        return '&gt;';
      case '"':
        return '&quot;';
      default:
        return '&#39;';
    }
  });
}
