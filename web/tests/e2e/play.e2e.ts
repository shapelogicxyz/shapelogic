// SPDX-License-Identifier: GPL-3.0-or-later
//
// A sliding puzzle played by hand (decision 0039): the Play tab in the
// rail only for a sliding puzzle; a piece dragged over the board, round a
// corner in one move and never through another piece; the same piece
// dragged again going on with its move; "Solved!" at the goal; Undo and
// Redo beside the board and on Ctrl+Z (the game's, not the document's);
// the moves exported to a file, imported again and played back; a file of
// another board refused; the position saved as the Start and as the Goal;
// with nested slides a piece carrying what is nested in it, and moving
// what it is nested in when pushed into its walls.

import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import {
  answerDialogs,
  BUILT,
  ENGINE,
  engineCall,
  launch,
  newDocument,
  openFixture,
} from './app.ts';
import { drag, expectGame, spaceThrice, STAGE, toPlay } from './play.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

interface Board {
  problem: string;
  tray: string;
  one: string;
  two: string;
}

interface Start {
  pieces: { piece: string; at: number[] }[];
  solved: boolean;
}

/**
 * A 3x2 board: a domino along the bottom left, a unit above its left cell
 * whose goal is the top right corner.
 */
async function build(page: Page): Promise<Board> {
  await newDocument(page, 'sliding');
  const made = async (size: number[], cells: number[][]): Promise<string> => {
    const { shape } = await engineCall<{ shape: string }>(page, 'shape.create', { size });
    await engineCall(page, 'shape.setCells', { shape, tool: 'fixed', cells });
    return shape;
  };
  const one = await made([1, 1, 1], [[0, 0, 0]]);
  const two = await made(
    [2, 1, 1],
    [
      [0, 0, 0],
      [1, 0, 0],
    ],
  );
  const tray = (await engineCall<{ shape: string }>(page, 'sliding.addStartGoal', { size: [3, 2] }))
    .shape;
  const { problem } = await engineCall<{ problem: string }>(page, 'problem.create');
  await engineCall(page, 'problem.setResult', { problem, shape: tray });
  await engineCall(page, 'sliding.place', { tray, problem, piece: two, at: [0, 0], goal: false });
  await engineCall(page, 'sliding.place', { tray, problem, piece: one, at: [0, 1], goal: false });
  const last = await engineCall<{ revision: number }>(page, 'sliding.place', {
    tray,
    problem,
    piece: one,
    at: [2, 1],
    goal: true,
  });
  await expect(page.locator('#stage')).toHaveAttribute('data-revision', String(last.revision));
  return { problem, tray, one, two };
}

