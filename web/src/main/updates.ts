// SPDX-License-Identifier: GPL-3.0-or-later
//
// Whether a newer ShapeLogic is out: the latest release on GitHub (its
// API's releases/latest, which skips drafts and prereleases) against the
// app's version, both read as vX.Y[.Z], and its notes (the release's
// body: release/notes/vX.Y.md, Markdown) for the dialog's Details. The main process asks (app-ipc.ts,
// on a session of its own: the window's session lets nothing remote
// through); the renderer shows the answer (renderer/shell/updates.ts).
// Free of Electron (the fetch is given), so the unit tests stub it.

import type { UpdateResult } from '../engine/app-bridge.ts';

/** The repository's latest published release. */
export const RELEASES_API = 'https://api.github.com/repos/shapelogicxyz/shapelogic/releases/latest';

/** How long the check waits for GitHub. */
const TIMEOUT_MS = 10_000;

/** The most of a release's notes passed on (GitHub allows 125,000 characters). */
export const NOTES_MAX = 20_000;

/** The fetch the check uses (Electron's session.fetch, or a stub). */
export type FetchFn = (url: string, init: RequestInit) => Promise<Response>;

/** A version's numbers (`v1.4` is [1, 4, 0]), or null when it is not vX.Y[.Z]. */
export function parseVersion(v: string): [number, number, number] | null {
  const m = /^v?(\d+)\.(\d+)(?:\.(\d+))?$/.exec(v.trim());
  if (m === null) {
    return null;
  }
  return [Number(m[1]), Number(m[2]), Number(m[3] ?? '0')];
}

/** Negative when a is older than b, 0 when the same, positive when newer. */
export function compareVersions(a: readonly number[], b: readonly number[]): number {
  for (let i = 0; i < 3; i++) {
    const d = (a[i] ?? 0) - (b[i] ?? 0);
    if (d !== 0) {
      return d;
    }
  }
  return 0;
}

/** The latest release: its tag (`v1.4`) and notes ('' when it has none); throws when GitHub does not say a tag. */
export async function fetchLatest(fetchFn: FetchFn): Promise<{ tag: string; notes: string }> {
  const r = await fetchFn(RELEASES_API, {
    headers: {
      Accept: 'application/vnd.github+json',
      'User-Agent': 'ShapeLogic update check',
    },
    signal: AbortSignal.timeout(TIMEOUT_MS),
  });
  if (!r.ok) {
    throw new Error(`GitHub answered ${String(r.status)}`);
  }
  const body: unknown = await r.json();
  if (typeof body !== 'object' || body === null) {
    throw new Error('GitHub sent no release');
  }
  const rel = body as Record<string, unknown>;
  if (rel.draft === true || rel.prerelease === true) {
    throw new Error('the latest release is not published');
  }
  const tag = rel.tag_name;
  if (typeof tag !== 'string' || parseVersion(tag) === null) {
    throw new Error(`the latest release's tag is not vX.Y: ${String(tag)}`);
  }
  const notes = typeof rel.body === 'string' ? rel.body.trim().slice(0, NOTES_MAX) : '';
  return { tag: tag.trim(), notes };
}

/** Whether the latest release is newer than `current`; an error (offline, GitHub) is a result too. */
export async function checkForUpdate(current: string, fetchFn: FetchFn): Promise<UpdateResult> {
  const mine = parseVersion(current);
  try {
    const { tag: latest, notes } = await fetchLatest(fetchFn);
    const theirs = parseVersion(latest);
    if (mine === null || theirs === null) {
      return { status: 'error', current, error: `cannot compare ${current} with ${latest}` };
    }
    return compareVersions(theirs, mine) > 0
      ? { status: 'newer', current, latest, notes }
      : { status: 'current', current, latest };
  } catch (e) {
    return { status: 'error', current, error: e instanceof Error ? e.message : String(e) };
  }
}
