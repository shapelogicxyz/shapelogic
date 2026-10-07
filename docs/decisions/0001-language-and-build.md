# 0001: C++23, CMake and Ninja

**Status:** accepted (stage 0)

## Context
The core is about 57k lines of performance-critical C++ ported from BurrTools.
It must build on macOS (arm64, x86-64), Linux x86-64 and Windows (MinGW), and
later also compile to WebAssembly for an optional browser version.

## Decision
- **C++23** (`std::expected`, `std::format` where available, ranges).
  Minimum compilers: Apple clang 17, GCC 13, MinGW-w64 GCC 13, Emscripten 3.1.
- **CMake 3.28+ with presets** and the Ninja generator. Presets: `dev`,
  `release`, `asan`, `tsan`. Build trees in `build/<preset>`.
- Dependencies come through `FetchContent`, pinned to an exact release and
  hash, and are added in the stage that first needs them (Catch2 now, zlib in
  stage 2, nlohmann/json in stage 7).
- `just` wraps the common commands; the justfile is the documented interface.
- Warnings are strict (`cmake/Warnings.cmake`) and are errors in `dev` and
  `release`. Third-party code is never compiled with them.

## Consequences
- The language stays the same as the code being ported, so porting is mostly
  mechanical and the algorithms keep their behaviour (see 0007).
- CMake has first-class Emscripten and Node add-on support should either be
  wanted later. The original used Meson; nothing is shared with it.
