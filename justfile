# ShapeLogic. `just` lists the recipes.
# Builds go to build/<preset> (see CMakePresets.json).

set shell := ["bash", "-uc"]

# The original BurrTools checkout the comparison harness runs against.
old_repo := env_var_or_default("BT_OLD_REPO", justfile_directory() + "/../burrtools_rotations")

default:
    @just --list

# Configure and build a preset (dev, release, asan, tsan)
build preset="dev":
    cmake --preset {{preset}} > /dev/null
    cmake --build --preset {{preset}}

# Unit and contract tests, without the [slow] cases
test preset="dev": (build preset)
    ctest --preset {{preset}} -LE slow

# Every test, [slow] included (run before calling a stage done)
test-all preset="dev": (build preset)
    ctest --preset {{preset}}

# Address + undefined-behaviour sanitizers
test-asan: (test-all "asan")

# ThreadSanitizer: the [parallel] cases (see AGENTS.md for why the puzzle choice matters),
# then the contract suite against the TSan-built engine
test-tsan: (build "tsan")
    TSAN_OPTIONS=halt_on_error=1 ./build/tsan/tests/bt_tests "[parallel]"
    TSAN_OPTIONS=halt_on_error=1 python3 tests/contract/test_engine.py --engine build/tsan/engine/bt-engine

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
    python3 -m venv "$tmp/build" && "$tmp/build/bin/pip" -q install --upgrade pip
    "$tmp/build/bin/pip" wheel ./python -w build/wheel
    python3 -m venv "$tmp/try"
    "$tmp/try/bin/pip" -q install build/wheel/shapelogic-*.whl
    cd python/tests && "$tmp/try/bin/python" -m unittest -q

# clang-tidy over the project's own sources (needs LLVM: brew install llvm)
check: (build "dev")
    #!/usr/bin/env bash
    set -euo pipefail
    tidy=$(command -v run-clang-tidy || ls /opt/homebrew/opt/llvm/bin/run-clang-tidy 2>/dev/null || true)
    if [ -z "$tidy" ]; then echo "clang-tidy not found: install LLVM (brew install llvm / apt install clang-tidy)"; exit 1; fi
    "$tidy" -quiet -p build/dev "$(pwd)/(core|engine|cli|tests/unit)/.*\.cpp$"

# Reformat the project's own sources with clang-format
format:
    #!/usr/bin/env bash
    set -euo pipefail
    fmt=$(command -v clang-format || ls /opt/homebrew/opt/llvm/bin/clang-format 2>/dev/null || true)
    if [ -z "$fmt" ]; then echo "clang-format not found: install LLVM (brew install llvm / apt install clang-format)"; exit 1; fi
    git ls-files 'core/*.h' 'core/*.cpp' 'engine/*.cpp' 'cli/*.cpp' 'tests/*.h' 'tests/*.cpp' | xargs "$fmt" -i

# ---- the web front end (web/, docs/FRONTEND-PLAN.md; needs Node 22) ----

# Install the web front end's packages from its lockfile
web-install:
    cd web && npm ci

# Generate web/src/engine/api.generated.ts from api/*.json
web-gen:
    cd web && npm run gen

# Type check (node, renderer and e2e projects), lint and format check the web front end
web-check:
    cd web && npm run check

# The web front end's tests; the engine tests need `just build release` (or BT_ENGINE)
web-test:
    cd web && npm test

# Run the app (electron-vite dev) with build/dev's engine, else build/release's, or BT_ENGINE (clears ELECTRON_RUN_AS_NODE, which turns Electron into Node)
web-dev:
    cd web && env -u ELECTRON_RUN_AS_NODE npm run dev

# Build the app and run its end-to-end tests (Playwright's Electron driver; the release engine or BT_ENGINE)
web-e2e *args:
    cd web && env -u ELECTRON_RUN_AS_NODE npm run e2e -- {{args}}

# Package the app (electron-builder, web/tools/dist.ts) for this platform with the release engine: `--dir` the unpacked app only, `--arch x64` an Intel Mac's (after `just build release-x64`)
web-dist *args: (build "release")
    cd web && env -u ELECTRON_RUN_AS_NODE npm run dist -- {{args}}

# The packaged app's smoke test (after `just web-dist --dir`; BT_PACKAGED_APP names another executable)
web-smoke:
    cd web && env -u ELECTRON_RUN_AS_NODE npx playwright test -c playwright.smoke.config.ts

# Remove all build directories
clean:
    rm -rf build
