// SPDX-License-Identifier: GPL-3.0-or-later
//
// The WebGL2 check at start-up (main/gpu-check.ts): which states go on, which
// ask a page for a real context, and what a page without one leads to.

import { describe, expect, it } from 'vitest';

import {
  GPU_FALLBACK_SWITCHES,
  GPU_RETRY_FLAG,
  gpuAction,
  probeFailure,
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

  // with SwiftShader the status is 'unavailable_software' though a context
  // works, and some virtual machines report none
  it('asks a page for a real context when WebGL2 is not reported enabled', () => {
    expect(gpuAction('unavailable_software', argv, 'linux', true)).toBe('probe');
    expect(gpuAction(undefined, argv, 'linux', true)).toBe('probe');
    expect(gpuAction(undefined, [...argv, GPU_RETRY_FLAG], 'linux', true)).toBe('probe');
  });
});

describe('probeFailure', () => {
  const argv = ['/opt/ShapeLogic/shapelogic'];

  it('starts again once with the overrides', () => {
    expect(probeFailure(argv)).toBe('retry');
  });

  it('explains when the second try got no context either', () => {
    expect(probeFailure([...argv, GPU_RETRY_FLAG])).toBe('explain');
  });

  it('explains instead of overriding what the user already overrode', () => {
    expect(probeFailure([...argv, '--ignore-gpu-blocklist'])).toBe('explain');
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
