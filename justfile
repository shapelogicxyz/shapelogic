# ShapeLogic. `just` lists the recipes.
# Builds go to build/<preset> (see CMakePresets.json).

set shell := ["bash", "-uc"]

# The original BurrTools checkout the comparison harness runs against.
old_repo := env_var_or_default("BT_OLD_REPO", justfile_directory() + "/../burr-tools")

default:
    @just --list

# Configure and build a preset (dev, release, asan, tsan); one build of a preset at a time (build/<preset>.lock)
build preset="dev":
    #!/usr/bin/env bash
    set -euo pipefail
    # Sessions side by side share build/<preset>: a second build of the
    # preset waits for the first (a mkdir lock, as macOS has no flock). A
    # lock whose holder is gone, or that never got its holder's pid, is
    # taken over.
    mkdir -p build
    lock="build/{{preset}}.lock"
    said=""
    until mkdir "$lock" 2>/dev/null; do
      holder=$(cat "$lock/pid" 2>/dev/null || true)
      if { [ -n "$holder" ] && ! kill -0 "$holder" 2>/dev/null; } ||
         { [ -z "$holder" ] && [ -n "$(find "$lock" -maxdepth 0 -mmin +1 2>/dev/null)" ]; }; then
        rm -rf "$lock"
        continue
      fi
      if [ -z "$said" ]; then
        echo "waiting for another build of {{preset}} (process ${holder:-?}, $lock)" >&2
        said=1
      fi
      sleep 1
    done
    echo $$ > "$lock/pid"
    trap 'rm -rf "$lock"' EXIT
    cmake --preset {{preset}} > /dev/null
    cmake --build --preset {{preset}}

# Unit and contract tests, without the [slow] cases (side by side: the unit
# tests are in shards, and the contract suite runs its tests at once)
test preset="dev": (build preset)
    ctest --preset {{preset}} -LE slow -j {{num_cpus()}}

# Every test, [slow] included (run before calling a stage done)
test-all preset="dev": (build preset)
    ctest --preset {{preset}} -j {{num_cpus()}}

# Address + undefined-behavior sanitizers
test-asan: (test-all "asan")

# ThreadSanitizer: the [parallel] cases (see AGENTS.md for why the puzzle choice matters), each a
# ctest test of its own, side by side; then the contract suite against the TSan-built engine
test-tsan: (build "tsan")
    TSAN_OPTIONS=halt_on_error=1 ctest --preset tsan -L parallel -j {{num_cpus()}}
    TSAN_OPTIONS=halt_on_error=1 python3 tests/contract/test_engine.py --engine build/tsan/engine/shapelogic_engine

# Compare the new bt against the original burrTxt (see tests/oracle/README.md)
oracle *args: (build "release")
    python3 tests/oracle/oracle.py --old-repo "{{old_repo}}" --new-bt build/release/cli/shapelogic_cli compare {{args}}

# Load and save every corpus file with the original and the new core, compare the XML
oracle-roundtrip *args: (build "release")
    python3 tests/oracle/oracle.py --old-repo "{{old_repo}}" roundtrip {{args}}

# Pause searches in one program and continue them in the other (saved positions, decision 0006)
oracle-resume *args: (build "release")
    python3 tests/oracle/oracle.py --old-repo "{{old_repo}}" resume-check {{args}}

# Solve as burrTxt2 does and compare the solved files (after `oracle.py baseline-files --tier all`)
oracle-files *args: (build "release")
    python3 tests/oracle/oracle.py --old-repo "{{old_repo}}" compare-files {{args}}

# Compare bt's text output with the original burrTxt's (after `oracle.py baseline-text`)
oracle-text *args: (build "release")
    python3 tests/oracle/oracle.py --old-repo "{{old_repo}}" --new-bt build/release/cli/shapelogic_cli compare-text {{args}}

# Import, export to Puzzlecad and convert grids with the original and the new core, compare (deferred part 6)
oracle-imports *args: (build "release")
    python3 tests/oracle/oracle.py --old-repo "{{old_repo}}" imports {{args}}

