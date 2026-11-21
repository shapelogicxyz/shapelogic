// SPDX-License-Identifier: GPL-3.0-or-later
//
// A stacking puzzle played by hand (decision 0042): the Play tab in the
// rail for a stacking puzzle, with its own board and the goal's picture; a
// top disc dragged to another rod, or clicked and then the rod; a transfer
// the rules refuse told in the status bar; a disc put straight back taking
// its move off; "Solved!" at the goal; Undo and Redo; the transfers
// exported to a file, imported again and played back; files of another
// board, of a sliding puzzle and with a move that cannot be made refused;
// the position saved as the Start and as the Goal; and a Panex board with
// a disc raised into the bridge.

import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { expect, test } from '@playwright/test';

import { answerDialogs, BUILT, ENGINE, engineCall, launch, openFixture } from './app.ts';
import { carry, clickRod, expectGame, expectStacks, STACK, STAGE, toStackPlay } from './play.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

/** The Tower of Hanoi with three discs: disc 0 the largest, the tower on rod 1, its goal on rod 3. */
const HANOI = 'test_stacking_hanoi.xmpuzzle';

interface Start {
  stacks: { disc: number; z: number }[][];
  goal: { disc: number; z: number }[][] | null;
  solved: boolean;
}

const numbers = (stacks: { disc: number }[][]): number[][] =>
  stacks.map((rod) => rod.map((d) => d.disc));

test('a stacking puzzle is played by hand: drags and clicks, the count, Solved!, undo and redo', async () => {
  const a = await launch();
  const page = a.page;
  try {
    // no Play tab until a puzzle that is played is open
    await expect(page.locator('.rbtn[data-tab=play]')).toHaveCount(0);
    await openFixture(page, HANOI);
    await expect(page.locator('.rbtn[data-tab=play]')).toBeVisible();
    await toStackPlay(page);
    await expectGame(page, 0, 0, false);
    await expectStacks(page, '0,1,2||');
    // the stacking puzzle's board and buttons, not the sliding puzzle's
    await expect(page.locator(STACK)).toBeVisible();
    await expect(page.locator('#playBoard')).toBeHidden();
    await expect(page.locator(`${STACK} .sk-disc`)).toHaveCount(3);
    await expect(page.locator(`${STACK} .sk-peg`)).toHaveCount(3);
    await expect(page.locator('#plShades')).toHaveCount(0);
    await expect(page.locator('#plProbMeta')).toHaveText('3 discs · play to the Goal');
    await expect(page.locator('#plUndo')).toBeDisabled();
    await expect(page.locator('#plSolved')).toBeHidden();
    // the goal's picture beside the board; Hide Goal takes it off, Show Goal brings it back
    await expect(page.locator('#plGoalPic')).toBeVisible();
    await expect(page.locator('#plGoalSvg .sk-disc')).toHaveCount(3);
    await expect(page.locator('#plGoalSvg .sk-disc[data-rod="2"]')).toHaveCount(3);
    await page.click('#plGoalShow');
    await expect(page.locator('#plGoalShow')).toHaveText('Show Goal');
    await expect(page.locator('#plGoalPic')).toBeHidden();
    await page.click('#plGoalShow');
    await expect(page.locator('#plGoalPic')).toBeVisible();
    // the discs' labels: their sizes here, hidden and shown
    await expect(page.locator(`${STACK} .sk-name`).first()).toBeVisible();
    await page.click('#plDiscLabels');
    await expect(page.locator('#plDiscLabels')).toHaveText('Show Disc Labels');
    await expect(page.locator(`${STACK} .sk-name`).first()).toBeHidden();
    await page.click('#plDiscLabels');

    // the smallest disc dragged to rod 3: one move
    await carry(page, 0, 2);
    await expectGame(page, 1, 1, false);
    await expectStacks(page, '0,1||2');
    await expect(page.locator('#plMoves')).toHaveText('1Move');
    await expect(page.locator('#stText')).toContainText('1 move');
    // a larger disc onto it: refused, with the rule that refuses it
    await carry(page, 0, 2);
    await expect(page.locator(STAGE)).toHaveAttribute('data-refused', '1');
    await expect(page.locator('#stText')).toHaveText('A larger disc cannot sit on a smaller one.');
    await expectGame(page, 1, 1, false);
    await expectStacks(page, '0,1||2');
    // put straight back: the move is gone
    await carry(page, 2, 0);
    await expectGame(page, 0, 0, false);
    await expectStacks(page, '0,1,2||');

    // clicked: the disc lifts, and goes onto the rod clicked next
    await clickRod(page, 0);
    await expect(page.locator(STACK)).toHaveAttribute('data-held', '0');
    await expect(page.locator(`${STACK} .sk-disc.held`)).toHaveCount(1);
    await expect(page.locator(`${STACK} .sk-col.ok`)).toHaveCount(2);
    await clickRod(page, 1);
    await expect(page.locator(STACK)).not.toHaveAttribute('data-held');
    await expectGame(page, 1, 1, false);
    await expectStacks(page, '0,1|2|');
    // clicked again on its own rod: put down, no move
    await clickRod(page, 0);
    await expect(page.locator(STACK)).toHaveAttribute('data-held', '0');
    await clickRod(page, 0);
    await expect(page.locator(STACK)).not.toHaveAttribute('data-held');
    await expectGame(page, 1, 1, false);

    // Undo and Redo, the buttons and the keys: the game's, not the document's
    const revision = await page.locator('#stage').getAttribute('data-revision');
    await page.click('#plUndo');
    await expectGame(page, 1, 0, false);
    await expectStacks(page, '0,1,2||');
    await page.keyboard.press('ControlOrMeta+Shift+z');
    await expectGame(page, 1, 1, false);
    await page.keyboard.press('ControlOrMeta+z');
    await expectGame(page, 1, 0, false);
    await expect(page.locator('#stage')).toHaveAttribute('data-revision', revision ?? '');

    // a new move from there drops the one undone; then the other six of the seven
    for (const [from, to] of [
      [0, 2],
      [0, 1],
      [2, 1],
      [0, 2],
      [1, 0],
      [1, 2],
    ] as const) {
      await carry(page, from, to);
    }
    await expectGame(page, 6, 6, false);
    await expect(page.locator('#plSolved')).toBeHidden();
    await carry(page, 0, 2);
    await expectGame(page, 7, 7, true);
    await expectStacks(page, '||0,1,2');
    await expect(page.locator('#plSolved')).toHaveText('Solved! — 7 moves');
    await expect(page.locator('#stText')).toContainText('Solved!');
    // back one move it is not solved; Reset keeps the moves for Redo
    await page.keyboard.press('ArrowLeft');
    await expectGame(page, 7, 6, false);
    await page.click('#plReset');
    await expectGame(page, 7, 0, false);
    await expectStacks(page, '0,1,2||');
  } finally {
    await a.close();
  }
});

