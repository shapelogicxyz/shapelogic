// SPDX-License-Identifier: GPL-3.0-or-later
//
// The update dialog's release notes (src/renderer/shell/release-notes.ts):
// the notes' Markdown as escaped HTML.

import { describe, expect, it } from 'vitest';

import { notesHtml } from '../../../src/renderer/shell/release-notes.ts';

describe('the release notes', () => {
  it('reads the headings, lists and paragraphs the notes use, without the title', () => {
    const md = [
      '# ShapeLogic v1.6',
      '',
      "## What's new",
      '',
      '- **Bigger searches.** The `--any-orientation` option.',
      '- Two',
      '  lines.',
      '',
      'Many small *fixes*.',
    ].join('\n');
    expect(notesHtml(md)).toBe(
      '<h4>What&#39;s new</h4>' +
        '<ul><li><b>Bigger searches.</b> The <code>--any-orientation</code> option.</li><li>Two lines.</li></ul>' +
        '<p>Many small <i>fixes</i>.</p>',
    );
  });

  it('escapes everything and keeps only a link’s text', () => {
    expect(notesHtml('<img src=x onerror=alert(1)> [the guide](https://example.com/)')).toBe(
      '<p>&#60;img src=x onerror=alert(1)&#62; the guide</p>',
    );
    expect(notesHtml('`<b>` and **`x`**')).toBe(
      '<p><code>&#60;b&#62;</code> and <b><code>x</code></b></p>',
    );
  });

  it('keeps a first heading that is not the release’s name, and has nothing for no notes', () => {
    expect(notesHtml('# Fixes\n- One')).toBe('<h4>Fixes</h4><ul><li>One</li></ul>');
    expect(notesHtml('')).toBe('');
  });
});