# The Python module against the original's module: the same transcripts (deferred part 7)
oracle-python *args: (build "release")
    python3 tests/oracle/oracle.py --old-repo "{{old_repo}}" python {{args}}

# Record the original burrTxt's answers for a corpus tier
oracle-baseline *args:
    python3 tests/oracle/oracle.py --old-repo "{{old_repo}}" baseline {{args}}

# The Python module's tests (python/tests) on a preset's module (built when Python's headers are found)
python-test preset="dev": (build preset)
    PYTHONPATH=build/{{preset}}/python python3 -m unittest discover -s python/tests -v

# The Python module's wheel in build/wheel (scikit-build-core), installed into a fresh venv and tested there
python-wheel:
    #!/usr/bin/env bash
    set -euo pipefail
    tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
    rm -rf build/wheel
    # the wheel's version: the git tag's, as CMake reads it (cmake/ProjectVersion.cmake)
    tag="${SHAPELOGIC_VERSION:-$(git describe --tags --match 'v[0-9]*' --abbrev=0 2>/dev/null || echo v0.0)}"
    sed -E 's/^v//; s/^([0-9]+\.[0-9]+)$/\1.0/' <<< "$tag" > python/VERSION
    python3 -m venv "$tmp/build" && "$tmp/build/bin/pip" -q install --upgrade pip
    "$tmp/build/bin/pip" wheel ./python -w build/wheel
    python3 -m venv "$tmp/try"
    "$tmp/try/bin/pip" -q install build/wheel/shapelogic-*.whl
    cd python/tests && "$tmp/try/bin/python" -m unittest -q

# Every test suite, one after another, with a table of what passed: `just gate`, `just gate unit oracle` (groups: tests/gate.sh), `just gate all`
gate *groups:
    bash tests/gate.sh {{groups}}

# clang-tidy over the project's own sources (needs LLVM: brew install llvm); with file names (a regex each, of the path), only those sources
check *files: (build "dev")
    #!/usr/bin/env bash
    set -euo pipefail
    tidy=$(command -v run-clang-tidy || ls /opt/homebrew/opt/llvm/bin/run-clang-tidy 2>/dev/null || true)
    if [ -z "$tidy" ]; then echo "clang-tidy not found: install LLVM (brew install llvm / apt install clang-tidy)"; exit 1; fi
    # Homebrew's LLVM is not on the PATH: clang-tidy is beside run-clang-tidy
    bin=$(command -v clang-tidy || echo "$(dirname "$tidy")/clang-tidy")
    # On macOS the sources are read against the SDK the build's compiler
    # uses: Homebrew's clang may default to an older one, whose library
    # lacks what the build has (std::jthread)
    sdk=()
    if [ "$(uname)" = Darwin ]; then sdk=(-extra-arg=-isysroot "-extra-arg=$(xcrun --show-sdk-path)"); fi
    if [ -n "{{files}}" ]; then
      "$tidy" -quiet -clang-tidy-binary "$bin" "${sdk[@]}" -p build/dev {{files}}
    else
      "$tidy" -quiet -clang-tidy-binary "$bin" "${sdk[@]}" -p build/dev "$(pwd)/(core|engine|cli|python/src|tests/unit|tests/tools)/.*\.cpp$"
    fi

# ---- the web front end (web/, docs/FRONTEND-PLAN.md; needs Node 22) ----

# Install the web front end's packages from its lockfile
app-install:
    cd web && npm ci

# Generate web/src/engine/api.generated.ts from api/*.json
app-gen:
    cd web && npm run gen

# Type check (node, renderer and e2e projects), lint and format check the web front end
app-check:
    cd web && npm run check

# The web front end's tests; the engine tests need `just build release` (or BT_ENGINE)
app-test:
    cd web && npm test

# Run the app (electron-vite dev) with build/dev's engine, else build/release's, or BT_ENGINE (clears ELECTRON_RUN_AS_NODE, which turns Electron into Node)
app-dev:
    cd web && env -u ELECTRON_RUN_AS_NODE npm run dev

