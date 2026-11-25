// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shell's controls, written once (cleanup item 12.5): the stepper (a
// number with minus and plus buttons, typed or stepped, held to its range)
// with one delegated handler; the switch's markup, and the one key handler
// that toggles a focused switch with Space or Enter (keyboard.ts calls it
// first); the rename in place of a list row; the arrow keys of a list; and
// the keyboard map printed as the settings dialog and Help print it. From
// docs/mockup/index.html: `kbKeys`, `kbRow`, the `.step` and `.switch`
// markup.

import { esc } from './dom.ts';
import { ic } from './icons.ts';
import { keyGroups } from './keys.ts';
import type { KeyRow } from './keys.ts';

/** A stepper: a whole number from min to max, its buttons a step apart. */
export interface StepperSpec {
  /** The value's name, given back to the handler. */
  key: string;
  value: number;
  min: number;
  /** None: no upper bound (the plus button never grays). */
  max?: number;
  /** How far a button moves the value (1). */
  step?: number;
  /** The minus and plus buttons' tooltips. */
  less: string;
  more: string;
  /** The field's name for a screen reader, and its tooltip (the name when none). */
  label: string;
  tip?: string;
  /** An axis letter before the minus button (X, Y, Z: its axis color). */
  axis?: string;
  /** A label between the minus button and the field. */
  inline?: string;
  /** More classes of the stepper (`sz`, `inl`). */
  cls?: string;
  /** Every part grayed, with this tooltip (a value that cannot be changed now). */
  locked?: string;
  /**
   * A typed value that is not whole or out of the range is refused (the
   * handler's `refused`), not rounded and held to the range.
   */
  strict?: true;
}

/** A stepper's markup. */
export function stepperHtml(s: StepperSpec): string {
  const max = s.max ?? Infinity;
  const step = s.step ?? 1;
  const locked = s.locked !== undefined;
  const off = (at: boolean, tip: string): string =>
    locked
      ? `disabled title="${esc(s.locked ?? '')}"`
      : `${at ? 'disabled ' : ''}title="${esc(tip)}"`;
  const data = `data-step="${esc(s.key)}" data-v="${String(s.value)}" data-min="${String(s.min)}" data-max="${Number.isFinite(max) ? String(max) : ''}" data-by="${String(step)}"${s.strict === true ? ' data-strict="1"' : ''}`;
  return (
    `<div class="step${s.cls === undefined ? '' : ` ${s.cls}`}" ${data}>` +
    (s.axis === undefined ? '' : `<b class="ax-${s.axis.toLowerCase()}">${esc(s.axis)}</b>`) +
    `<button data-sd="-1" ${off(s.value <= s.min, s.less)}>${ic('minus', 12)}</button>` +
    (s.inline === undefined ? '' : `<span class="il">${esc(s.inline)}</span>`) +
    `<input class="v" value="${String(s.value)}" inputmode="numeric" aria-label="${esc(s.label)}" title="${esc(s.tip ?? s.label)}"${locked ? ' disabled' : ''}>` +
    `<button data-sd="1" ${off(s.value >= max, s.more)}>${ic('plus', 12)}</button></div>`
  );
}

/** What a set of steppers does with a new value. */
export interface StepperHandler {
  /**
   * A new value of a stepper, held to its range (a strict one's typed value
   * inside it): from a button (`typed` false) or typed in.
   */
  set(key: string, value: number, typed: boolean): void;
  /** Typed text that is no number, or Esc: the field should show the value again (by default it is put back). */
  restore?(key: string): void;
  /** A strict stepper's typed value out of its range (restore follows). */
  refused?(key: string, min: number, max: number): void;
}

/** The numbers of a stepper's element. */
function stepperOf(el: Element): {
  box: HTMLElement;
  key: string;
  v: number;
  min: number;
  max: number;
  by: number;
  strict: boolean;
} | null {
  const box = el.closest<HTMLElement>('.step[data-step]');
  if (box === null) {
    return null;
  }
  const d = box.dataset;
  return {
    box,
    key: d.step ?? '',
    v: Number(d.v),
    min: Number(d.min),
    max: d.max === '' || d.max === undefined ? Infinity : Number(d.max),
    by: Number(d.by ?? 1),
    strict: d.strict === '1',
  };
}

