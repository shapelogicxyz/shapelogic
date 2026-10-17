// SPDX-License-Identifier: GPL-3.0-or-later
//
// The WebGL2 check at start-up (main/gpu-check.ts): which states go on, which
// start again with the blocklist overridden, which explain.

import { describe, expect, it } from 'vitest';

import {
  GPU_FALLBACK_SWITCHES,
  GPU_RETRY_FLAG,
  gpuAction,
  retryArgs,
  webgl2Usable,
} from '../../src/main/gpu-check.ts';

describe('webgl2Usable', () => {
  it('accepts the enabled states and nothing else', () => {
    expect(webgl2Usable('enabled')).toBe(true);
    expect(webgl2Usable('enabled_force')).toBe(true);
    expect(webgl2Usable('unavailable_software')).toBe(false);
    expect(webgl2Usable('disabled_off')).toBe(false);
    expect(webgl2Usable(undefined)).toBe(false);
  });
});

describe('gpuAction', () => {
  const argv = ['/opt/ShapeLogic/shapelogic'];

  it('goes on when WebGL2 works', () => {
    expect(gpuAction('enabled', argv, 'linux', true)).toBe('continue');
  });

  it('goes on outside a packaged Linux app, whatever the status', () => {
    expect(gpuAction('unavailable_software', argv, 'darwin', true)).toBe('continue');
    expect(gpuAction('unavailable_software', argv, 'win32', true)).toBe('continue');
    expect(gpuAction('unavailable_software', argv, 'linux', false)).toBe('continue');
  });

  it('starts again once when WebGL2 is blocked', () => {
    expect(gpuAction('unavailable_software', argv, 'linux', true)).toBe('retry');
  });

  it('explains when the second try did not help', () => {
    expect(gpuAction('unavailable_software', [...argv, GPU_RETRY_FLAG], 'linux', true)).toBe(
      'explain',
    );
  });

  it('explains instead of overriding what the user already overrode', () => {
    expect(
      gpuAction('unavailable_software', [...argv, '--ignore-gpu-blocklist'], 'linux', true),
    ).toBe('explain');
  });
});

describe('retryArgs', () => {
  it('keeps the arguments, drops the program, adds the overrides and the retry flag', () => {
    expect(retryArgs(['/opt/shapelogic', 'a.xmpuzzle'])).toEqual([
      'a.xmpuzzle',
      ...GPU_FALLBACK_SWITCHES,
      GPU_RETRY_FLAG,
    ]);
  });
});
