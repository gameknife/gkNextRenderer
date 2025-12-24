# Gemini Code Assistant Guidelines

This document outlines the operational guidelines, development conventions, and testing strategies for the Gemini Code Assistant working on the `gkNextRenderer` project.

## Development Conventions

### Language Preference
*   All interactions and documentation should be conducted in **Chinese (中文)** unless specified otherwise.

### Coding Style
*   Adhere to the project's `.clang-format` for C++ code.
*   Follow modern C++20 standards.
*   Maintain consistent naming conventions as observed in existing files (e.g., `CamelCase` for classes/methods, `camelCase_` or `_camelCase` for member variables).

### Architecture & Patterns
*   **Engine Architecture**: The engine uses a Singleton pattern for `NextEngine`.
*   **Physics**: Jolt Physics is used. `NextPhysics` wrapper handles integration.
*   **Assets**: `Scene`, `Node`, `Model` manage the scene graph.
*   **Rendering**: Vulkan-based renderer.

## Testing Strategy

The project uses **Catch2** for unit and integration testing.

### Test Location
*   Test source files are located in `src/Tests/`.
*   The test executable target is `gkNextUnitTests`.

### Writing Tests
1.  **Framework**: Use `Catch2` macros (`TEST_CASE`, `SECTION`, `REQUIRE`, `CHECK`).
2.  **Integration Tests**:
    *   For tests requiring the full engine stack (Physics, Scene), instantiating `NextEngine` is possible but requires a valid Vulkan environment (GPU) and compiled Shaders.
    *   **Limitation**: Running full engine tests in headless CI/CD environments without GPU/Display may fail.
    *   **Workaround**: For logic-only tests, prefer mocking dependencies or testing components (`NextPhysics`, `Node`) in isolation if possible.
3.  **Mocking**:
    *   To test `NextGameInstanceBase` subclasses, create a Mock/Test implementation (as seen in `Test_PhysicsSync.cpp`).
    *   Avoid testing `VulkanBaseRenderer` logic in unit tests unless the environment supports it.

### Running Tests
*   **Build**: Ensure `gkNextUnitTests` target is built.
    ```bat
    build.bat windows
    ```
*   **Execute**: Run the executable from the `bin` directory.
    ```bat
    .\build\windows\bin\gkNextUnitTests.exe
    ```
*   **Prerequisites**:
    *   Ensure `assets` directory is accessible (usually run from project root, but currently CWD handling in tests might need adjustment or assets need to be in place).
    *   **Important**: Shaders (`.spv` files) must be compiled and available in `assets/shaders`. The engine runtime throws an exception if shaders are missing.

### Current Test Issues & Troubleshooting
*   **Shader Missing Exception**: The engine startup currently fails if `Process.UpScaleFSR.comp.slang.spv` is missing. Ensure shaders are compiled before running integration tests that initialize `NextEngine`.
*   **Path Issues**: `FileHelper` uses relative paths. Ensure the test executable is run with the correct Working Directory (Project Root) or assets are copied to the executable directory.

## Modification Log

*   **2025-12-23**: Added `SetBodyActive` to `NextPhysics` and synchronized `Node::SetVisible` with physics body activation. Added `catch2` to `vcpkg.json` and created initial `gkNextUnitTests` framework.
