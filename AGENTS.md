# AGENTS.md

This file is the single source of truth for AI coding assistants (Claude Code, Gemini CLI, Codex, GitHub Copilot, Cursor, etc.) working in this repository. `CLAUDE.md` imports it via `@AGENTS.md`; other tools read `AGENTS.md` directly.

## Communication Preference

**Language: 中文 (Chinese)**
Always communicate with the user in Chinese (中文).

## Project Overview

gkNextRenderer is a cross-platform 3D game engine built with modern C++20 and Vulkan, featuring hardware/software ray tracing, real-time global illumination, GPU-driven rendering, and GPU CSM shadows. The Engine core remains below the 50k LOC target (~31k on 2026-07-17); all first-party Engine, Modules, Gameplay, applications and tests total 141,421 lines. Treat these as snapshots and use `gnb loc` for the current count.

**Key Technologies:**
- C++20/C11, Vulkan API, Slang shader language (ray query, not ray pipeline)
- ECS architecture (entt library) + entt::meta reflection
- QuickJS TypeScript scripting with hot reload (bundled `tools/tsc`)
- Multi-platform: Windows x86_64 / Linux x86_64 / macOS arm64 / Android arm64 / iOS arm64

**Subprojects (under `src/Application/`):**
- Render: gkNextRenderer (main), gkNextMinimalRenderer, gkNextStillBenchmark, gkNextMotionBenchmark, gkNextVisualTest, RmlUiDemo
- Editor: gkNextEditor (ImGui editor + node-based material editor), ScadStudio, ScadLibrary
- Game: MagicaLego, Brotato3D, KongLie3D, NextRA, BrickPlayer, CharacterDemo, FlappyCpp/FlappyJs, TruckerDemo, StudioSim, AirportSim, CitySolSim, Voyage3D
- Util: Packager (release packaging), ScadCatalog

## Build Commands

**Build (vcpkg is auto-bootstrapped on first run):**
- Setup once: `./gnb.sh setup` (Windows: `gnb.bat setup`)
- Core build (default): `./gnb.sh build` (Windows: `gnb.bat build` —— 默认仅构建核心目标 `gkNextRenderer` 与 `gkNextUnitTests`)
- Full build (all targets): `./gnb.sh build --all` (Windows: `gnb.bat build --all` —— 构建全量 15+ 子项目)
- Specific target: `./gnb.sh build gkNextEditor`
- Android: `./gnb.sh android`
- Clean rebuild: `./gnb.sh build --clean`
- Force vcpkg update: `./gnb.sh setup --refresh`

**Build serialization (IMPORTANT):** `gnb build` is a synchronous command. Do not impose a
short tool timeout on it; always wait for the command to return before taking any other build
action. Never start another `gnb`, CMake, or Ninja build while one build/configure is running,
because concurrent Windows builds can lock `.obj`, executables, or vcpkg state files.

**Targeted builds (IMPORTANT — prefer over full `gnb build --all`):**
默认不加参数运行 `./gnb.sh build` 只会编译 `gkNextRenderer` 和 `gkNextUnitTests`。AGENT 在验证改动时**默认只构建受影响的目标**，避免全量构建：
- **改动 Engine 层**（`src/Engine/**`、shaders、公共 runtime/reflection）：只需 `./gnb.sh build`（即默认构建 `gkNextRenderer` + `gkNextUnitTests`）。这两个目标编译通过即代表 engine API 没有破坏面上调用。
- **改动某个具体 program**（`src/Application/**` 下的单一子项目，如 MagicaLego、Brotato3D、ScadStudio 等）：只构建该目标自身，例如 `./gnb.sh build MagicaLego`。
- **改动 gnb / tools / 纯文档**：无需 C++ 构建。
- **大型 engine 重构、改动 ABI/广泛 header、不确定影响面，或用户明确要求**：才执行全量 `./gnb.sh build --all --reconfigure`，确认所有 program 都能编译。
- 增量构建无需 `--reconfigure`；仅在改了 CMake/preset/新增文件未被 glob 收录时才加 `--reconfigure`。