test('a sliding puzzle is played by hand: drags, the count, Solved!, undo and redo', async () => {
  const a = await launch();
  const page = a.page;
  try {
    // no Play tab until a sliding puzzle is open
    await expect(page.locator('.rbtn[data-tab=play]')).toHaveCount(0);
    const b = await build(page);
    await expect(page.locator('.rbtn[data-tab=play]')).toBeVisible();
    await toPlay(page);
    await expect(page.locator(STAGE)).toHaveAttribute('data-problem', b.problem);
    await expectGame(page, 0, 0, false);
    await expect(page.locator('.pl-piece')).toHaveCount(2);
    await expect(page.locator('.pl-cell')).toHaveCount(6);
    await expect(page.locator('.pl-goal')).toHaveCount(1);
    await expect(page.locator('#plSolved')).toBeHidden();
    await expect(page.locator('#plUndo')).toBeDisabled();
    await expect(page.locator('#plRedo')).toBeDisabled();
    await expect(page.locator('#plReset')).toBeDisabled();
    await expect(page.locator('#plReset')).toHaveText('Reset');
    // Hide Goal Shadows leaves the goal's tint off the floor, Hide Goal Lines its outlines; Show brings each back
    await expect(page.locator('#plShades')).toHaveText('Hide Goal Shadows');
    await expect(page.locator('#plLines')).toHaveText('Hide Goal Lines');
    await expect(page.locator('.pl-goal')).toBeVisible();
    await expect(page.locator('.pl-mark')).toHaveCount(2);
    await page.click('#plShades');
    await expect(page.locator('#plShades')).toHaveText('Show Goal Shadows');
    await expect(page.locator('.pl-goal')).toBeHidden();
    await expect(page.locator('.pl-mark').first()).toBeVisible();
    await page.click('#plLines');
    await expect(page.locator('#plLines')).toHaveText('Show Goal Lines');
    await expect(page.locator('.pl-mark').first()).toBeHidden();
    await page.click('#plShades');
    await expect(page.locator('#plShades')).toHaveText('Hide Goal Shadows');
    await expect(page.locator('.pl-goal')).toBeVisible();
    await expect(page.locator('.pl-mark').first()).toBeHidden();
    await page.click('#plLines');
    await expect(page.locator('#plLines')).toHaveText('Hide Goal Lines');
    await expect(page.locator('.pl-mark').first()).toBeVisible();
    // Hide Grid leaves the lines between the floor's cells off; Show Grid brings them back
    await expect(page.locator('#plGrid')).toHaveText('Hide Grid');
    await expect(page.locator('.pl-grid').first()).toBeVisible();
    await page.click('#plGrid');
    await expect(page.locator('#plGrid')).toHaveText('Show Grid');
    await expect(page.locator('.pl-grid').first()).toBeHidden();
    await page.click('#plGrid');
    await expect(page.locator('#plGrid')).toHaveText('Hide Grid');
    await expect(page.locator('.pl-grid').first()).toBeVisible();
    // Hide Piece Labels leaves the pieces' names off; Show Piece Labels brings them back (these pieces have none)
    await expect(page.locator('#plLabels')).toHaveText('Hide Piece Labels');
    await page.click('#plLabels');
    await expect(page.locator('#plLabels')).toHaveText('Show Piece Labels');
    await expect(page.locator('#playBoard')).toHaveClass(/nolabels/);
    await page.click('#plLabels');
    await expect(page.locator('#plLabels')).toHaveText('Hide Piece Labels');
    await expect(page.locator('#playBoard')).not.toHaveClass(/nolabels/);
    // Simple Pieces draws flat square pieces and tray; 3D Pieces brings the sides back
    await expect(page.locator('#plSimple')).toHaveText('Simple Pieces');
    await page.click('#plSimple');
    await expect(page.locator('#plSimple')).toHaveText('3D Pieces');
    await expect(page.locator('#playBoard')).toHaveClass(/simple/);
    await expect(page.locator('.pl-piece')).toHaveCount(2);
    await expect(page.locator('.pl-piece use')).toHaveCount(0);
    await page.click('#plSimple');
    await expect(page.locator('#plSimple')).toHaveText('Simple Pieces');
    await expect(page.locator('#playBoard')).not.toHaveClass(/simple/);
    // the moves made, beside the board
    await expect(page.locator('#plMoves')).toHaveText('0Moves');
    // the 3D view is out of sight under the board, and the Shapes cards are gone
    await expect(page.locator('#left')).toBeHidden();
    await expect(page.locator('#leftPl')).toBeVisible();

    // the domino cannot go up: the unit is in its way
    await drag(page, '0,0,0', [[0, 1]]);
    await expectGame(page, 0, 0, false);
    await expect(page.locator('.pl-piece[data-at="0,0,0"]')).toHaveCount(1);

    // the unit along the top row and round the corner: one move
    await drag(page, '0,1,0', [
      [2, 0],
      [0, -1],
    ]);
    await expectGame(page, 1, 1, false);
    await expect(page.locator('.pl-piece[data-at="2,0,0"]')).toHaveCount(1);
    await expect(page.locator('#plCount')).toHaveText('1 move');
    await expect(page.locator('#plMoves')).toHaveText('1Move');

    // dragged again straight away, it goes on with its move: still one, and at its goal
    await drag(page, '2,0,0', [[0, 1]]);
    await expectGame(page, 1, 1, true);
    await expect(page.locator('#plSolved')).toBeVisible();
    await expect(page.locator('#plSolved')).toHaveText('Solved! — 1 move');

    // Undo and Redo beside the board
    const revision = await page.locator('#stage').getAttribute('data-revision');
    await page.click('#plUndo');
    await expectGame(page, 1, 0, false);
    await expect(page.locator('.pl-piece[data-at="0,1,0"]')).toHaveCount(1);
    await expect(page.locator('#plSolved')).toBeHidden();
    await expect(page.locator('#plUndo')).toBeDisabled();
    await page.click('#plRedo');
    await expectGame(page, 1, 1, true);
    // Reset puts the pieces back at the Start and keeps the move for Redo
    await page.click('#plReset');
    await expectGame(page, 1, 0, false);
    await expect(page.locator('#plReset')).toBeDisabled();
    await page.click('#plRedo');
    await expectGame(page, 1, 1, true);
    // the keys are the game's here: the document is left alone
    await page.locator('#playBoard').focus();
    await page.keyboard.press('ControlOrMeta+z');
    await expectGame(page, 1, 0, false);
    await page.keyboard.press('ControlOrMeta+Shift+z');
    await expectGame(page, 1, 1, true);
    await page.keyboard.press('ArrowLeft');
    await expectGame(page, 1, 0, false);
    await page.keyboard.press('ArrowRight');
    await expectGame(page, 1, 1, true);
    await expect(page.locator('#stage')).toHaveAttribute('data-revision', revision ?? '');
    const names = await engineCall<{ undo: string[] }>(page, 'edit.history');
    expect(names.undo.at(-1)).toBe('Place goal');

    // another piece is another move; the unit taken off its goal and put back leaves the count
    await drag(page, '0,0,0', [[1, 0]]);
    await expectGame(page, 2, 2, true);
    await drag(page, '2,1,0', [[-1, 0]]);
    await expectGame(page, 3, 3, false);
    await drag(page, '1,1,0', [[1, 0]]);
    await expectGame(page, 2, 2, true);
    await expect(page.locator('#plPos')).toHaveText('Move 2 of 2');

    // a move made after an Undo takes the place of the moves undone
    await page.click('#plUndo');
    await expectGame(page, 2, 1, true);
    await drag(page, '0,0,0', [[1, 0]]);
    await expectGame(page, 2, 2, true);

    // another puzzle type has no Play tab: the Shapes tab is shown
    await newDocument(page, 'bricks');
    await expect(page.locator('.rbtn[data-tab=play]')).toHaveCount(0);
    await expect(page.locator('#stage')).toHaveAttribute('data-tab', 'entities');
  } finally {
    await a.close();
  }
});

