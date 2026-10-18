// SPDX-License-Identifier: GPL-3.0-or-later
//
// ESLint with typescript-eslint's strict, type-checked rules, and the
// front end's boundaries (AGENTS.md): src/engine/ is platform-free (Node
// only in node-transport.ts), the renderer never sees Node or Electron.

import js from '@eslint/js';
import { defineConfig } from 'eslint/config';
import tseslint from 'typescript-eslint';

const nodeModules = ['node:*', 'fs', 'path', 'child_process', 'os', 'url', 'electron'];

export default defineConfig(
  {
    ignores: [
      'node_modules/',
      'out/',
      'dist/',
      'src/engine/api.generated.ts',
      'src/demo/engine/*.js',
      'eslint.config.js',
    ],
  },
  js.configs.recommended,
  tseslint.configs.strictTypeChecked,
  tseslint.configs.stylisticTypeChecked,
  {
    languageOptions: {
      parserOptions: {
        project: ['./tsconfig.node.json', './tsconfig.web.json', './tsconfig.e2e.json'],
        tsconfigRootDir: import.meta.dirname,
      },
    },
    rules: {
      '@typescript-eslint/restrict-template-expressions': ['error', { allowNumber: true }],
      '@typescript-eslint/consistent-type-definitions': 'off',
      '@typescript-eslint/no-unnecessary-type-parameters': 'off',
      'no-restricted-globals': ['error', 'name', 'event'],
    },
  },
  {
    files: ['src/engine/**/*.ts'],
    ignores: ['src/engine/node-transport.ts'],
    rules: {
      'no-restricted-imports': [
        'error',
        {
          patterns: [
            { group: nodeModules, message: 'src/engine/ is platform-free (decision 0013)' },
          ],
        },
      ],
      'no-restricted-globals': [
        'error',
        'process',
        'Buffer',
        'require',
        '__dirname',
        'window',
        'document',
      ],
    },
  },
  {
    // src/demo/ (the website's demo) is the renderer over the engine in the
    // page: it may take the host's server (main/engine-ipc.ts, free of
    // Electron), never Node
    files: ['src/demo/**/*.ts'],
    rules: {
      'no-restricted-imports': [
        'error',
        { patterns: [{ group: nodeModules, message: 'the demo runs in a browser' }] },
      ],
      'no-restricted-globals': ['error', 'process', 'Buffer', 'require', '__dirname'],
    },
  },
  {
    files: ['src/renderer/**/*.ts'],
    rules: {
      'no-restricted-imports': [
        'error',
        {
          patterns: [
            {
              group: nodeModules,
              message: 'the renderer never sees Node or Electron (decision 0013)',
            },
            {
              group: ['../main/*', '../preload/*'],
              message: 'the renderer reaches the host only through window.bt',
            },
          ],
        },
      ],
      'no-restricted-globals': ['error', 'process', 'Buffer', 'require', '__dirname'],
    },
  },
);