**CMake presets:** `windows` (默认使用 Ninja 极速生成器，带 MSVC/SDK 环境自动发现), `windows-vs`, `linux`, `macos-arm64`, `ios`.

**Optional Features:**
- AVIF is manual: `cmake --preset windows -DENABLE_AVIF=ON -DVCPKG_MANIFEST_FEATURES=avif` then `./gnb.sh build`
- DLSS/Streamline is always enabled on Windows and disabled elsewhere
- OIDN and MinGW support have been removed

**Build output:** `out/build/<platform>/bin/`

## Run Commands

- Default target/list: `./gnb.sh run`
- Specific target: `./gnb.sh run gkNextEditor`
- Editor shortcut: `./gnb.sh editor`
- Visual test shortcut: `./gnb.sh visual`
- TUI terminal mode: `./gnb.sh tui --scene assets/models/playground.glb`
- Android: `./gnb.sh android`
- Optional assets: `./gnb.sh paks fetch` / `./gnb.sh paks list`
- Source-line stats: `./gnb.sh loc` (CLI) — also browsable in `./gnb.sh dashboard`
- Dashboard: `./gnb.sh dashboard` (Wails window on Windows/macOS, browser fallback on Linux; todo/build/run/test/git/chat/LOC tabs)

Desktop binaries can be launched from any working directory; no `cd out/build/<preset>/bin` is required.

**Runtime success indicator:** Log shows `uploaded scene [...] to gpu`

## Testing

Tests no longer require the current working directory to be `bin`; launch them via their executable path. 需要完整引擎的集成测试用 `EngineTestFixture`，它会建真实的 Vulkan swapchain 渲染——fixture 默认带 `--hidden-window`，**测试时不再弹窗抢焦点**（仍真实渲染，可截图）。

```bash
# Unit tests (Catch2)
./out/build/<preset>/bin/gkNextUnitTests

# Run specific test by name or tag
./out/build/<preset>/bin/gkNextUnitTests "RenderComponent Usage"
./out/build/<preset>/bin/gkNextUnitTests "[Unit][RenderComponent]"

# List available tests/tags
./out/build/<preset>/bin/gkNextUnitTests --list-tests
./out/build/<preset>/bin/gkNextUnitTests --list-tags

# Visual tests (renders scenes, generates screenshots + report)
./out/build/<preset>/bin/gkNextVisualTest
```

**Visual Test Config:** `assets/configs/visual_test.json` defines scenes, frame counts, output directory.

### Agent Visual Validation (快速肉眼验证渲染改动)

当 AGENT 想快速确认一个渲染/场景/着色改动"看起来对不对"，**首选 `gnb shot`**，不要手动开窗口等截图：

```bash
# 渲染一个场景到稳定帧 → 截一张图 → 自动退出。完成后会打印截图绝对路径。
gnb shot --scene assets/models/playground.glb
gnb shot --target ScadStudio --scene assets/scad/source/beer_cup.scad --frames 60
gnb shot --target AirportSim --ui  # 截图包含 ImGui，适合验证 HUD / 面板
```

机制（gnb 统一编排，所有 target 行为一致）：
- gnb 启动目标并建立带一次性 token 的 loopback 控制通道，等待稳定帧后请求截图到**固定路径** `out/build/<preset>/screenshots/agent_validation.jpg`（覆盖式，无时间戳），确认落盘后请求退出。
- 默认截图隐藏 ImGui；传 `gnb shot --ui` 让截图包含当帧 UI（gnb 在截图请求里带上 ui 参数）。
- 窗口用 `SDL_WINDOW_HIDDEN` 创建：**不弹窗、不抢焦点**，不会打断你的 dev loop；present 自动切 immediate mode，渲染不受 vsync 限制，wall-clock 很快（几秒一张图）。
- AGENT 读那张 `agent_validation.jpg` 即可肉眼判断。需要换帧数用 `--frames`;输出路径固定为上述 `agent_validation.jpg`。
- 注意用 `./gnb.sh`（Windows `gnb.bat`）入口可在 gnb 源码变更后自动重建;直接调根目录 `gnb` 二进制不会自动更新。

