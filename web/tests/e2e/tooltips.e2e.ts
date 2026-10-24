// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every control has a tooltip: each button, switch, check box, radio
// button, list, number field and slider shown in the three workspaces (a
// brick puzzle with solutions, a sliding and a stacking one; the search
// options open, the pieces as a table), the Display menu and the settings
// pages carries a title, its own or one of a near ancestor's (a label's or
// a row's), which the page shows on hover (held in data-hover-title while
// the pointer is on it).

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import { BUILT, ENGINE, launch, menu, openFixture } from './app.ts';
import type { App } from './app.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

let a: App;
test.beforeEach(async () => {
  a = await launch();
});
test.afterEach(async () => {
  await a.close();
});

/** The controls shown without a tooltip, described for the failure message. */
async function untitled(page: Page): Promise<string[]> {
  return page.evaluate(() => {
    const out: string[] = [];
    const sel = 'button, [role=switch], [role=listbox], input, select, textarea, [tabindex="0"]';
    for (const el of document.querySelectorAll<HTMLElement>(sel)) {
      const r = el.getBoundingClientRect();
      if (r.width === 0 || r.height === 0 || el.closest('[aria-hidden="true"]') !== null) {
        continue;
      }
      // the browser shows the nearest title; a label's or a row's counts, a card's does not
      let tipped = false;
      for (let p: HTMLElement | null = el, d = 0; p !== null && d < 4; p = p.parentElement, d++) {
        tipped ||= p.title !== '' || p.dataset.hoverTitle !== undefined;
      }
      if (!tipped) {
        const text = el.textContent.trim().replace(/\s+/g, ' ').slice(0, 30);
        const attrs = [...el.attributes]
          .filter((x) => x.name === 'id' || x.name.startsWith('data-'))
          .map((x) => `[${x.name}=${x.value}]`)
          .join('');
        out.push(`${el.tagName.toLowerCase()}${attrs} "${text}"`);
      }
    }
    return out;
  });
}

test('every control shown has a tooltip', async () => {
  test.setTimeout(180_000);
  const { page } = a;
  const missing = new Set<string>();
  const check = async (where: string): Promise<void> => {
    for (const m of await untitled(page)) {
      missing.add(`${where}: ${m}`);
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
    await check('settings');
  }
  expect([...missing]).toEqual([]);
});

test('the page draws the tooltip of the control under the pointer', async () => {
  const { page } = a;
  const tip = page.locator('.hovertip');
  const btn = page.locator('#btnSettings');
  const title = await btn.getAttribute('title');
  expect(title).toMatch(/Settings/);
  await btn.hover();
  await expect(tip).toBeVisible();
  await expect(tip).toHaveText(title ?? '');
  // held while the pointer is on it, so the native tooltip has nothing to show
  await expect(btn).not.toHaveAttribute('title', /./);
  await page.mouse.move(2, 300);
  await expect(tip).toBeHidden();
  await expect(btn).toHaveAttribute('title', title ?? '');
  // "Show tooltips" off: none
  await page.keyboard.press('Control+,');
  await page.click('#setPg .switch[data-k="tips"]');
  await page.keyboard.press('Escape');
  await btn.hover();
  await page.waitForTimeout(1000);
  await expect(tip).toBeHidden();
});
