// SPDX-License-Identifier: GPL-3.0-or-later
//
// The update check (src/main/updates.ts): versions read as vX.Y[.Z] and
// compared by number, and GitHub's answer (a stub here) turned into newer,
// current or an error, never a throw.

import { describe, expect, it } from 'vitest';

import {
  checkForUpdate,
  compareVersions,
  parseVersion,
  RELEASES_API,
} from '../../src/main/updates.ts';
import type { FetchFn } from '../../src/main/updates.ts';

const answer =
  (body: unknown, status = 200): FetchFn =>
  () =>
    Promise.resolve(Response.json(body, { status }));

describe('versions', () => {
  it('read tags and the app’s version alike', () => {
    expect(parseVersion('v1.4')).toEqual([1, 4, 0]);
    expect(parseVersion('1.3.0')).toEqual([1, 3, 0]);
    expect(parseVersion('v2.10.3')).toEqual([2, 10, 3]);
    expect(parseVersion('v1')).toBeNull();
    expect(parseVersion('v1.4-beta')).toBeNull();
    expect(parseVersion('')).toBeNull();
  });

  it('compare by number, not as text', () => {
    const v = (s: string): [number, number, number] => parseVersion(s) ?? [0, 0, 0];
    expect(compareVersions(v('v1.10'), v('v1.9'))).toBeGreaterThan(0);
    expect(compareVersions(v('v1.3'), v('1.3.0'))).toBe(0);
    expect(compareVersions(v('1.3.0'), v('v1.4'))).toBeLessThan(0);
    expect(compareVersions(v('v2.0'), v('1.99.99'))).toBeGreaterThan(0);
  });
});

describe('the check', () => {
  it('asks GitHub’s latest release, as JSON', async () => {
    const asked: { url: string; init: RequestInit }[] = [];
    const fetchFn: FetchFn = (url, init) => {
      asked.push({ url, init });
      return Promise.resolve(Response.json({ tag_name: 'v1.3' }));
    };
    await checkForUpdate('1.3.0', fetchFn);
    expect(asked).toHaveLength(1);
    expect(asked[0]?.url).toBe(RELEASES_API);
    expect(asked[0]?.init.headers).toMatchObject({ Accept: 'application/vnd.github+json' });
    expect(asked[0]?.init.signal).toBeInstanceOf(AbortSignal);
  });

  it('finds a newer release', async () => {
    expect(await checkForUpdate('1.3.0', answer({ tag_name: 'v1.4' }))).toEqual({
      status: 'newer',
      current: '1.3.0',
      latest: 'v1.4',
    });
  });

  it('finds this one, or an older one, current', async () => {
    expect(await checkForUpdate('1.3.0', answer({ tag_name: 'v1.3' }))).toMatchObject({
      status: 'current',
      latest: 'v1.3',
    });
    expect(await checkForUpdate('1.4.0', answer({ tag_name: 'v1.3' }))).toMatchObject({
      status: 'current',
    });
  });

  it('turns every failure into an error', async () => {
    const rateLimited = await checkForUpdate('1.3.0', answer({ message: 'rate limit' }, 403));
    expect(rateLimited).toMatchObject({ status: 'error', error: 'GitHub answered 403' });
    const notJson: FetchFn = () => Promise.resolve(new Response('<html>'));
    expect(await checkForUpdate('1.3.0', notJson)).toMatchObject({ status: 'error' });
    const offline: FetchFn = () => Promise.reject(new TypeError('fetch failed'));
    expect(await checkForUpdate('1.3.0', offline)).toMatchObject({
      status: 'error',
      error: 'fetch failed',
    });
    expect(await checkForUpdate('1.3.0', answer({ tag_name: 'nightly' }))).toMatchObject({
      status: 'error',
    });
    expect(await checkForUpdate('dev', answer({ tag_name: 'v1.4' }))).toMatchObject({
      status: 'error',
    });
  });

  it('never offers a draft or a prerelease', async () => {
    expect(
      await checkForUpdate('1.3.0', answer({ tag_name: 'v1.4', prerelease: true })),
    ).toMatchObject({ status: 'error' });
    expect(await checkForUpdate('1.3.0', answer({ tag_name: 'v1.4', draft: true }))).toMatchObject({
      status: 'error',
    });
  });
});