**何时用哪条路径：**
- **改了某个场景/材质/光照/着色，只想看一眼对不对** → `gnb shot --scene <X>`（最轻、最快）。
- **做渲染回归、需要和 baseline 对比 / 一次性扫多个场景** → 跑全量 `gkNextVisualTest`（生成 report + baseline diff + manifest，较重）。

### DLSS / Streamline 验证限制

- `gnb shot` 和 `gnb validate`（包括 `--visible`）都会传入 `--agent-validation`；该模式为了确定性会强制禁用 Streamline，因此只能验证送入 DLSS 前的 scene color / depth / motion 等资源链，**不能证明 DLSS 或 DLSS-RR 实际生效**。
- 不要尝试用单独的 `--hidden-window` 绕过上述限制。普通 hidden SDL window 可能被识别为 minimized，swapchain 创建会等待窗口恢复，导致程序停在 DLSS evaluate 之前；这条路径不适合 Streamline/DLSS 验证。
- 需要验证真实 DLSS 时，必须在 Windows NVIDIA 环境中使用**非 hidden、非 agent-validation**的正常窗口/present 路径。需要自动结束时可使用短时 `gkNextMotionBenchmark` 配置，但仍不能传 `--hidden-window`；运行会弹出窗口，AGENT 应先告知用户。
- 验证时确认日志出现 `DLSS Super Resolution active for <renderer>` 或 `DLSS Ray Reconstruction active for PathTracing`，并检查没有 `slEvaluateFeature` / Streamline failure。普通 DLSS 应适用于 PathTracing、SoftwareTracing、SoftwareModern、SoftwareModernNoAmbient；DLSS-RR 只适用于 PathTracing。

### Agent Interactive Validation（输入驱动 + 断言）

需要把应用驱动到交互状态并自动判断 pass/fail 时，用声明式脚本：

```bash
gnb validate --script assets/agentscripts/smoke.agentscript.json
gnb validate --script assets/agentscripts/smoke.agentscript.json --target gkNextRenderer --scene assets/models/playground.glb
gnb validate --script assets/agentscripts/smoke.agentscript.json --visible  # 显示窗口，便于人工观察回放
```

机制：
- `gnb validate` 读取并解释脚本里的 `target` / `scene` / `viewport` 和步骤，命令行参数可覆盖；报告也由 gnb 写出。
- gnb 用 `--agent-validation` 启动引擎以获得隐藏窗口、Immediate present、禁用 Streamline 等确定性语义，并通过原子控制端点驱动。需要显示窗口时传 `gnb validate --visible`。
- Agent 脚本鼠标移动只推送合成 `SDL_EVENT_MOUSE_MOTION`，不会 `SDL_WarpMouseInWindow` 移动系统光标；按键/鼠标按钮也只走 SDL 事件队列。
- 支持步骤：`key` / `text` / `mouse-move` / `mouse-button` / `click` / `drag` / `scroll` / `wait-frames` / `wait-ms` / `wait-until` / `cvar` / `exec` / `assert` / `screenshot` / `log` / `quit`。
- 内建查询：`engine.totalFrames`、`engine.frameRate`、`engine.time`、`engine.status`、`scene.nodeCount`、`scene.selectedId`、`scene.selectedCount`、`cvar.<name>`；游戏可通过 `RegisterAgentQueries` 暴露 `game.<name>`。
- 结束会写 JSON report 到 `out/build/<preset>/agent_reports/<script-name>.json`；任一断言失败时进程返回非零退出码，CI 可直接判定失败。

## Linting

**Static Analysis:**
- Config: `.clang-tidy` (naming + include cleaner)
- Generate compile database: `cmake --preset <platform> -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`
- Run clang-tidy: `python3 tools/clang-tools/run-clang-tidy.py -p out/build/<platform>`
- Run naming checks: `BUILD_DIR=out/build/<platform> tools/clang-tools/run-naming.sh`

## Code Style (Summary)

