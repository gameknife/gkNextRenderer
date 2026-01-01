# Plan: Fix Multi-Platform Build System Issues

## Phase 1: GitHub Actions Workflow Fixes
- [x] Task: Fix `build.yml` (or equivalent) to use correct arguments for Windows builds. [648c8b00]
- [x] Task: Fix `build.yml` (or equivalent) to use correct arguments for Linux builds. [bb4d846f]
- [x] Task: Fix `build.yml` (or equivalent) to use correct arguments for macOS builds. [f31af8cc]
- [x] Task: Verify workflow syntax and dry-run (if possible). [47c4934c]
- [x] Task: Conductor - User Manual Verification 'GitHub Actions Workflow Fixes' (Protocol in workflow.md) [5ad86f1b]

## Phase 2: Linux & Android Configuration
- [x] Task: Update `CMakePresets.json` to include `android-linux-debug` preset. [110e3fa1]
- [x] Task: Update `CMakePresets.json` to include `android-linux-release` preset. [5d51a28a]
- [x] Task: Update `build.sh` to handle new Android presets if necessary. [c1e4db49 - No changes needed]
- [x] Task: Conductor - User Manual Verification 'Linux & Android Configuration' (Protocol in workflow.md) [70eca9ee]

## Phase 3: macOS & iOS Configuration
- [x] Task: Update `CMakePresets.json` to include `ios-release` preset. [b21a7341]
- [x] Task: Update `CMakePresets.json` to include `ios-debug` preset. [4f1cded3]
- [ ] Task: Update `build.sh` to handle new iOS presets if necessary.
- [ ] Task: Conductor - User Manual Verification 'macOS & iOS Configuration' (Protocol in workflow.md)