test('the transfers of a game are exported, imported and played back; the position is saved', async () => {
  const a = await launch();
  const page = a.page;
  const dir = mkdtempSync(join(tmpdir(), 'bt-e2e-stack-play-'));
  const path = join(dir, 'game.slmoves');
  try {
    await openFixture(page, HANOI);
    await toStackPlay(page);
    const problem = (await page.locator(STAGE).getAttribute('data-problem')) ?? '';
    expect(problem).not.toBe('');
    await expect(page.locator('#plExport')).toBeDisabled();
    await carry(page, 0, 2);
    await carry(page, 0, 1);
    await expectGame(page, 2, 2, false);

    // the transfers as a file: by rod, with the rods and the start's sizes
    await answerDialogs(page, { save: path });
    await page.click('#plExport');
    await expect(page.locator(STAGE)).toHaveAttribute('data-exported', '1');
    const file = JSON.parse(readFileSync(path, 'utf8')) as Record<string, unknown>;
    expect(file).toMatchObject({
      format: 'shapelogic-moves',
      version: 2,
      kind: 'stacking',
      rods: [3, 3, 3],
      panex: false,
      start: [[3, 2, 1], [], []],
      moves: [
        { from: 0, to: 2 },
        { from: 0, to: 1 },
      ],
    });

    // the game begun again, then the file read back: its transfers, from the start
    await page.click('#plRestart');
    await page.click('.cfm [data-x="1"]');
    await expectGame(page, 0, 0, false);
    await page.setInputFiles('#plFile', path);
    await expect(page.locator(STAGE)).toHaveAttribute('data-imported', '1');
    await expectGame(page, 2, 0, false);
    await expectStacks(page, '0,1,2||');
    // played back to the end
    await page.click('#plTransport [data-t="play"]');
    await expectGame(page, 2, 2, false);
    await expect(page.locator(STAGE)).toHaveAttribute('data-playing', 'false');
    await expectStacks(page, '0|1|2');
    // the seekbar goes to any move
    await page.locator('#plSeek').fill('1');
    await expectGame(page, 2, 1, false);
    await expectStacks(page, '0,1||2');

    // files of another board, with a move that cannot be made, of a sliding puzzle and of no kind
    const refused = async (name: string, text: string, says: string): Promise<void> => {
      writeFileSync(join(dir, name), text);
      await page.setInputFiles('#plFile', join(dir, name));
      await expect(page.locator('#notice')).toContainText(says);
      await page.click('#notice [data-x="ok"]');
    };
    await refused('other.slmoves', JSON.stringify({ ...file, rods: [4, 4, 4] }), 'another board');
    await refused(
      'start.slmoves',
      JSON.stringify({ ...file, start: [[3, 2], [1], []] }),
      'another position',
    );
    await refused(
      'blocked.slmoves',
      JSON.stringify({ ...file, moves: [{ from: 1, to: 0 }] }),
      'Move 1 of 1 cannot be made here: Rod 2 is empty.',
    );
    await refused(
      'sliding.slmoves',
      JSON.stringify({ format: 'shapelogic-moves', version: 1, board: {}, pieces: [], moves: [] }),
      'for a sliding puzzle',
    );
    await refused('text.slmoves', 'not json', 'not a ShapeLogic moves file');
    await expectGame(page, 2, 1, false);

    // the position after the first transfer saved as the Start: a new game from there
    await page.click('#plSaveStart');
    await expectGame(page, 0, 0, false);
    await expectStacks(page, '0,1||2');
    let start = await engineCall<Start>(page, 'stacking.playStart', { problem });
    expect(numbers(start.stacks)).toEqual([[0, 1], [], [2]]);
    const history = await engineCall<{ undo: string[] }>(page, 'edit.history');
    expect(history.undo.at(-1)).toBe('Set start position');

    // the position after another transfer saved as the Goal: the goal's picture follows
    await carry(page, 0, 1);
    await expectGame(page, 1, 1, false);
    await page.click('#plSaveGoal');
    await expect(page.locator('#plGoalSvg .sk-disc[data-rod="1"]')).toHaveCount(1);
    await expect(page.locator('#plGoalSvg .sk-disc[data-rod="2"]')).toHaveCount(1);
    start = await engineCall<Start>(page, 'stacking.playStart', { problem });
    expect(numbers(start.goal ?? [])).toEqual([[0], [1], [2]]);
    // the game goes on: its move is still there, and it has reached the new goal
    await expectGame(page, 1, 1, true);
    await expect(page.locator('#plSolved')).toHaveText('Solved! — 1 move');
  } finally {
    await a.close();
  }
});