**Naming (enforced by .clang-tidy):**
- Types/functions: PascalCase (e.g., `class RenderContext`, `void RenderFrame()`)
- Variables/parameters: camelCase (e.g., `int frameCounter`)
- Private members: camelCase_ (trailing underscore, e.g., `VkDevice device_`)
- Global variables: PascalCase (e.g., `GOption`)
- Constants/constexpr: camelCase (e.g., `constexpr int maxFrames`)
- Macros: UPPER_CASE (e.g., `VK_CHECK_RESULT`)

**Formatting:**
- Indentation: 4 spaces, no tabs
- Braces: Allman style (opening brace on new line)
- First include: `Common/CoreMinimal.hpp` (includes std, fmt, spdlog, platform detection)
- Platform abstraction: Use `PlatformCommon.h`, not direct platform headers; use `#if ANDROID` not `#ifdef`

**Shaders:**
- Use Slang (`.vert.slang`, `.frag.slang`, `.rgen.slang`, `.comp.slang`)
- Uses ray query API, not ray pipeline
- Avoid hard-coded constants; use uniforms/push constants

**Vulkan:**
- Always check VkResult with `VK_CHECK_RESULT`
- RAII for resource cleanup (pair allocations with destructors)
- Prefer `std::unique_ptr`/`std::shared_ptr` over raw owning pointers

## Architecture Overview

```
src/
├── Engine/                  # Engine core library (gkNextEngine.lib) — must not depend on Modules/
│   ├── Common/              # CoreMinimal.hpp + shared platform abstraction
│   ├── Runtime/             # Engine runtime: ECS, reflection, command history, config, editor UI host
│   ├── Assets/              # Scene, core asset data, GPU resources, CPU acceleration
│   ├── Vulkan/              # Vulkan backend + RayTracing/ (HW ray tracing)
│   ├── Rendering/           # Render pipelines
│   │   ├── PathTracing/     # Full path tracing
│   │   ├── SoftwareTracing/ # Software ray tracing
│   │   ├── SoftwareModern/  # Modern rasterization + software GI + NoAmbient deferred
│   │   ├── Shadow/          # GPU CSM shadow pass (4 cascades, bindless sampling)
│   │   ├── PipelineCommon/  # Shared pipeline utilities
│   │   ├── Preview/         # RenderView services (thumbnails, offscreen cameras)
│   │   └── Upscaler/        # IUpscaler abstraction (impl injected by NextStreamline)
│   └── Utilities/           # Misc helpers
├── Modules/                 # 16 optional engine modules (static libs, linked per app; see src/Modules/README.md)
│   ├── GltfLoader/, LDrawLoader/, ScadLoader/, SplatLoader/, SceneExport/  # Content pipelines
│   ├── NextQuickJS/, NextPhysics/, NextAudio/, NextAI/, NextRmlUi/         # Runtime capabilities
│   ├── NextRemote/, NextStreamline/, NextTui/, RenderViews/                # Presentation / streaming
│   └── DevTools/, LiveCoding/                                              # Development tooling
├── Gameplay/                # Gameplay primitives shared across games (CharacterActor, NavGrid A*, AI, rig, sim)
├── Application/             # Subproject entry points (per role)
│   ├── Render/, Editor/, Game/, Util/   # See "Subprojects" above
├── Tests/                   # Catch2 unit tests (gkNextUnitTests)
└── ThirdParty/              # Third-party code (DO NOT MODIFY)

assets/
├── shaders/                 # Slang shaders (.slang)
├── configs/                 # Runtime config (cvar_default.json, visual_test.json, per-game configs, ...)
├── models/                  # glTF scenes
├── scripts/                 # Hand-maintained runtime JS/MLS scripts
└── typescript/              # TypeScript sources + generated Engine.d.ts; runtime JS is emitted under build assets

tools/gnb/                   # Project CLI (Go) — see "gnb" section below
```

## Key Architectural Patterns