test('the moves of a game are exported, imported and played back; the position is saved', async () => {
  const a = await launch();
  const page = a.page;
  const dir = mkdtempSync(join(tmpdir(), 'bt-e2e-play-'));
  const path = join(dir, 'game.slmoves');
  try {
    const b = await build(page);
    await toPlay(page);
    await expect(page.locator('#plExport')).toBeDisabled();
    await drag(page, '0,1,0', [[2, 0]]);
    await drag(page, '0,0,0', [[1, 0]]);
    await expectGame(page, 2, 2, true);

    // the moves as a file
    await answerDialogs(page, { save: path });
    await page.click('#plExport');
    await expect(page.locator(STAGE)).toHaveAttribute('data-exported', '1');
    const file = JSON.parse(readFileSync(path, 'utf8')) as {
      format: string;
      version: number;
      board: { size: number[]; cells: string };
      pieces: unknown[];
      moves: { movers: number[]; by: number[] }[];
    };
    expect(file.format).toBe('shapelogic-moves');
    expect(file.version).toBe(1);
    expect(file.board).toEqual({ size: [3, 2, 1], cells: '111111' });
    expect(file.pieces).toHaveLength(2);
    expect(file.moves.map((m) => m.by)).toEqual([
      [2, 0, 0],
      [1, 0, 0],
    ]);

    // the game begun again, then the file read back: its moves, from the start
    await page.click('#plRestart');
    await page.click('#confirm [data-x="ok"]');
    await expectGame(page, 0, 0, false);
    await page.setInputFiles('#plFile', path);
    await expect(page.locator(STAGE)).toHaveAttribute('data-imported', '1');
    await expectGame(page, 2, 0, false);
    await expect(page.locator('.pl-piece[data-at="0,1,0"]')).toHaveCount(1);
    // played back to the end
    await page.click('#plTransport [data-t="play"]');
    await expectGame(page, 2, 2, true);
    await expect(page.locator(STAGE)).toHaveAttribute('data-playing', 'false');
    await expect(page.locator('#plSolved')).toHaveText('Solved! — 2 moves');
    // the seekbar goes to any move
    await page.locator('#plSeek').fill('1');
    await expectGame(page, 2, 1, true);
    await page.click('#plTransport [data-t="start"]');
    await expectGame(page, 2, 0, false);

    // Space three times at once (play, pause, play): one playback, each move
    // drawn once and in order (cleanup item 6.6: the paused one went on beside
    // the new); a game of eight moves, the first one there and back
    const first = file.moves[0];
    const there = join(dir, 'there.slmoves');
    writeFileSync(
      there,
      JSON.stringify({
        ...file,
        moves: [0, 1, 2, 3, 4, 5, 6, 7].map((i) => ({
          movers: first?.movers,
          by: i % 2 === 0 ? [2, 0, 0] : [-2, 0, 0],
        })),
      }),
    );
    await page.setInputFiles('#plFile', there);
    // the moves made here are replaced, asked first
    await page.click('#confirm [data-x="ok"]');
    await expect(page.locator(STAGE)).toHaveAttribute('data-imported', '2');
    await expectGame(page, 8, 0, false);
    await spaceThrice(page, 8);
    // the game of two moves again
    await page.setInputFiles('#plFile', path);
    // the moves made here are replaced, asked first
    await page.click('#confirm [data-x="ok"]');
    await expect(page.locator(STAGE)).toHaveAttribute('data-imported', '3');
    await expectGame(page, 2, 0, false);

    // a file of another board, and one whose move cannot be made, are refused
    const other = join(dir, 'other.slmoves');
    writeFileSync(other, JSON.stringify({ ...file, board: { size: [2, 3, 1], cells: '111111' } }));
    await page.setInputFiles('#plFile', other);
    await expect(page.locator('#notice')).toContainText('another board');
    await page.click('#notice [data-x="ok"]');
    const blocked = join(dir, 'blocked.slmoves');
    writeFileSync(
      blocked,
      JSON.stringify({ ...file, moves: [{ movers: file.moves[1]?.movers, by: [0, 1, 0] }] }),
    );
    await page.setInputFiles('#plFile', blocked);
    await expect(page.locator('#notice')).toContainText('Move 1 of 1 cannot be made here');
    await page.click('#notice [data-x="ok"]');
    writeFileSync(join(dir, 'text.slmoves'), 'not json');
    await page.setInputFiles('#plFile', join(dir, 'text.slmoves'));
    await expect(page.locator('#notice')).toContainText('not a ShapeLogic moves file');
    await page.click('#notice [data-x="ok"]');
    await expectGame(page, 2, 0, false);

    // the position after the first move saved as the Start: a new game from there
    await page.click('#plTransport [data-t="fwd"]');
    await expectGame(page, 2, 1, true);
    await page.click('#plSaveStart');
    await expectGame(page, 0, 0, true);
    await expect(page.locator('#plSolved')).toHaveText('The Start is the Goal already');
    const start = await engineCall<Start>(page, 'sliding.playStart', { problem: b.problem });
    const unit = start.pieces.find((p) => p.piece === b.one);
    expect(unit?.at).toEqual([2, 1, 0]);
    expect(start.solved).toBe(true);
    const history = await engineCall<{ undo: string[] }>(page, 'edit.history');
    expect(history.undo.at(-1)).toBe('Set start position');

    // the position after another move saved as the Goal: every piece's goal where it is
    await drag(page, '0,0,0', [[1, 0]]);
    await expectGame(page, 1, 1, true);
    await page.click('#plSaveGoal');
    await expect(page.locator('.pl-goal')).toHaveCount(3);
    const tray = await engineCall<{ goals: (string | null)[] }>(page, 'sliding.tray', {
      tray: b.tray,
      problem: b.problem,
    });
    expect(tray.goals).toEqual([null, b.two, b.two, null, null, b.one]);
    // the game goes on: its move is still there
    await expectGame(page, 1, 1, true);
  } finally {
    await a.close();
  }
});

