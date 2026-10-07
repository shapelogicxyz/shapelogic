# BurrTools Next. `just` lists the recipes.
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

# ThreadSanitizer, on the [parallel] cases (see AGENTS.md for why the puzzle choice matters)
test-tsan: (build "tsan")
    TSAN_OPTIONS=halt_on_error=1 ./build/tsan/tests/bt_tests "[parallel]"

# Compare the new bt against the original burrTxt (see tests/oracle/README.md)
oracle *args: (build "release")
    python3 tests/oracle/oracle.py --old-repo "{{old_repo}}" --new-bt build/release/cli/bt compare {{args}}

# Load and save every corpus file with the original and the new core, compare the XML
oracle-roundtrip *args: (build "release")
    python3 tests/oracle/oracle.py --old-repo "{{old_repo}}" roundtrip {{args}}

# Pause searches in one program and continue them in the other (saved positions, decision 0006)
oracle-resume *args: (build "release")
    python3 tests/oracle/oracle.py --old-repo "{{old_repo}}" resume-check {{args}}

# Record the original burrTxt's answers for a corpus tier
oracle-baseline *args:
    python3 tests/oracle/oracle.py --old-repo "{{old_repo}}" baseline {{args}}

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

# Remove all build directories
clean:
    rm -rf build
