# Track Specification: Fix Multi-Platform Build System Issues

## 1. Overview
The recent build system refactoring has introduced regressions in the build process for Linux and macOS environments, specifically affecting CI/CD pipelines and cross-compilation targets. This track aims to resolve these issues by correcting GitHub Actions workflow invocations and adding missing CMake presets for Android (on Linux) and iOS (on macOS).

## 2. Problem Statement
*   **GitHub Actions:** The workflows are currently invoking build scripts with incorrect arguments or flags, causing failures in the CI pipeline.
*   **Linux/Android:** The `CMakePresets.json` configuration lacks a preset for building Android targets on Linux hosts.
*   **macOS/iOS:** The `CMakePresets.json` configuration lacks a preset for building iOS targets on macOS hosts.

## 3. Functional Requirements
*   **Fix GitHub Actions Workflows:** Update the `.github/workflows` YAML files to correctly call the `build.sh` (or relevant) scripts with the proper arguments for each platform/target.
*   **Add Android-on-Linux Preset:** Define a valid CMake preset (e.g., `android-linux-debug` / `android-linux-release`) in `CMakePresets.json` to enable Android builds on Linux machines.
*   **Add iOS-on-macOS Preset:** Define a valid CMake preset (e.g., `ios-release` / `ios-debug`) in `CMakePresets.json` to enable iOS builds on macOS machines.
*   **Verify Script Compatibility:** Ensure `build.sh` correctly recognizes and handles the new/updated presets.

## 4. Acceptance Criteria
*   **GitHub Actions Success:** The updated GitHub Actions workflows must run successfully for Linux and macOS matrices without errors related to script invocation.
*   **Manual Build Verification (Linux):** Executing the build command for Android on a Linux environment (using the new preset) should configure and build successfully (or at least fail on code compilation, not configuration).
*   **Manual Build Verification (macOS):** Executing the build command for iOS on a macOS environment (using the new preset) should configure and build successfully.
*   **No Regressions:** Existing Windows and standard Linux/macOS desktop builds must continue to function correctly.

## 5. Out of Scope
*   Fixing actual code compilation errors (C++) that might be revealed after the build system is fixed (unless trivial).
*   Major refactoring of `build.sh` beyond what is necessary to support the CLI arguments and presets.