**Reflection System (entt::meta):**
- Provides auto-generated editor UI via PropertyPanel
- Exposes component properties to QuickJS JavaScript bindings
- Supports undo/redo for property modifications
- See `docs/AGENT_GUIDE/ReflectionSystem.md` for detailed documentation
- Register components using `REFLECT_COMPONENT` macro in component's .cpp file
- TypeScript definitions in `assets/typescript/Engine.d.ts` mirror reflected properties

**QuickJS Scripting:**
- TypeScript hot reload support via bundled `tools/tsc/tsc[.exe]` (`tsc.exe` on Windows, `tsc` on macOS/Linux); no Node/npm/global `tsc` dependency is required at runtime
- ES module loading supports relative imports under the runtime `assets/scripts` path; sources live in `assets/typescript`, while the source-tree `assets/scripts` directory contains separately maintained scripts
- Components reflected via `entt::meta` are auto-exposed to JavaScript
- Global namespace: `Global.GetEngine()`, `Global.GetScene()`, `Global.spdlog()`
- Scripted games should extend `assets/typescript/NextGameInstanceBase.ts` and call `RunGameInstance(new YourGameInstance())`
- Scene API: `Scene.FindNodeIdWithComponent()`, `Scene.GetNodeById()`, `SceneBuild.*` for rebuild-time procedural scene construction, `Scene.AddRenderNode()` for runtime nodes
- See `docs/AGENT_GUIDE/QuickJSBindings.md`; `FlappyCpp` / `FlappyJs` replay parity is the binding regression demo

**Component System:**
- ECS via entt library
- All components inherit from `Assets::Component`
- Must implement `GetMetaType()` for reflection support
- Common components: RenderComponent, PhysicsComponent, SkinnedMeshComponent

**Rendering / Shadows:**
- Five registered renderer types: PathTracing (HW RT), SoftwareTracing (SW DDA on ambient cubes), SoftwareModern (rasterizer + software GI), VoxelTracing, and SoftwareModernNoAmbient (deferred Lambert+IBL+CSM without AmbientCube).
- GPU CSM: 4 cascades, D32_SFLOAT per-cascade images bound bindless (slots 0..3); cascade selection + 3x3 PCF lives in `Common.SampleSunShadowCSM` (PathTracingRenderer.slang).
- `ShadowMapPass` (Engine/Rendering/Shadow) renders the cascades; UBO carries `SunCascadeViewProjection[4]` + `CascadeSplits`.

**Resource Management:**
- Vulkan objects use RAII (destroyed in destructors)
- Always pair allocations with deterministic cleanup
- No silent failures in init/resource loading paths

## Repository Hygiene

- DO NOT modify third-party code in `ThirdParty/` or `external/`
- DO NOT commit build artifacts (`out/`, object files)
- DO NOT hard-code absolute paths or add secrets/keys
- When adding dependencies, update `vcpkg.json`

## Verification After Changes

1. **Build:** 按改动范围选择目标构建（详见上文 "Targeted builds"），默认**不要**全量构建：
   - Engine 层改动：`./gnb.sh build gkNextRenderer gkNextUnitTests`（Windows: `gnb.bat build ...`）
   - 单个 program 改动：`./gnb.sh build <该 target>`
   - 仅大型重构 / 广泛 header / ABI 改动 / 用户要求时才用 `./gnb.sh build --reconfigure` 全量验证
2. **Run:** Verify application starts and logs `uploaded scene [...] to gpu`
3. **Test:** Run unit tests if touching core systems
4. **Visual:** 渲染类改动 → `gnb shot --scene <X>` 截一张图肉眼验证（不弹窗、自动退出，见上文 "Agent Visual Validation"）；需要 baseline 回归再跑 `gkNextVisualTest`

**Assistant Note:** 只有大型重构或不确定影响面的改动，才需要全量 `gnb build --reconfigure` 并修复全部编译错误后再报告完成；常规改动用对应的 targeted build 即可。

## Key References