test('with nested slides a piece carries what is nested in it, moves what it is in, or moves alone', async () => {
  const a = await launch();
  const page = a.page;
  try {
    // the Cave with the Smile in its pocket (the nested fixture): the Cave
    // can go right alone, leaving the Smile, or carry it
    await openFixture(page, 'sliding/test_sliding_nested.xmpuzzle');
    const snap = await engineCall<{ problems: { id: string }[] }>(page, 'document.get');
    const problem = snap.problems[0]?.id ?? '';
    await toPlay(page);
    await expect(page.locator('.pl-piece')).toHaveCount(6);
    // their names are written on them
    await expect(page.locator('.pl-name')).toHaveText(['1', '2', '3', '4', 'Cave', 'Smile']);
    await expect(page.locator('#plNote')).toHaveText('');

    // the Smile cannot leave the pocket
    await drag(page, '3,4,0', [[-1, 0]]);
    await drag(page, '3,4,0', [[0, 1]]);
    await expectGame(page, 0, 0, false);
    // without nested slides Shift changes nothing: the Cave goes alone
    await drag(page, '3,3,0', [[1, 0]], { shift: true });
    await expectGame(page, 1, 1, false);
    await expect(page.locator('.pl-piece[data-at="4,3,0"]')).toHaveCount(1);
    await expect(page.locator('.pl-piece[data-at="3,4,0"]')).toHaveCount(1);
    await page.click('#plUndo');
    await expectGame(page, 1, 0, false);

    // nested slides stored with the problem: the game is played by them
    const set = await engineCall<{ revision: number }>(page, 'problem.setSolverSettings', {
      problem,
      settings: { nestedSlides: true },
    });
    await expect(page.locator(STAGE)).toHaveAttribute('data-revision', String(set.revision));
    await expect(page.locator('#plNote')).toContainText('Nested slides');
    // the document changed under the game: its move was made again and is still there
    await expectGame(page, 1, 0, false);

    // the Smile pushed into the Cave's wall takes the Cave along: the Cave's move
    await drag(page, '3,4,0', [[2, 0]]);
    await expectGame(page, 1, 1, false);
    await expect(page.locator('.pl-piece[data-at="5,3,0"]')).toHaveCount(1);
    await expect(page.locator('.pl-piece[data-at="5,4,0"]')).toHaveCount(1);
    await page.click('#plUndo');
    await expectGame(page, 1, 0, false);

    // a plain drag still takes the Cave alone where it can go alone
    await drag(page, '3,3,0', [[2, 0]]);
    await expectGame(page, 1, 1, false);
    await expect(page.locator('.pl-piece[data-at="5,3,0"]')).toHaveCount(1);
    await expect(page.locator('.pl-piece[data-at="3,4,0"]')).toHaveCount(1);
    await page.click('#plUndo');
    await expectGame(page, 1, 0, false);
    // Shift: it carries the Smile, and the two are one move
    await drag(page, '3,3,0', [[2, 0]], { shift: true });
    await expectGame(page, 1, 1, false);
    await expect(page.locator('.pl-piece[data-at="5,3,0"]')).toHaveCount(1);
    await expect(page.locator('.pl-piece[data-at="5,4,0"]')).toHaveCount(1);
    // the Cave alone again is another move, the group's own again goes on with it
    await drag(page, '5,3,0', [[1, 0]]);
    await expectGame(page, 2, 2, false);
    await expect(page.locator('.pl-piece[data-at="5,4,0"]')).toHaveCount(1);
  } finally {
    await a.close();
  }
});

