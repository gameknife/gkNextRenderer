# 命令行接口 (CLI) 规范

本文档定义了项目维护脚本的标准参数和行为。

## 一般原则
- 所有脚本均支持 `-h` 和 `--help` 以打印使用信息。
- 带有值的标志可以使用空格或等号（例如 `--target name` 或 `--target=name`），但为了简化 Shell 处理，脚本默认实现空格分隔解析。

## 1. 构建脚本 (`build.sh` / `build.bat`)

用于使用 CMake Presets 配置和构建项目。

### 语法
```bash
./build.sh [options]
./build.bat [options]
```

### 选项
| 选项 | 描述 | 默认值 |
|--------|-------------|---------|
| `--preset <name>` | 使用的 CMake Preset。 | 基于操作系统自动检测 (例如 `default-macos-arm64`, `default-linux`, `default-windows`) |
| `--config <type>` | 构建配置 (Debug, Release)。如果适用（多配置生成器），将覆盖 preset 的默认值。 | `Debug` (如果 preset 未指定) |
| `--clean` | 构建前删除构建目录。 | `false` |
| `--target <name>` | 构建特定目标而不是 `all`。 | `all` |
| `--android` | **特殊:** 触发 Android Gradle 构建而不是 CMake。 | `false` |
| `-h, --help` | 显示使用信息。 | - |

> **关于 `--android` 的说明**: 此标志目前是一个“模式切换”，重定向到 `gradlew`。未来这可能会统一在 `android-arm64` 这样的 preset 下，但目前为了向后兼容，它保留为独立的标志。

## 2. 运行脚本 (`run.sh` / `run.bat`)

用于在正确的工作目录和环境中执行构建的二进制文件。

### 语法
```bash
./run.sh [options] [-- app-args]
./run.bat [options] [-- app-args]
```

### 选项
| 选项 | 描述 | 默认值 |
|--------|-------------|---------|
| `--target <name>` | 要运行的可执行目标名称。 | `gkNextRenderer` |
| `--preset <name>` | 用于构建的 CMake Preset（用于定位二进制文件）。 | 自动检测 |
| `--bin-dir <path>` | 包含可执行文件的显式路径。覆盖 preset 逻辑。 | `(Empty)` |
| `--list` | 列出检测到的 bin 目录中的所有可用可执行文件。 | `false` |
| `--dry-run` | 打印将要执行的命令而不实际运行。 | `false` |
| `-h, --help` | 显示使用信息。 | - |

### 应用程序参数
`--` 分隔符之后的所有参数将直接传递给可执行文件。
映射到应用程序参数的预定义便捷标志：
- `--scene <path>` -> 映射为 `--load-scene=<path>`
- `--present-mode <value>` -> 映射为 `--present-mode=<value>`

## 3. Vcpkg 引导脚本 (`vcpkg.sh` / `vcpkg.bat`)

用于确保 `vcpkg` 工具存在并已引导。它**不**安装依赖项（CMake 会做这件事）。

### 语法
```bash
./vcpkg.sh [options]
./vcpkg.bat [options]
```

### 选项
| 选项 | 描述 | 默认值 |
|--------|-------------|---------|
| `--update` | 强制对 vcpkg 仓库执行 `git pull`。 | `false` |
| `-h, --help` | 显示使用信息。 | - |

> **变更说明:** 移除了未使用的 `<platform>` 参数。

## 目录结构标准

为了支持这些脚本，CMake 构建系统必须强制执行以下输出结构：

- **Build Root:** `out/build/<preset>/`
- **Binaries:** `out/build/<preset>/bin/`
- **Libraries:** `out/build/<preset>/lib/`

这将替换旧的路径，如 `build/windows/bin` 或 `cmake-build-debug/`。
