// SPDX-License-Identifier: GPL-3.0-or-later
//
// A release's notes (its GitHub body, release/notes/vX.Y.md) as HTML for
// the update dialog's Details: the little Markdown the notes use (##
// headings, - lists, paragraphs, **bold**, *emphasis*, `code`, links),
// everything escaped first, so nothing in the notes becomes markup of its
// own. A link keeps its text only: the window opens nothing remote. The
// first heading is left out when it only names the release ("# ShapeLogic
// v1.6"), which the dialog already says. Free of the DOM (its own escape,
// as dom.ts's), so the unit tests read it.

/** Text as HTML: dom.ts's `esc`, which needs the DOM's types. */
const esc = (text: string): string =>
  text.replace(/[&<>"']/g, (c) => `&#${String(c.charCodeAt(0))};`);

/** The notes' Markdown as HTML (escaped; '' for none). */
export function notesHtml(md: string): string {
  const out: string[] = [];
  let para: string[] = [];
  let list: string[] = [];
  const flush = (): void => {
    if (para.length > 0) {
      out.push(`<p>${inline(para.join(' '))}</p>`);
      para = [];
    }
    if (list.length > 0) {
      out.push(`<ul>${list.map((li) => `<li>${inline(li)}</li>`).join('')}</ul>`);
      list = [];
    }
  };
  let first = true;
  for (const raw of md.replace(/\r\n?/g, '\n').split('\n')) {
    const line = raw.trim();
    const h = /^(#{1,6})\s+(.*)$/.exec(line);
    const li = /^[-*+]\s+(.*)$/.exec(line);
    if (line === '') {
      flush();
    } else if (h !== null) {
      flush();
      const text = h[2] ?? '';
      if (!(first && /^ShapeLogic\s+v?\d/i.test(text))) {
        // the notes' ## is the dialog's largest heading
        const level = Math.min(6, Math.max(4, (h[1]?.length ?? 1) + 2));
        out.push(`<h${String(level)}>${inline(text)}</h${String(level)}>`);
      }
    } else if (li !== null) {
      if (para.length > 0) {
        flush();
      }
      list.push(li[1] ?? '');
    } else if (list.length > 0 && /^\s/.test(raw)) {
      // an item's next line
      list[list.length - 1] = `${list.at(-1) ?? ''} ${line}`;
    } else {
      if (list.length > 0) {
        flush();
      }
      para.push(line);
    }
    first = false;
  }
  flush();
  return out.join('');
}

// a line's code, links, bold and emphasis, escaped
function inline(text: string): string {
  // the code set aside first (a NUL and its number), so ** and * leave it alone
  const code: string[] = [];
  return esc(text)
    .replace(/`([^`]*)`/g, (_m, c: string) => `\0${String(code.push(`<code>${c}</code>`) - 1)}\0`)
    .replace(/\[([^\]]*)\]\([^)]*\)/g, '$1')
    .replace(/\*\*(.+?)\*\*/g, '<b>$1</b>')
    .replace(/(^|[^*\w])\*([^*\s][^*]*?)\*(?!\w)/g, '$1<i>$2</i>')
    .replace(/\0(\d+)\0/g, (_m, i: string) => code[Number(i)] ?? '');
}