test('a Panex board is played: a disc rests where its size allows, and a raised one blocks', async () => {
  const a = await launch();
  const page = a.page;
  try {
    // two towers of three on the outer columns of three, to be swapped
    await openFixture(page, 'stacking/stack_panex_swap_3.xmpuzzle');
    await toStackPlay(page);
    await expectGame(page, 0, 0, false);
    await expectStacks(page, '0,1,2||3,4,5');
    // the steps of each column under the bridge, and the rule's note
    await expect(page.locator(`${STACK} .sk-step`)).toHaveCount(9);
    await expect(page.locator(`${STACK} .sk-bridge`)).toHaveCount(1);
    await expect(page.locator('#plNote')).toContainText('Panex columns');
    await expect(page.locator('#plProbMeta')).toHaveText('6 discs · play to the Goal');

    // a small disc onto the empty middle column falls one level below the bridge
    await carry(page, 2, 1);
    await expectStacks(page, '0,1,2|5|3,4');
    await expect(page.locator(`${STACK} .sk-disc[data-i="5"]`)).toHaveAttribute('data-z', '2');
    // the next onto it waits raised in the bridge
    await carry(page, 2, 1);
    await expectStacks(page, '0,1,2|5,4|3');
    await expect(page.locator(`${STACK} .sk-disc[data-i="4"]`)).toHaveAttribute('data-z', '3');
    await expectGame(page, 2, 2, false);
    // and blocks the move that passes over it
    await carry(page, 0, 2);
    await expect(page.locator(STAGE)).toHaveAttribute('data-refused', '1');
    await expect(page.locator('#stText')).toHaveText('The disc raised on rod 2 is in the way.');
    await expectGame(page, 2, 2, false);
    // Undo draws the disc back to where it rested
    await page.click('#plUndo');
    await expectGame(page, 2, 1, false);
    await expectStacks(page, '0,1,2|5|3,4');
    await expect(page.locator(`${STACK} .sk-disc[data-i="4"]`)).toHaveAttribute('data-z', '1');
  } finally {
    await a.close();
  }
});

test('the Goal picture opens the goal, drawn large, in a window of its own', async () => {
  const a = await launch();
  const page = a.page;
  try {
    await openFixture(page, HANOI);
    await toStackPlay(page);
    await expect(page.locator('#plGoalPic .pop svg')).toBeVisible();
    const opened = a.app.waitForEvent('window');
    await page.click('#plGoalPic');
    const goal = await opened;
    await expect(goal).toHaveTitle(/^Goal: P1/);
    await expect(goal.locator('img')).toBeVisible();
    // the drawing, an image: the three discs on rod 3, without the strips that take the pointer
    const src = (await goal.locator('img').getAttribute('src')) ?? '';
    const svg = Buffer.from(src.replace(/^data:image\/svg\+xml;base64,/, ''), 'base64').toString();
    expect(svg.match(/data-rod="2" data-z="/g)).toHaveLength(3);
    expect(svg).not.toContain('<title>');
    expect(svg).not.toContain('class=');
    // a second click brings the same window forward, no new one
    await page.click('#plGoalPic');
    await expect.poll(() => a.app.windows().length).toBe(2);
  } finally {
    await a.close();
  }
});