test('Show Goal Image opens the board with each piece on its goal in a window of its own', async () => {
  const a = await launch();
  const page = a.page;
  try {
    await build(page);
    await toPlay(page);
    await expect(page.locator('#plGoalWin')).toHaveText('Show Goal Image');
    await expect(page.locator('#plGoalWin')).toBeEnabled();
    const opened = a.app.waitForEvent('window');
    await page.click('#plGoalWin');
    const goal = await opened;
    await expect(goal).toHaveTitle(/^Goal: P1/);
    await expect(goal.locator('img')).toBeVisible();
    // the drawing, an image: only the piece with a goal, standing on it, its styles written in
    const src = (await goal.locator('img').getAttribute('src')) ?? '';
    const svg = Buffer.from(src.replace(/^data:image\/svg\+xml;base64,/, ''), 'base64').toString();
    expect(svg).toMatch(/^<svg /);
    const pieces = [...svg.matchAll(/<g data-i="\d+"[^>]*data-at="([^"]+)"/g)].map((m) => m[1]);
    expect(pieces).toEqual(['2,1,0']);
    // drawn where the board draws its goal's cell (the piece is one cell: its
    // group's corner is the cell's lower left, y down)
    const at = /<g data-i="\d+"[^>]*translate\(([-\d.]+)px,\s*([-\d.]+)px\)/.exec(svg);
    const cell = await page.locator('#playBoard .pl-goal[data-at="2,1,0"]').evaluate((r) => ({
      x: Number(r.getAttribute('x')),
      y: Number(r.getAttribute('y')),
      sc: Number(r.closest('svg')?.dataset.cell),
    }));
    expect(Number(at?.[1])).toBeCloseTo(cell.x, 1);
    expect(Number(at?.[2])).toBeCloseTo(cell.y + cell.sc, 1);
    expect(svg).not.toContain('class=');
    expect(svg).not.toContain('<script');
    // the window goes with the app's: the menus still act on the app window
    expect(a.app.windows()).toHaveLength(2);
  } finally {
    await a.close();
  }
});
