// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Play tab's goal window (engine/app-bridge.ts's goal picture and page,
// main/goal-window.ts) and the goal places a sliding goal picture moves the
// pieces to (renderer/play/goal-places.ts).

import { describe, expect, it } from 'vitest';

import { goalPage, goalWindowSize, isGoalPicture } from '../../src/engine/app-bridge.ts';
import type { GoalPicture } from '../../src/engine/app-bridge.ts';
import { goalPlaces, goalTitle } from '../../src/renderer/play/goal-places.ts';

const picture: GoalPicture = {
  title: 'Goal: P1 <b>"Klotski"</b> & co',
  svg: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 200"><text>é</text></svg>',
  width: 400,
  height: 200,
  background: '#1a1d24',
  dark: true,
};

describe('the goal window', () => {
  it('takes only a picture of the right shape', () => {
    expect(isGoalPicture(picture)).toBe(true);
    expect(isGoalPicture({ ...picture, background: 'rgb(26, 29, 36)' })).toBe(true);
    expect(isGoalPicture(null)).toBe(false);
    expect(isGoalPicture({ ...picture, svg: '<script>alert(1)</script>' })).toBe(false);
    expect(isGoalPicture({ ...picture, background: 'red;}body{display:none' })).toBe(false);
    expect(isGoalPicture({ ...picture, background: 'url(x)' })).toBe(false);
    expect(isGoalPicture({ ...picture, width: 0 })).toBe(false);
    expect(isGoalPicture({ ...picture, dark: undefined })).toBe(false);
    expect(isGoalPicture({ ...picture, height: Number.NaN })).toBe(false);
    expect(isGoalPicture({ ...picture, title: 'x'.repeat(301) })).toBe(false);
  });

  it('shows the drawing as an image under a policy that runs and fetches nothing', () => {
    const page = goalPage(picture);
    expect(page).toContain(
      `content="default-src 'none'; img-src data:; style-src 'unsafe-inline'"`,
    );
    // the title escaped where it is written, the drawing only inside the image's data: URL
    expect(page).toContain(
      '<title>Goal: P1 &lt;b&gt;&quot;Klotski&quot;&lt;/b&gt; &amp; co</title>',
    );
    expect(page).not.toContain('<svg');
    const data = /src="data:image\/svg\+xml;base64,([^"]+)"/.exec(page)?.[1] ?? '';
    expect(new TextDecoder().decode(Uint8Array.from(atob(data), (c) => c.charCodeAt(0)))).toBe(
      picture.svg,
    );
    expect(page).toContain('background:#1a1d24');
  });

  it('writes GOAL in bold above the drawing, white in the dark theme and black in the light', () => {
    const dark = goalPage(picture);
    expect(dark).toContain('<body><h1>GOAL</h1><img');
    expect(dark).toMatch(/h1\{[^}]*font:bold [^}]*color:#ffffff/);
    expect(goalPage({ ...picture, dark: false })).toMatch(/h1\{[^}]*color:#000000/);
  });

  it('opens in the drawing’s shape under its caption, at most 640 pixels across or down', () => {
    expect(goalWindowSize(picture)).toEqual({ width: 424, height: 254 });
    expect(goalWindowSize({ ...picture, width: 1200, height: 600 })).toEqual({
      width: 640,
      height: 362,
    });
    expect(goalWindowSize({ ...picture, width: 100, height: 300 })).toEqual({
      width: 220,
      height: 354,
    });
    expect(goalWindowSize({ ...picture, width: 300, height: 1200 })).toEqual({
      width: 220,
      height: 640,
    });
  });

  it('is titled by its problem', () => {
    expect(goalTitle({ label: 'P2', name: 'Easy' })).toBe('Goal: P2 Easy');
    expect(goalTitle({ label: 'P2', name: '' })).toBe('Goal: P2');
    expect(goalTitle(undefined)).toBe('Goal');
  });
});

describe('a sliding goal’s places', () => {
  it('puts each goal’s piece at the goal’s lowest corner, a copy at a time', () => {
    const bar = [
      [0, 0, 0],
      [1, 0, 0],
    ];
    const dot = [[0, 0, 0]];
    const start = {
      pieces: [
        { piece: 's1', cells: bar },
        { piece: 's2', cells: dot },
        { piece: 's2', cells: dot },
        { piece: 's3', cells: dot },
      ],
      goals: [
        // the bar's goal, and both copies of the dot: the first copy takes the first goal
        {
          piece: 's1',
          cells: [
            [3, 2, 0],
            [2, 2, 0],
          ],
        },
        { piece: 's2', cells: [[4, 1, 0]] },
        { piece: 's2', cells: [[0, 3, 0]] },
      ],
    };
    const got = goalPlaces(start);
    expect([...got.entries()]).toEqual([
      [0, [2, 2, 0]],
      [1, [4, 1, 0]],
      [2, [0, 3, 0]],
    ]);
    // s3 has no goal: it is not in the picture
    expect(got.has(3)).toBe(false);
  });

  it('matches a goal only to a piece whose cells it is', () => {
    const start = {
      pieces: [{ piece: 's1', cells: [[0, 0, 0]] }],
      goals: [
        {
          piece: 's1',
          cells: [
            [0, 0, 0],
            [0, 1, 0],
          ],
        },
      ],
    };
    expect(goalPlaces(start).size).toBe(0);
  });
});
