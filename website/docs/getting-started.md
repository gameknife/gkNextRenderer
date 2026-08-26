# 快速起步指南 (Getting Started)

`gkNextEngine` 是一套基于现代 C++20 与 Vulkan 的跨平台 3D 渲染与游戏引擎实验场。项目通过统一的 `gnb` 命令行工具链，简化了依赖拉取、环境检查、编译与运行流程。

---

## 🛠️ 环境前置要求

| 平台 | 编译器 / 基础工具 | 说明 |
| :--- | :--- | :--- |
| **Windows** | Visual Studio 2022 (C++ 工作负载) | 推荐使用 VS2022，构建默认采用 Ninja 生成器 |
| **Linux** | GCC 12+ 或 Clang 16+、CMake 3.26+、Ninja | Ubuntu / Arch 下 `gnb setup` 会自动安装缺失的系统库 |
| **macOS** | Xcode / Command Line Tools (Apple Silicon) | macOS arm64 原生支持 |

> **提示**：不需要手动预装 Vulkan SDK 或 Slang 编译器，`gnb setup` 会自动检测并在需要时拉取项目约定的 SDK 版本至仓库内。

---

## 🚀 一键构建与运行

### 1. 检查宿主机环境
```bash
# Windows
./gnb.bat doctor

# Linux / macOS
./gnb.sh doctor
```

### 2. 准备依赖环境 (Setup)
```bash
./gnb.bat setup    # Windows
./gnb.sh setup     # Linux / macOS
```
> `gnb setup` 会自动初始化 vcpkg、下载 Vulkan SDK 1.4+ 以及 Slang 工具链。

### 3. 极速增量构建 (Build)
```bash
# 默认构建核心目标 (gkNextRenderer + gkNextUnitTests)
./gnb.bat build

# 构建全量 15+ 子项目
./gnb.bat build --all

# 构建特定子项目 (如 MagicaLego)
./gnb.bat build MagicaLego
```

### 4. 启动与体验 (Run)
```bash
# 启动主渲染器
./gnb.bat run gkNextRenderer

# 启动综合可视化编辑器
./gnb.bat run gkNextEditor

# 启动机场生态模拟
./gnb.bat run AirportSim
```

---

## 🌐 Remote Play 远程云游玩

任意桌面 Target 原生支持作为 WebRTC Host 运行，利用 Vulkan Video 硬件编码将 60FPS 画面推流到浏览器：

```bash
./gnb.sh remote --target gkNextRenderer --scene assets/models/playground.glb --res 1280x720
```

启动后控制台会输出本地 WebRTC 访问地址（如 `http://127.0.0.1:8080`），使用手机或任意 PC 浏览器打开即可零安装游玩并支持键鼠/虚拟手柄回传。

---

## 🧪 自动化测试

```bash
# 单元测试 (Catch2)
./out/build/windows/bin/gkNextUnitTests

# Agent 快速截图验证 (不弹窗、稳定帧截图自动退出)
gnb shot --scene assets/models/playground.glb
```
