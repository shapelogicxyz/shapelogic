// SPDX-License-Identifier: GPL-3.0-or-later
//
// The few DOM helpers every view uses. From docs/mockup/index.html: `$`,
// `$$`, `css`, `esc`.

/** The element with this id; throws when the page has none (a broken page, not a user error). */
export function byId<T extends HTMLElement = HTMLElement>(id: string): T {
  const e = document.getElementById(id);
  if (e === null) {
    throw new Error(`the page has no #${id}`);
  }
  return e as T;
}

/** The first element matching a selector, or null. */
export function $(selector: string, root: ParentNode = document): HTMLElement | null {
  return root.querySelector<HTMLElement>(selector);
}

/** Every element matching a selector. */
export function $$(selector: string, root: ParentNode = document): HTMLElement[] {
  return [...root.querySelectorAll<HTMLElement>(selector)];
}

/** A CSS custom property of the document, as the theme sets it now. */
export function css(name: string): string {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

export { esc } from './html.ts';

/** Whether keyboard input goes to a text field (keys then belong to it). */
export function isTyping(target: EventTarget | null): boolean {
  return (
    target instanceof HTMLElement &&
    (target.matches(
      'input:not([type=range]):not([type=checkbox]):not([type=file]),textarea,select',
    ) ||
      target.isContentEditable)
  );
}

const lastHtml = new WeakMap<Element, string>();

/**
 * Sets an element's HTML only when it changed: a re-render that would
 * draw the same keeps its elements, so the focus stays and a double-click
 * whose first click re-rendered still finds its element. The markup is
 * the card's view model: whatever the card shows is in it, so no field
 * can be missed, as a key written by hand can. Returns whether it drew.
 */
export function setHtml(el: HTMLElement, html: string): boolean {
  // emptied by other code since (hasChildNodes: reading innerHTML would
  // serialize the whole element at every call)
  if (lastHtml.get(el) === html && (html === '' || el.hasChildNodes())) {
    return false;
  }
  el.innerHTML = html;
  lastHtml.set(el, html);
  return true;
}

/** Forgets what setHtml last set, so the next call draws again (fields typed into get the engine's values back). */
export function forgetHtml(el: HTMLElement): void {
  lastHtml.delete(el);
}

/** Marks the one element of a list that is selected (class `sel`, aria-selected), by a data key. */
export function markSelected(
  root: ParentNode,
  selector: string,
  key: string,
  sel: string | null,
): void {
  for (const e of root.querySelectorAll<HTMLElement>(selector)) {
    const on = sel !== null && e.dataset[key] === sel;
    e.classList.toggle('sel', on);
    if (e.hasAttribute('aria-selected')) {
      e.setAttribute('aria-selected', String(on));
    }
  }
}