/**
 * Wires the steppers inside an element (drawn again at will): a button
 * steps the value shown, a field typed in sets it once confirmed (Enter
 * or leaving it), Esc puts the field back, and a field taken selects its
 * text.
 */
export function wireSteppers(root: HTMLElement, h: StepperHandler): void {
  const restore = (s: { box: HTMLElement; key: string; v: number }): void => {
    if (h.restore !== undefined) {
      h.restore(s.key);
      return;
    }
    const input = s.box.querySelector<HTMLInputElement>('input.v');
    if (input !== null) {
      input.value = String(s.v);
    }
  };
  root.addEventListener('click', (e) => {
    const b = (e.target as Element).closest<HTMLButtonElement>('button[data-sd]');
    const s = b === null ? null : stepperOf(b);
    if (b === null || b.disabled || s === null) {
      return;
    }
    const d = b.dataset.sd ?? '1';
    const next = Math.min(s.max, Math.max(s.min, s.v + s.by * Number(d)));
    if (next !== s.v) {
      h.set(s.key, next, false);
      // drawn again at once: the press keeps its place
      if (!b.isConnected) {
        root
          .querySelector<HTMLElement>(
            `.step[data-step="${CSS.escape(s.key)}"] button[data-sd="${d}"]`,
          )
          ?.focus();
      }
    }
  });
  root.addEventListener('change', (e) => {
    const input = e.target as Element;
    const s = input.matches('input.v') ? stepperOf(input) : null;
    if (s === null) {
      return;
    }
    const text = (input as HTMLInputElement).value.trim();
    const n = Math.round(Number(text));
    if (text === '' || !Number.isFinite(n)) {
      restore(s);
    } else if (s.strict && (!Number.isInteger(Number(text)) || n < s.min || n > s.max)) {
      h.refused?.(s.key, s.min, s.max);
      restore(s);
    } else {
      const next = Math.min(s.max, Math.max(s.min, n));
      if (next !== s.v) {
        h.set(s.key, next, true);
      } else {
        restore(s);
      }
    }
  });
  root.addEventListener('focusin', (e) => {
    const t = e.target as Element;
    if (t.matches('input.v') && stepperOf(t) !== null) {
      (t as HTMLInputElement).select();
    }
  });
  root.addEventListener('keydown', (e) => {
    const t = e.target as Element;
    const s = t.matches('input.v') ? stepperOf(t) : null;
    if (s === null) {
      return;
    }
    if (e.key === 'Enter') {
      e.preventDefault();
      (t as HTMLInputElement).blur();
    } else if (e.key === 'Escape' && (t as HTMLInputElement).value !== String(s.v)) {
      // back to the value shown, without changing it (a second Esc goes on)
      e.preventDefault();
      restore(s);
    }
  });
}

/** A switch. */
export interface SwitchSpec {
  on: boolean;
  /** Its tooltip. */
  tip: string;
  /** Its name for a screen reader, when no text beside it says it. */
  label?: string;
  /** The text beside it, inside the switch. */
  text?: string;
  /** Its data attributes (`{ o: 'justCount' }` is data-o="justCount"). */
  data?: Record<string, string>;
  id?: string;
  /** Grayed: a click does nothing (the `off` class). */
  off?: boolean;
  style?: string;
}

