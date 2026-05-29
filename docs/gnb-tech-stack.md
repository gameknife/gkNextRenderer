# gnb 技术栈说明

`gnb` 是 `gkNextRenderer` 的统一工程入口，负责把原来分散的构建、运行、测试、资源准备、移动端入口和打包流程收敛到一个跨平台 CLI 里。命令面向使用者保持简洁，但实现上分成了几层明确的技术栈。

## 一句话概览

- 启动层：仓库根目录的 `gnb.bat` / `gnb.sh` 负责 bootstrap、本地重编译和缓存二进制切换
- CLI 层：`tools/gnb/cmd/gnb/main.go` 使用 `cobra` 注册子命令与参数
- 配置层：仓库根 `gnb.toml` 保存版本约束、vcpkg、外部工具链、pak 资源和目标列表
- 执行层：`tools/gnb/internal/*` 分模块封装 CMake、vcpkg、资源下载、运行器、打包和平台逻辑
- 产物层：构建结果仍然落在 `out/build/<preset>/bin/`，`gnb` 只是统一驱动这些流程

## 为什么用 Go

- `gnb` 是独立二进制，不依赖引擎本体，也不需要先把 C++ 工程编出来
- 单文件分发适合做仓库根命令入口，Windows / Linux / macOS 都能保持一致体验
- 标准库已经足够覆盖文件系统、进程启动、HTTP 下载、ZIP/TAR 解包等基础能力
- 本地如果装了 Go，`gnb.bat` / `gnb.sh` 会优先从 `tools/gnb` 重新编译，方便维护者直接改 CLI

## 实现分层

### 1. 启动与分发层

根目录的 `gnb.bat` 和 `gnb.sh` 不是完整业务脚本，而是很薄的 shim：

- 优先使用仓库本地 `gnb(.exe)`，必要时从 `tools/gnb` 重新编译
- 没有本地二进制时，使用 `tools/gnb-bin/<platform>/` 下的缓存副本
- 缓存缺失或版本落后时，从 GitHub release 下载预编译二进制和 `gnb-version.txt`

这层的目标是让普通用户不必理解 Go 工具链，也能直接使用 `gnb`。

### 2. CLI 命令层

`tools/gnb/cmd/gnb/main.go` 是命令入口，核心技术是 [Cobra](https://github.com/spf13/cobra)：

- 子命令注册：`info`、`doctor`、`setup`、`build`、`run`、`test`、`visual`、`editor`
- 平台命令：`android`、`ios`
- 资源和发布命令：`paks`、`package`、`install`

`main.go` 只做参数编排和上下文拼装，不把具体逻辑塞进命令定义里，这样后续新增子命令时不会让入口文件失控膨胀。

### 3. 配置与数据层

`gnb.toml` 是 `gnb` 的中心配置文件，`tools/gnb/internal/config/config.go` 用 `BurntSushi/toml` 解析。当前主要承载几类数据：

- `[gnb]`：最小版本约束
- `[vcpkg]`：vcpkg ref、本地根目录、binary cache 目录
- `[external.*]`：Streamline、TypeScript 编译器、MoltenVK、Slang 的下载地址
- `[paks]`：可选资源包所在仓库、release tag、每个资源的落盘位置
- `[targets]`：默认启动目标与可运行目标白名单

这种做法把“仓库策略”放在 TOML，把“执行逻辑”放在 Go 代码里，便于后续调整版本、资源源地址和目标列表，而不必反复改命令实现。

### 4. 执行模块层

`tools/gnb/internal/` 里的模块按职责拆分：

- `cmakerun`：选择 preset，执行 configure / build / clean
- `vcpkg`：bootstrap 与 toolchain 路径管理
- `fetcher`：下载并解包外部工具链，如 Slang、TypeScript 编译器、MoltenVK
- `paks`：列出、拉取、发布可选 pak / ffmpeg / sfx 资源
- `runner`：定位 `out/build/<preset>/bin/` 下的可执行文件并启动
- `packager`：把桌面版本或 `MagicaLego` 产物打成分发包
- `android` / `ios`：移动端专用入口
- `platform`：平台识别、可执行扩展名、Linux 包依赖检查
- `console`：统一 `[gnb]` 风格输出和命令回显

这个拆法的好处是边界清晰。比如“下载工具链”不需要知道 CMake 参数怎么拼，“运行目标”也不需要关心 pak 发布逻辑。

## `gnb` 依赖了哪些外部技术

除了 Go 标准库，当前关键依赖主要有：

- `spf13/cobra`：CLI 命令和参数系统
- `BurntSushi/toml`：解析 `gnb.toml`
- `CMake` / `Ninja`：原生工程配置与构建后端
- `vcpkg`：C++ 依赖管理与二进制缓存
- GitHub Releases：预编译 `gnb` 二进制和可选 pak 资源的分发通道

换句话说，`gnb` 本身不是新的构建系统，而是站在现有 CMake + vcpkg 之上的统一控制面。

## 与主工程的关系

`gnb` 不直接链接 `src/` 下的 C++ 代码，也不参与 Vulkan / ECS / QuickJS 运行时。它的职责边界非常明确：

- 在“构建前”准备工具链和资源
- 在“构建时”统一驱动 CMake preset 和 target
- 在“构建后”提供运行、测试、可视化测试和打包入口

这意味着 `gnb` 出问题时，通常可以把问题归类到以下几个层面之一：

- 宿主机环境缺失：`gnb doctor`
- 依赖准备不完整：`gnb setup`
- 原生工程配置或编译失败：`gnb build`
- 可执行产物缺失或路径不对：`gnb run`

## 维护建议

- 加新外部工具链时，优先补 `gnb.toml`，再在 `internal/fetcher` 增加对应逻辑
- 加新应用 target 时，同时更新 `gnb.toml` 的 `[targets]`
- 新增子命令时，保持 `main.go` 只负责组装，具体行为下沉到 `internal/`
- 不要把平台分支散落到多个命令里，优先收敛到 `internal/platform`

## 相关文档

- 架构与代码导览（改 `gnb` 源码必读）：`docs/gnb-architecture.md`
- 命令用法手册：`docs/gnb-cli.md`
- 最小命令规格：`docs/CLI_SPEC.md`
- 本地开发入口：`tools/gnb/README.md`
