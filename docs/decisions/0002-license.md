# 0002: GPL-3.0-or-later

**Status:** accepted (stage 0)

## Context
The ported code is from BurrTools, whose source headers say "GPL version 2 or
later" and whose repository ships the GPLv3 text (COPYING). The web front-end
design by Girish Sharma is published under GPL-3.0.

## Decision
BurrTools Alpha is licensed **GPL-3.0-or-later**. Every source file starts with
`// SPDX-License-Identifier: GPL-3.0-or-later`. COPYING holds the GPLv3 text;
AUTHORS credits the original authors and the front-end designer.

## Consequences
- Compatible with both sources (GPLv2-or-later code may be used under v3).
- Third-party dependencies must be GPLv3-compatible (Catch2: BSL-1.0, zlib:
  zlib licence, nlohmann/json: MIT, all fine).
