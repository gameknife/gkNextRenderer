# Specification: Build System Refactoring & Standardization

## 1. Overview
该 Track 旨在重构 `gkNextRenderer` 的构建系统，包括核心构建脚本 (`.bat`/`.sh`)、CMake 配置文件以及 vcpkg 依赖集成。目标是消除重复逻辑，提升跨平台一致性，并使构建流程更具可读性和扩展性。

## 2. Functional Requirements

### 2.1 脚本标准化 (Scripts)
- **对齐功能**：确保 Windows (`.bat`) 和 Unix-like (`.sh`) 脚本在功能参数上完全一致。
- **公共逻辑提取**：清理重复代码，将通用配置（如目录路径定义、版本信息）提取到公共位置或简化脚本结构。
- **参数增强**：为 `build.sh/bat` 和 `run.sh/bat` 提供更标准化的参数支持（例如 `--config [Debug|Release]`, `--target [Name]`, `--clean` 等）。

### 2.2 CMake 现代化 (Modern CMake)
- **Target-based Approach**：优化 `CMakeLists.txt`，确保全面采用 `target_link_libraries`, `target_include_directories` 等现代 CMake 实践，减少全局变量使用。
- **目录结构标准化**：规范构建输出目录 (`bin`, `lib`) 和中间件目录的结构。
- **模块化**：如果可能，将复杂的 CMake 逻辑拆分为 `cmake/` 目录下的独立模块。

### 2.3 依赖管理优化 (vcpkg)
- **清理配置**：审核并优化 `vcpkg.json` 和 `vcpkg-configuration.json`。
- **集成稳定性**：确保 vcpkg 的 toolchain 集成在各平台上更稳健，减少环境依赖导致的问题。

## 3. Non-Functional Requirements
- **易读性**：脚本和 CMake 代码应遵循统一的命名规范，并添加必要的关键逻辑注释。
- **可扩展性**：方便后续添加新的平台支持或构建目标。

## 4. Acceptance Criteria
1. 所有 `.bat` 和 `.sh` 脚本均能正常工作，且接收的参数和行为逻辑保持一致。
2. 项目能在 Windows (MSVC), Linux (GCC/Clang) 和 macOS (Clang) 上通过重构后的脚本成功构建。
3. CMake 结构清晰，不再出现混乱的全局属性设置。
4. `vcpkg` 依赖下载和链接流程在全平台保持稳定。

## 5. Out of Scope
- 本次重构不改变现有的 C++ 代码逻辑。
- **不引入 Python**、PowerShell Core 或其他第三方脚本语言，坚持使用原生 Shell 和 Batch。
