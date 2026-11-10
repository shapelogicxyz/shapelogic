// SPDX-License-Identifier: GPL-3.0-or-later
//
// The layout at a small window (1200 x 800, smaller than the tests' own):
// in the three workspaces (a brick puzzle with solutions, a sliding and a
// stacking one; the search options open), the Display menu and the
// settings pages, the page does not scroll, and every control shown lies
// inside the window and is not cut off by a card or panel around it (a
// list that scrolls up and down may hide rows above or below, not cut
// them at the side).

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import { appOf, BUILT, ENGINE, launch, menu, openFixture } from './app.ts';
import type { App } from './app.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

/** The small window's content size. */
const SMALL = { width: 1200, height: 800 } as const;

let a: App;
test.beforeEach(async () => {
  a = await launch();
  await appOf(a.page).evaluate(({ BrowserWindow }, size) => {
    BrowserWindow.getAllWindows()[0]?.setContentSize(size.width, size.height);
  }, SMALL);
  await expect.poll(() => a.page.evaluate(() => window.innerWidth)).toBe(SMALL.width);
});
test.afterEach(async () => {
  await a.close();
});

/** The controls shown cut off or outside the window, and the page's own overflow, described. */
async function cutOff(page: Page): Promise<string[]> {
  // the cards' and panels' transitions over
  await page.waitForTimeout(400);
  return page.evaluate(() => {
    const out: string[] = [];
    const root = document.documentElement;
    if (root.scrollWidth > window.innerWidth || root.scrollHeight > window.innerHeight) {
      out.push(`the page scrolls: ${String(root.scrollWidth)} x ${String(root.scrollHeight)}`);
    }
    // a pixel's slack for rounding
    const slack = 1;
    const sel = 'button, [role=switch], [role=listbox], input, select, textarea, [tabindex="0"]';
    for (const el of document.querySelectorAll<HTMLElement>(sel)) {
      const r = el.getBoundingClientRect();
      if (r.width === 0 || r.height === 0 || el.closest('[aria-hidden="true"]') !== null) {
        continue;
      }
      if (getComputedStyle(el).visibility === 'hidden') {
        continue;
      }
      const name = (): string => {
        const text = el.textContent.trim().replace(/\s+/g, ' ').slice(0, 30);
        const attrs = [...el.attributes]
          .filter((x) => x.name === 'id' || x.name.startsWith('data-'))
          .map((x) => `[${x.name}=${x.value}]`)
          .join('');
        return `${el.tagName.toLowerCase()}${attrs} "${text}"`;
      };
      // a control in a list that scrolls up and down may be scrolled out of sight
      let scrolls = false;
      let clip: string | null = null;
      for (let p = el.parentElement; p !== null && clip === null; p = p.parentElement) {
        const s = getComputedStyle(p);
        const b = p.getBoundingClientRect();
        if (s.overflowX !== 'visible') {
          if (r.left < b.left - slack || r.right > b.right + slack) {
            clip = `at the side of ${p.id === '' ? p.className : `#${p.id}`}`;
          }
        }
        const scrollsY = s.overflowY === 'auto' || s.overflowY === 'scroll';
        if (s.overflowY !== 'visible' && !scrollsY && !scrolls) {
          if (r.top < b.top - slack || r.bottom > b.bottom + slack) {
            clip = `at the top or bottom of ${p.id === '' ? p.className : `#${p.id}`}`;
          }
        }
        scrolls ||= scrollsY;
      }
      if (clip !== null) {
        out.push(`${name()} cut off ${clip}`);
      } else if (
        r.left < -slack ||
        r.right > window.innerWidth + slack ||
        (!scrolls && (r.top < -slack || r.bottom > window.innerHeight + slack))
      ) {
        out.push(`${name()} outside the window`);
      }
    }
    return out;
  });
}

test('at 1200 x 800 every control shown fits, nothing cut off', async () => {
  test.setTimeout(180_000);
  const { page } = a;
  const problems = new Set<string>();
  const check = async (where: string): Promise<void> => {
    for (const m of await cutOff(page)) {
      problems.add(`${where}: ${m}`);
    }
  };
  for (const f of [
    'BallRoom_solved.xmpuzzle',
    'test_sliding_solver.xmpuzzle',
    'test_stacking_hanoi.xmpuzzle',
  ]) {
    await openFixture(page, f);
    await page.keyboard.press('Control+1');
    await check(`${f} entities`);
    await page.click('#bDisp');
    await check(`${f} display menu`);
    await page.keyboard.press('Escape');
    await page.keyboard.press('Control+2');
    await check(`${f} puzzle`);
    // a stacking puzzle has no Groups section
    if (await page.locator('#pzGrpHead').isVisible()) {
      await page.click('#pzGrpHead');
      await check(`${f} puzzle groups`);
      await page.click('#pzGrpHead');
    }
    await page.keyboard.press('Control+3');
    if (await page.locator('#slvOpts').isHidden()) {
      await page.click('#slvOptHead');
    }
    // a solution chosen shows the player (the stacking fixture has none stored)
    const row = page.locator('#slvList .lrow[data-id]').first();
    if ((await row.count()) > 0) {
      await row.click();
    }
    await check(`${f} solver`);
  }
  await menu(page, 'settings');
  for (const b of await page.locator('#setNav button').all()) {
    await b.click();
    await check(`settings ${(await b.textContent())?.trim() ?? ''}`);
  }
  expect([...problems]).toEqual([]);
});
