# Steam Deck 首次部署与编译复盘

本文记录一次在 Steam Deck 上从零部署并编译 `gkNextRenderer` 的实际过程，目标是把首轮阻塞点沉淀下来，降低后续全新环境部署的摩擦。

## 环境概况

- 设备：Steam Deck
- 系统：SteamOS / Arch Linux 系
- 编译预设：`full-linux`
- 目标命令：

```bash
./build.sh --preset full-linux --reconfigure
```

## 实际遇到的问题

### 1. `vulkan-loader` 在 vcpkg 阶段失败

首个阻塞不是项目源码，而是 vcpkg 依赖 `vulkan-loader` 在配置阶段失败。

核心报错：

```text
The following required packages were not found:
- xrandr
```

进一步检查后，Steam Deck 缺少的并不只有一个包，至少包括：

- `libxrandr`
- `wayland-protocols`
- `libxkbcommon`

这些包缺失时，vcpkg 会在比较深的依赖阶段才报错，定位成本较高。

### 2. 第三方源码下载失败

在继续安装依赖时，`opengl-registry` 下载曾出现 GitHub 传输失败：

```text
curl: (92) HTTP/2 stream 1 reset by server
```

这不是源码问题，也不是本地配置问题，属于第三方归档下载时的网络抖动。重新执行构建后，vcpkg 成功下载并继续构建。

### 3. CMake 配置阶段缺少 `slangc`

当 vcpkg 依赖都准备好后，项目自身配置又卡在：

```text
slangc not found!
```

这是因为项目需要 Slang 着色器编译器，但首次部署时机器上并没有可用的 `slangc`，而 README 也没有明确把它作为 Linux 首次部署的关键前置项强调出来。

仓库里实际上已经有对应脚本：

```bash
./tools/fetch_slang_linux.sh
```

执行后会把 `slangc` 安装到 `external/slang-<version>-linux-x86_64/`，随后 CMake 就能自动发现。

## 实际处理方式

### 1. 安装缺失的系统包

在 Steam Deck / Arch Linux 上补齐：

```bash
sudo pacman -S --needed libxrandr wayland-protocols libxkbcommon
```

### 2. 重新执行完整构建

```bash
./build.sh --preset full-linux --reconfigure
```

如果中途遇到 GitHub 归档下载失败，通常直接重试即可。

### 3. 安装 `slangc`

```bash
./tools/fetch_slang_linux.sh
```

然后再次执行：

```bash
./build.sh --preset full-linux --reconfigure
```

## 最终结果

构建成功，主要产物包括：

- `out/build/full-linux/bin/gkNextRenderer`
- `out/build/full-linux/bin/gkNextEditor`
- `out/build/full-linux/bin/gkNextUnitTests`

运行验证中，`gkNextRenderer` 成功启动，并输出：

```text
uploaded scene [CornellBox.proc] to gpu
```

说明首次部署后的运行链路已经打通。

## 额外观察

### 1. 单元测试存在现有失败

环境修复和构建完成后，`gkNextUnitTests` 能启动，但当前仓库基线里已有至少一个失败用例：

- `src/Tests/Test_LDrawLoader.cpp:122`
- 用例：`LDraw loader applies configurable LDU scale to geometry and placement`

这属于测试基线问题，不是此次 Steam Deck 环境问题导致的编译失败。

### 2. `Gemini` Provider 初始化警告不影响构建

运行时日志里会看到 AI provider 的 fallback 警告，但不影响渲染器启动与 GPU 场景上传。

## 后续优化建议

### 1. 在 `build.sh` 里前置检查 Linux 桌面依赖

不要等到 vcpkg 构建 `vulkan-loader` 时才暴露缺失包。更好的方式是在构建一开始就用 `pkg-config` 检查：

- `xrandr`
- `wayland-protocols`
- `xkbcommon`

然后直接给出不同发行版的安装命令提示。

### 2. 在 `build.sh` 里自动补 `slangc`

如果检测到：

- `PATH` 中没有 `slangc`
- `external/` 下也没有项目托管的 Slang

则可以直接调用：

```bash
./tools/fetch_slang_linux.sh
```

这样首次部署时就不会在 CMake 深处才因为 `slangc` 失败。

### 3. README 增加 Steam Deck / Arch Linux 专项说明

Ubuntu 依赖列表并不能覆盖 SteamOS / Arch 系环境。README 应该补一段单独说明，至少明确：

- 推荐在 Steam Deck 上直接使用 `full-linux`
- 首次部署需安装 `libxrandr`、`wayland-protocols`、`libxkbcommon`
- `build.sh` 会自动尝试准备 `slangc`
- 若 GitHub 下载偶发失败，可直接重试构建

### 4. 把“网络抖动可重试”写进文档

首次部署时最容易误判的一类问题，是把第三方归档下载失败误认为本地环境配置错误。README 或部署说明里应明确：

- vcpkg 依赖下载依赖 GitHub
- 单次失败不一定代表配置错误
- 优先重试一次构建，再判断是否需要人工排查

## 推荐的 Steam Deck 首次部署命令

```bash
sudo pacman -S --needed base-devel cmake ninja curl zip unzip tar pkgconf libxrandr wayland-protocols libxkbcommon
./build.sh --preset full-linux --reconfigure
./run.sh --preset full-linux --target gkNextRenderer
```

如果构建过程中 GitHub 下载失败，直接再次执行：

```bash
./build.sh --preset full-linux --reconfigure
```
