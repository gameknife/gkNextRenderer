# Gemini Code Assistant Context

This document provides context for the Gemini code assistant to understand the `gkNextRenderer` project.

## Project Overview

`gkNextRenderer` is a cross-platform 3D game engine written in modern C++. It focuses on modern rendering techniques and game technology practices. The engine supports Windows, Linux, macOS, Android, and iOS.

The project is structured as a collection of libraries and executables. The main library is `gkNextEngine`, which contains the core engine functionality. Several executables are built on top of this library, including:

*   `gkNextRenderer`: The main renderer, which supports path tracing and hybrid rendering.
*   `gkNextEditor`: An ImGui-based editor for creating and editing scenes.
*   `MagicaLego`: A Voxel/Lego style prototype for path tracing validation.
*   `gkNextBenchmark`: A set of benchmarks for static and real-time scenes.
*   `Packager`: A tool for packing assets into `.pkg` files.
*   `gkNextUnitTests`: Unit and Integration tests using Catch2.

## Technologies

The project uses the following technologies:

*   **C++:** The primary programming language is C++20.
*   **Vulkan:** The engine uses Vulkan for rendering.
*   **CMake:** The project is built using CMake.
*   **vcpkg:** Dependencies are managed using vcpkg.
*   **SDL3:** Used for windowing and input.
*   **ImGui:** The editor is built using ImGui.
*   **glTF:** The engine uses the glTF 2.0 format for assets and scenes.
*   **Blender:** The recommended tool for creating and editing assets.
*   **Jolt Physics:** Used for physics simulation.
*   **Catch2:** Used for unit testing.

A full list of dependencies can be found in the `vcpkg.json` file.

## Building and Running

The project can be built and run on Windows, Linux, macOS, Android, and iOS. The following scripts are provided for convenience:

*   `vcpkg.bat` / `vcpkg.sh`: Installs the required dependencies using vcpkg.
*   `build.bat` / `build.sh`: Builds the project using CMake.
*   `run.bat` / `run.sh`: Runs the executables.

### Optional Build Features

The engine supports several optional features that can be enabled during the build process using specific flags.

#### AVIF Support
Enables AVIF texture loading and screenshot saving.
```bat
build.bat --avif
```

#### DLSS Support (Windows Only)
Enables NVIDIA DLSS support. This will automatically invoke `tools/fetch_streamline.bat` to download and deploy the necessary NVIDIA Streamline SDK.
```bat
build.bat --dlss
```

#### OIDN Support
Enables Intel OpenImageDenoise support for high-quality denoising. This will automatically invoke `tools/fetch_oidn.bat` or `tools/fetch_oidn.sh` to download and deploy the OIDN runtime libraries.
```bat
build.bat --oidn
```

Flags can be combined:
```bat
build.bat --avif --oidn --dlss
```

### Windows (Visual Studio 2022)

```bat
rem Install dependencies
.\vcpkg.bat windows

rem Build the project
.\build.bat windows-dev

rem Run the main renderer
.\run.bat
```

### Running Tests

To run the unit/integration tests:

```bat
.\out\build\windows-dev\bin\gkNextUnitTests.exe
```

*Note: Integration tests may require a valid Vulkan environment (GPU) and compiled shaders in `assets/shaders`.*

### Linux

```sh
# Install dependencies
./vcpkg.sh linux

# Build the project
./build.sh linux

# Run the main renderer
./run.sh linux
```

### macOS

```sh
# Install dependencies
./vcpkg.sh macos

# Build the project
./build.sh macos

# Run the main renderer
./run.sh macos
```

### Android (on Windows)

```bat
rem Install dependencies
vcpkg.bat android

rem Build the project
build.bat android

rem Run the main renderer
run.bat android
```

The `run.bat`/`run.sh` scripts can be used to run different executables by using the `--target` option. For example, to run the editor on Windows:

```bat
run.bat windows --target gkNextEditor.exe
```

## Development Conventions

*   **Language Preference:** All future interactions should be conducted in Chinese (中文).
*   **Coding Style:** The project uses a `.clang-format` file to enforce a consistent coding style.
*   **Testing:** 
    *   Tests are located in `src/Tests`.
    *   Use `Catch2` framework.
    *   Ensure tests handle resource paths correctly (usually project root).
    *   Mock dependencies where possible to avoid full engine initialization overhead/failures in CI.
*   **Contributions:** Contributions are welcome. Please open an issue or pull request on GitHub.
*   **Documentation:** The `doc` directory contains additional documentation, including `Thoughts.md`, which contains notes and ideas about the project.
*   **Agent Guide:** The `AGENT_GUIDE` directory contains guidelines for AI agents working on the project.