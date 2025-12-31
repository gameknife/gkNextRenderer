# Plan: Fix Multi-Platform Build System Issues

## Phase 1: GitHub Actions Workflow Fixes
- [x] Task: Fix `build.yml` (or equivalent) to use correct arguments for Windows builds. [648c8b00]
- [x] Task: Fix `build.yml` (or equivalent) to use correct arguments for Linux builds. [bb4d846f]
- [x] Task: Fix `build.yml` (or equivalent) to use correct arguments for macOS builds. [f31af8cc]
- [ ] Task: Verify workflow syntax and dry-run (if possible).
- [ ] Task: Conductor - User Manual Verification 'GitHub Actions Workflow Fixes' (Protocol in workflow.md)

## Phase 2: Linux & Android Configuration
- [ ] Task: Update `CMakePresets.json` to include `android-linux-debug` preset.
- [ ] Task: Update `CMakePresets.json` to include `android-linux-release` preset.
- [ ] Task: Update `build.sh` to handle new Android presets if necessary.
- [ ] Task: Conductor - User Manual Verification 'Linux & Android Configuration' (Protocol in workflow.md)

## Phase 3: macOS & iOS Configuration
- [ ] Task: Update `CMakePresets.json` to include `ios-release` preset.
- [ ] Task: Update `CMakePresets.json` to include `ios-debug` preset.
- [ ] Task: Update `build.sh` to handle new iOS presets if necessary.
- [ ] Task: Conductor - User Manual Verification 'macOS & iOS Configuration' (Protocol in workflow.md)
