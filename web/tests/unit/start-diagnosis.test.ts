// SPDX-License-Identifier: GPL-3.0-or-later
//
// A failed engine start keeps every detail and says the likely cause
// (cleanup item 2.2): the cause table, the Windows exit codes, the log's
// text, and the transport's failures from a real launch (a missing file, a
// file that may not be run).

import { chmodSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { afterAll, describe, expect, it } from 'vitest';

import { engineVersion, NodeTransport } from '../../src/engine/node-transport.ts';
import { EngineStartError, failureOf } from '../../src/engine/spawn-failure.ts';
import type { SpawnFailure } from '../../src/engine/spawn-failure.ts';
import {
  exitCodeText,
  failureText,
  likelyCause,
  stderrLine,
} from '../../src/engine/start-diagnosis.ts';

const failure = (f: Partial<SpawnFailure>): SpawnFailure => ({
  which: 'version',
  code: 'UNKNOWN',
  message: 'spawn UNKNOWN',
  afterMs: 120,
  ...f,
});

describe('the likely cause of a failed start', () => {
  it('names security software for Windows UNKNOWN', () => {
    expect(likelyCause(failure({}), 'win32')).toMatch(/security software.*Smart App Control/s);
  });

  it('has a cause for each code the table knows', () => {
    for (const [code, words] of [
      ['ENOENT', /missing.*quarantined/],
      ['NO_ENGINE', /no engine file/],
      ['EACCES', /denied running the engine/],
      ['EPERM', /denied running the engine/],
      ['EBUSY', /locked.*antivirus/],
      ['TIMEOUT', /did not answer within 10 seconds/],
      ['VERSION', /another version.*reinstall/],
    ] as const) {
      expect(likelyCause(failure({ code }), 'win32')).toMatch(words);
    }
  });

  it('has none for a code it does not know', () => {
    expect(likelyCause(failure({ code: 'E2BIG' }), 'win32')).toBeNull();
  });

  it('decodes the Windows status an engine ended with before its hello', () => {
    const ended = (exitCode: number): string | null =>
      likelyCause(failure({ code: 'EXITED', which: 'session', exitCode }), 'win32');
    expect(ended(3221225781)).toMatch(/library .* missing/);
    expect(ended(3221225595)).toMatch(/damaged, or built for another processor/);
    expect(ended(3221225794)).toMatch(/failed to start/);
    expect(ended(3221225477)).toMatch(/crashed/);
    expect(ended(3221225501)).toMatch(/instruction this processor lacks/);
    expect(ended(3221226505)).toMatch(/broken check/);
    expect(ended(3221225506)).toMatch(/denied/);
    expect(ended(3221226536)).toMatch(/signature/);
    // the same status read as signed
    expect(ended(3221225781 - 2 ** 32)).toMatch(/library .* missing/);
    // not decoded elsewhere, nor an ordinary status
    expect(likelyCause(failure({ code: 'EXITED', exitCode: 3221225477 }), 'linux')).toBeNull();
    expect(ended(1)).toBeNull();
  });

  it('writes an exit status in decimal and hex', () => {
    expect(exitCodeText(3221225781, 'win32')).toBe(
      'exit code 3221225781 (0xC0000135): a library the engine needs is missing',
    );
    expect(exitCodeText(2, 'darwin')).toBe('exit code 2 (0x00000002)');
  });
});

describe("the log's text of a failed start", () => {
  it('says the error, its fields, which launch, the path, the time and the cause', () => {
    const f = failure({
      errno: -4094,
      syscall: 'spawn',
      path: 'C:\\ShapeLogic\\resources\\shapelogic_engine.exe',
    });
    expect(failureText('spawn UNKNOWN', f, 'win32')).toBe(
      'spawn UNKNOWN (errno -4094, syscall spawn, the version check) at ' +
        'C:\\ShapeLogic\\resources\\shapelogic_engine.exe, 120 ms after start. Likely cause: ' +
        (likelyCause(f, 'win32') ?? ''),
    );
  });

  it('is the reason alone without a failure', () => {
    expect(failureText('not started', undefined, 'win32')).toBe('not started');
  });

  it("puts the engine's last words on one line", () => {
    expect(stderrLine(failure({ stderr: 'one\ntwo\n' }))).toBe('one | two');
    expect(stderrLine(failure({}))).toBeNull();
  });
});

describe("the transport's failed launches", () => {
  const dir = mkdtempSync(join(tmpdir(), 'sl-start-'));
  afterAll(() => {
    rmSync(dir, { recursive: true, force: true });
  });

  it('a missing file: ENOENT, in the version check', async () => {
    const path = join(dir, 'no-such-engine');
    const e = await engineVersion(path).catch((x: unknown) => x);
    expect(e).toBeInstanceOf(EngineStartError);
    const f = failureOf(e);
    expect(f).toMatchObject({ which: 'version', code: 'ENOENT', path });
    expect(f?.afterMs).toBeGreaterThanOrEqual(0);
  });

  it.skipIf(process.platform === 'win32')(
    'a file that may not be run: EACCES, in the version check',
    async () => {
      const path = join(dir, 'not-runnable');
      writeFileSync(path, '#!/bin/sh\necho hi\n');
      chmodSync(path, 0o644);
      const f = failureOf(await NodeTransport.start(path).catch((x: unknown) => x));
      expect(f).toMatchObject({ which: 'version', code: 'EACCES' });
    },
  );

  it.skipIf(process.platform === 'win32')('a program that is not the engine: VERSION', async () => {
    const path = join(dir, 'other');
    writeFileSync(path, '#!/bin/sh\necho something else\n');
    chmodSync(path, 0o755);
    const f = failureOf(await NodeTransport.start(path).catch((x: unknown) => x));
    expect(f).toMatchObject({ which: 'version', code: 'VERSION' });
  });

  it.skipIf(process.platform === 'win32')(
    'a version check that fails: EXITED with its status and stderr',
    async () => {
      const path = join(dir, 'fails');
      writeFileSync(path, '#!/bin/sh\necho broken >&2\nexit 7\n');
      chmodSync(path, 0o755);
      const f = failureOf(await NodeTransport.start(path).catch((x: unknown) => x));
      expect(f).toMatchObject({ which: 'version', code: 'EXITED', exitCode: 7 });
      expect(f?.stderr).toContain('broken');
    },
  );
});