/** A switch's markup: a focusable `role="switch"` that the one key handler toggles. */
export function switchHtml(s: SwitchSpec): string {
  const data = Object.entries(s.data ?? {})
    .map(([k, v]) => ` data-${k}="${esc(v)}"`)
    .join('');
  return (
    `<div class="switch${s.on ? ' on' : ''}${s.off === true ? ' off' : ''}"${s.id === undefined ? '' : ` id="${esc(s.id)}"`}${data} role="switch" tabindex="0" aria-checked="${String(s.on)}"` +
    `${s.label === undefined ? '' : ` aria-label="${esc(s.label)}"`} title="${esc(s.tip)}"${s.style === undefined ? '' : ` style="${s.style}"`}>` +
    `<span class="tr"></span>${s.text === undefined ? '' : `<span>${esc(s.text)}</span>`}</div>`
  );
}

/**
 * Space or Enter on a focused switch clicks it (each switch's click does
 * what it does): true when the press was taken. keyboard.ts calls this
 * before anything else hears the key.
 */
export function pressSwitch(e: KeyboardEvent): boolean {
  if ((e.key !== ' ' && e.key !== 'Enter') || e.ctrlKey || e.metaKey || e.altKey) {
    return false;
  }
  const t = e.target;
  const sw = t instanceof Element ? t.closest<HTMLElement>('[role="switch"]') : null;
  if (sw === null) {
    return false;
  }
  if (!e.repeat) {
    sw.click();
  }
  return true;
}

/**
 * Puts a field in place of a row's label to rename it: focused, its text
 * selected; Enter confirms by leaving it; `end` is called once it is left.
 */
export function renameInPlace(
  label: Element,
  o: { value: string; name: string; end: () => void },
): HTMLInputElement | null {
  label.innerHTML = `<input value="${esc(o.value)}" placeholder="Label" aria-label="${esc(o.name)}">`;
  const input = label.querySelector('input');
  if (input === null) {
    return null;
  }
  input.focus();
  input.select();
  input.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      input.blur();
    }
  });
  input.addEventListener('blur', o.end);
  return input;
}

/**
 * The item a list's arrow keys go to from `at` of `n`: Up and Down one
 * (held at the ends, or round with `wrap`), Home and End the first and the
 * last; null for another key or an empty list.
 */
export function listKeys(key: string, at: number, n: number, wrap = false): number | null {
  if (n === 0) {
    return null;
  }
  switch (key) {
    case 'Home':
      return 0;
    case 'End':
      return n - 1;
    case 'ArrowDown':
    case 'ArrowUp': {
      const d = key === 'ArrowDown' ? 1 : -1;
      return wrap ? (((at + d) % n) + n) % n : Math.max(0, Math.min(n - 1, at + d));
    }
    default:
      return null;
  }
}

/** The keys of a row of the keyboard map as kbd chips (the mockup's kbKeys). */
export function kbKeys(spec: string): string {
  return spec
    .split(' / ')
    .map((alt) =>
      alt
        .split(' · ')
        .map((part) =>
          part.startsWith('@')
            ? `<em>${esc(part.slice(1))}</em>`
            : part
                .split(/\+(?=.)/)
                .map((t) =>
                  t.startsWith('@') ? `<em>${esc(t.slice(1))}</em>` : `<kbd>${esc(t)}</kbd>`,
                )
                .join('<i>+</i>'),
        )
        .join('<i>·</i>'),
    )
    .join('<i>/</i>');
}

/** A row of the keyboard map as the help prints it. */
export const kbRowHtml = (r: KeyRow): string =>
  `<div class="kbrow"><span>${esc(r.does)}</span><span class="k">${kbKeys(r.keys)}</span></div>`;

/**
 * The keyboard map by its groups (keys.ts), the rows whose keys, action or
 * group hold the query (all for none); empty when none does.
 */
export function keysHtml(query = ''): string {
  const q = query.trim().toLowerCase();
  return keyGroups()
    .map((g) => {
      const rows = g.rows.filter(
        (r) => q === '' || `${r.keys} ${r.does} ${g.group}`.toLowerCase().includes(q),
      );
      return rows.length === 0
        ? ''
        : `<div class="kbgrp">${esc(g.group)}${g.sub === undefined ? '' : `<small>${esc(g.sub)}</small>`}</div>` +
            rows.map(kbRowHtml).join('');
    })
    .join('');
}
