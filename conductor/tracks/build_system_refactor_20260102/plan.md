# Plan: Build System Refactoring & Standardization

## Phase 1: Analysis & Design [checkpoint: 6eb10a3]
- [x] Task: Analyze current build scripts and CMake files to identify all existing logical branches and parameters. [analysis]
- [x] Task: Define the standard command-line interface (CLI) specification for all scripts (`build`, `run`, `vcpkg`). [docs/CLI_SPEC.md]
- [x] Task: Design the new directory structure for CMake modules and build artifacts. [docs/CMAKE_STRUCTURE.md]
- [x] Task: Conductor - User Manual Verification 'Phase 1: Analysis & Design' (Protocol in workflow.md) [f03ed9e]

## Phase 2: Modern CMake Refactoring [checkpoint: 6725f3e]
- [x] Task: [TDD] Create a validation script to check for deprecated CMake globals and verify target-based linking. [9e38cb2]
- [x] Task: Refactor `CMakeLists.txt` to remove global include/link settings and use `target_*` commands. [65d5f6f]
- [x] Task: Modularize CMake logic: Move complex setup (e.g., vcpkg setup, toolchain checks) into `cmake/` modules. [1f484b3]
- [x] Task: Standardize output directories in CMake (bin/lib) to match the new spec. [b9b58d9]
- [x] Task: Conductor - User Manual Verification 'Phase 2: Modern CMake Refactoring' (Protocol in workflow.md) [327729f]

## Phase 3: Shell Script Refactoring (Unix)
- [x] Task: [TDD] Create `tests/build_system/test_build_sh.sh` to verify `build.sh` arguments (help, invalid args, successful build). [d1daf99]
- [x] Task: Refactor `build.sh` to implement the standard CLI interface and parameter parsing. [d1daf99]
- [x] Task: Refactor `run.sh` to support standardized arguments (`--target`, `--config`). [7060802]
- [ ] Task: Refactor `vcpkg.sh` to clean up logic and align with new directory structures.
- [ ] Task: Verify entire Unix build chain (vcpkg -> build -> run) works with new scripts.
- [ ] Task: Conductor - User Manual Verification 'Phase 3: Shell Script Refactoring (Unix)' (Protocol in workflow.md)

## Phase 4: Batch Script Refactoring (Windows)
- [ ] Task: [TDD] Create `tests/build_system/test_build_bat.bat` (or equivalent) to verify `build.bat` arguments.
- [ ] Task: Refactor `build.bat` to implement the standard CLI interface and parameter parsing (matching Unix scripts).
- [ ] Task: Refactor `run.bat` to support standardized arguments.
- [ ] Task: Refactor `vcpkg.bat` to ensure consistency with the Unix version.
- [ ] Task: Verify entire Windows build chain works with new scripts.
- [ ] Task: Conductor - User Manual Verification 'Phase 4: Batch Script Refactoring (Windows)' (Protocol in workflow.md)

## Phase 5: Cleanup & CI Integration
- [ ] Task: Update `vcpkg.json` and `vcpkg-configuration.json` to remove obsolete entries.
- [ ] Task: Update `README.md` and project documentation to reflect the new build commands and options.
- [ ] Task: Review and update Github Workflows (`.github/workflows`) to use the new script parameters.
- [ ] Task: Final full-system verification on all supported platforms (via CI or local checks).
- [ ] Task: Conductor - User Manual Verification 'Phase 5: Cleanup & CI Integration' (Protocol in workflow.md)