# Run the app as app-dev does, with build/release's engine (built first): the optimized engine, for long solves
app-release: (build "release")
    cd web && env -u ELECTRON_RUN_AS_NODE BT_ENGINE="{{justfile_directory()}}/build/release/engine/shapelogic_engine" npm run dev

# Build the app and run its end-to-end tests (Playwright's Electron driver; the release engine or BT_ENGINE),
# side by side (E2E_WORKERS apps at once, half the cores by default); without arguments the timed test
# (perf.e2e.ts) follows, alone. The build is the run's own (web/out-e2e-<pid>, BT_OUT_DIR), removed at the end.
app-e2e *args:
    #!/usr/bin/env bash
    set -euo pipefail
    cd web
    export BT_OUT_DIR="out-e2e-$$"
    trap 'rm -rf "$BT_OUT_DIR"' EXIT
    env -u ELECTRON_RUN_AS_NODE npm run e2e -- {{args}}
    {{ if args == "" { "env -u ELECTRON_RUN_AS_NODE npx playwright test --project=timed" } else { "" } }}

# Only the timed e2e test (perf.e2e.ts), alone, on a build of its own (as app-e2e)
app-e2e-timed:
    #!/usr/bin/env bash
    set -euo pipefail
    cd web
    export BT_OUT_DIR="out-e2e-$$"
    trap 'rm -rf "$BT_OUT_DIR"' EXIT
    env -u ELECTRON_RUN_AS_NODE npm run e2e-timed

# Package the app (electron-builder, web/tools/dist.ts) for this platform with the release engine: `--dir` the unpacked app only, `--arch x64` an Intel Mac's (after `just build release-x64`)
app-dist *args: (build "release")
    cd web && env -u ELECTRON_RUN_AS_NODE npm run dist -- {{args}}

# The packaged app's smoke test (after `just app-dist --dir`; BT_PACKAGED_APP names another executable)
app-smoke:
    cd web && env -u ELECTRON_RUN_AS_NODE npx playwright test -c playwright.smoke.config.ts

# The older web-* names
alias web-install := app-install
alias web-gen := app-gen
alias web-check := app-check
alias web-test := app-test
alias web-dev := app-dev
alias web-e2e := app-e2e
alias web-dist := app-dist
alias web-smoke := app-smoke

# ---- the website and user guide (site/, site/README.md; needs Node 22.22+) ----

# Run the site with live reload on http://localhost:4321
site-dev:
    cd site && npm ci && npm run dev

# Build the site and check its links and the app's Help URLs
site-build:
    cd site && npm ci && npm run build && npm test

# Rebuild the site's demo of the app (site/public/demo/): the engine compiled to WebAssembly (needs Emscripten: brew install emscripten) under the renderer built for the browser; `--check` drives the result in Google Chrome
site-demo *args:
    emcmake cmake --preset wasm > /dev/null
    cmake --build --preset wasm
    cd web && npm run demo -- {{args}}

# Retake the user guide's screenshots from the built app (site/src/assets/screens; after `just build release`)
docs-screenshots:
    #!/usr/bin/env bash
    set -euo pipefail
    cd web
    # a build of its own (BT_OUT_DIR), as app-e2e's
    export BT_OUT_DIR="out-docs-$$"
    trap 'rm -rf "$BT_OUT_DIR"' EXIT
    env -u ELECTRON_RUN_AS_NODE npm run docs-shots

# ---- releases (the steps: `just release` with no version prints them) ----

# Start a release: with no version, how and the steps; with one (vX.Y), check, push the tag and make the draft release with release/notes/vX.Y.md
release version="":
    @bash .github/scripts/release.sh {{version}}

# Keep a copy of a release's files in release/vX.Y (git-ignored)
release-fetch version:
    mkdir -p release/{{version}}
    gh release download {{version}} -R shapelogicxyz/shapelogic -D release/{{version}} --clobber

# Remove all build directories
clean:
    rm -rf build