- **`docs/README.md`** - 现行文档索引与生命周期规则；架构设计、项目说明和仍有效计划从这里进入
- **`docs/AGENT_GUIDE/`** - Layered documentation:
  - `core-patterns.md` / `contextual-rules.md` / `coding-standards.md` / `quick-commands.md` - General rules
  - `ReflectionSystem.md` - entt::meta reflection (editor UI + JS bindings)
  - `QuickJSBindings.md` - JS/TS engine bindings and replay parity demo
  - `HotReload.md` - Shader/script hot reload mechanics
  - `LDrawLoader.md` - LDraw model loading (used by MagicaLego/BrickPlayer)
  - `SCADLoader.md` - OpenSCAD (.scad) DSL loading (parser/evaluator/CSG via Manifold/text via FreeType)
  - `ScadTerrain.md` - gk_terrain low-poly walkable terrain (TERR spec, ter_* combinators, TerrainComponent)
  - `ScadAssetPlaybook.md` - SCAD 资产生成实战手册：kit → 场景 → 地形开放地图的流程、契约与验证闭环（新题材组件库/地图生成任务必读）
  - `ScadRig.md` - ScadRig rigid-body character rigs (bone_ modules + anim_* clips, FRigAnimator runtime)
  - `MagicaLego.md` - MagicaLego subproject notes
  - `Brotato3D.md` - Brotato3D code structure (god-class + per-system split, runtime data model, object pools)
  - `CharacterDemo.md` - CharacterDemo + NextGameplay shared layer (CharacterActor facade, ECS components, NavGrid A*, AI behavior tree)
- **README.en.md** - Project overview and quick start
- **.clang-tidy** - Naming conventions (source of truth)

## gnb Dashboard

`gnb dashboard` 默认启动 Wails 原生窗口（Windows/macOS）；普通 htmx 请求由 Wails AssetServer 直接处理，Build/Run/Test 和 Chat 的流式响应走随机 loopback 端口。`--browser` 可显式使用系统浏览器，`--no-open` 只启动 server。**Linux 不构建 Wails 桌面窗口**（无 libgtk/webkit 依赖，CGO 关闭）：`gnb`/`gnb dashboard` 直接回退到系统浏览器模式，`gnb dashboard --no-open` 为纯 CLI 模式（用 `curl 127.0.0.1:7777` 交互）。提供 tabs：
- **TODO**：可视化 `.spec/TODO.md` 的工作流操作（增删改、move、spec 创建、标 done/blocked）
- **Build / Run / Test**：触发 cmake build、运行 target、Catch2 测试，SSE 实时流日志
- **Git**：分支管理、stash、commits、本地改动 stage/unstage、LLM 生成 commit message
- **Chat**：通过统一 AI provider/profile router 对话（LocalLlama 可按需启动），支持流式与多会话归档
- **LOC**：`gnb loc` 的图表/表格化视图（分类柱图 + 嵌套表格）

实现位置：`tools/gnb/internal/dashboard/`（Go + 内嵌 html template）。

## Local LLM (gnb llm)

gnb 集成 **llama.cpp + Gemma 4** 本地 LLM，OpenAI 兼容 HTTP（默认 127.0.0.1:8765）。当前 active 模型 `gemma-4-E4B-it (Q4_K_M)`，128K context；备选 `E2B-it`。`gnb.toml` 的 `[external.llm].active` 切换，命令用 `--model <id>` 临时覆盖。

```bash
gnb llm setup [--model <id>|--all]   # 下载 llama.cpp 二进制 + GGUF 到 external/llm/
gnb llm models                       # 列出模型 + 下载状态
gnb llm serve | status | stop        # server 生命周期（自动按需启动 / 切模型重启）
gnb llm chat "你好"                  # 一次性 prompt
gnb git commit-msg [--stage-all] [--commit] [--dry-run] [--model <id>]   # LLM 生成 commit
gnb git ai-commit                    # commit-msg 的短别名
```

诊断要点：
- 配置：`gnb.toml` 的 `[external.llm.*]` 段（版本、URL、端口、模型列表）
- 运行时：`external/llm/run/server.{log,pid}`（PID 文件第 4 行是当前加载的模型 id）
- 后端：Windows/Linux Vulkan、macOS Metal
- 多模型 `[[external.llm.models]]` 数组配置，GGUF 落在 `external/llm/models/<file>.gguf` 可并存

