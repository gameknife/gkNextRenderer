# Steam Deck 首次部署与编译复盘

本文记录一次在 Steam Deck 上从零部署并编译 `gkNextRenderer` 的实际过程，目标是把首轮阻塞点沉淀下来，降低后续全新环境部署的摩擦。

## 环境概况

- 设备：Steam Deck
- 系统：SteamOS / Arch Linux 系
- 编译预设：`linux`
- 目标命令：

```bash
./gnb.sh build --reconfigure
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

`slangc` 现在随 VulkanSDK 一同分发，`gnb setup` 会通过 `gnb deps fetch vulkan` 自动拉取 VulkanSDK。如果仍然找不到，确认 `external/VulkanSDK/<version>/` 下存在 `bin/slangc`，或设置 `VULKAN_SDK` 环境变量指向已安装的 SDK 根目录。

## 实际处理方式

### 1. 安装缺失的系统包

在 Steam Deck / Arch Linux 上补齐：

```bash
sudo pacman -S --needed libxrandr wayland-protocols libxkbcommon
```

### 2. 重新执行完整构建

```bash
./gnb.sh build --reconfigure
```

如果中途遇到 GitHub 归档下载失败，通常直接重试即可。

## 最终结果

构建成功，主要产物包括：

- `out/build/linux/bin/gkNextRenderer`
- `out/build/linux/bin/gkNextEditor`
- `out/build/linux/bin/gkNextUnitTests`

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

## 已沉淀到主流程

这次复盘暴露的几个痛点现已固化到 `gnb` 与 README，无需再手动处理：

- pacman / apt 环境下，`gnb setup` 与 Linux 首轮 `gnb build` 会在 vcpkg bootstrap 前自动安装桌面构建所需系统包（含 `libxrandr`、`wayland-protocols`、`libxkbcommon`）。
- `slangc` 随项目托管的 Vulkan SDK 由 `gnb setup` 自动拉取。
- README 已有「Steam Deck / Arch Linux」专项小节，并说明 GitHub 归档下载偶发失败时优先重试同一条构建命令。

## 推荐的 Steam Deck 首次部署命令

```bash
sudo pacman -S --needed base-devel cmake ninja curl zip unzip tar pkgconf libxrandr wayland-protocols libxkbcommon
./gnb.sh setup
./gnb.sh build --reconfigure
./gnb.sh run gkNextRenderer
```

如果构建过程中 GitHub 下载失败，直接再次执行：

```bash
./gnb.sh build --reconfigure
```