commit-msg prompt 内容包含：模式（staged / working tree）、文件清单（含 `??` 未跟踪）、`git diff --stat`、已跟踪文件 diff、未跟踪文件合成的 `+++ b/<path>` 新文件 diff（带二进制/64KB 大小保护）。超 `--max-diff-chars` 按文件边界截断。

引擎层 `NextAI::FAIService` 通过 AI bridge 复用 gnb 的 provider/profile；选择 LocalLlama 时共享同一个 llama-server，无需独立模型进程。NextAI 只提供 Chat、Structured Output、stream、session/cancel/usage 等轻量能力；SCAD、MagicaLego、StudioSim、AirportSim 的 prompt、领域校验、一次修复和 deterministic fallback 由各产品拥有。Dashboard 默认走普通 Chat，仅显式 `Tool Call Smoke` 使用固定内存 fixture；不得向普通请求附加 repo/Git/Shell/Scene 工具。`gnb llm serve --model <id>` 切模型后由 gnb 路由新请求，无需重启应用。

## Spec Workflow

**完整规范见 [.spec/README.md](.spec/README.md)**。下面是 AGENT 必须遵守的核心规则，规范与本节不一致以 README 为准。

文件位置：
- 任务列表：`.spec/TODO.md`
- 任务详细规格（可选，仅复杂任务）：`.spec/specs/<id>.md`
- 完成报告（一任务一文件）：`.spec/journal/<id>.md`
- 卡住提问（一任务一文件）：`.spec/blockers/<id>.md`
- 归档：`.spec/ARCHIVE.md`

执行规则：

1. 用户触发"启动交互式工作流"时，先读取 `.spec/TODO.md` 了解当前里程碑、任务列表和"最近完成"数量
2. 主循环通过 `gnb todo next --wait --timeout 590s --json` 获取任务：
   - 若"下一步"段已有 `[ ]` 任务，命令会立即返回第一个任务
   - 若当前没有任务，命令会等待 `.spec/TODO.md` 修改；590 秒内出现任务就立即返回
   - 若等待 590 秒仍没有任务，命令返回 `found: false` 并退出；AGENT 必须立即再次调用同一命令继续等待，不要自己 sleep、不要输出总结、不要结束 turn
3. 若命令返回 `milestone_status: "done"`，退出工作流
4. 若命令返回 `found: true`：若 `.spec/specs/<id>.md` 存在，先读它再执行
5. 执行任务
6. 完成后：
   - 在 TODO.md 把该任务的 `[ ]` 改为 `[x]`
   - 行末追加 ` → journal/<id>.md (YYYY-MM-DD)`
   - 写 `.spec/journal/<id>.md`（frontmatter + 做了什么 + 改动文件 + 风险/遗留）
7. 回到步骤 2，继续下一个 `[ ]` 任务
8. **唯一退出条件**：里程碑状态被改为 `done`。除此之外（包括"下一步"段为空、所有任务已完成但里程碑未 done、命令 590 秒无任务返回），都必须继续调用步骤 2 的命令轮询

特殊情况：
- 任务歧义无法判断时：写 `.spec/blockers/<id>.md`，任务状态改 `[!]`，**跳过该任务继续做下一个**，不要瞎猜
- 启动工作流时若"最近完成"段超过 10 条：在首次回复中提醒用户运行 `gnb todo archive`，但不要自己归档
- 用户在工作流期间修改 TODO.md：下一轮重扫时会发现

边界：
- AGENT **可改**：TODO.md 中任务的状态字符、行末 journal 链接；`journal/`、`blockers/` 下的文件
- AGENT **不可改**：TODO.md 中任务标题/ID/优先级/类型/所属段落；`specs/` 下的文件；`ARCHIVE.md`；"待规划"段任何

任务
- 不要建立自动化任务（hooks、scheduled tasks 等）
- 不要调用其他 agent 处理任务
