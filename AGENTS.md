# AGENTS.md

This file is the single source of truth for AI coding assistants (Claude Code, Gemini CLI, Codex, GitHub Copilot, Cursor, etc.) working in this repository. `CLAUDE.md` imports it via `@AGENTS.md`; other tools read `AGENTS.md` directly.

## Communication Preference

**Language: 中文 (Chinese)**
Always communicate with the user in Chinese (中文).

## Project Overview

gkNextRenderer is a cross-platform 3D game engine built with modern C++20 and Vulkan, featuring hardware/software ray tracing, real-time global illumination, GPU-driven rendering, and GPU CSM shadows. Target codebase size is <50k LOC of first-party engine code (currently ~85k LOC including all games + tests; see `gnb loc`).

**Key Technologies:**
- C++20/C11, Vulkan API, Slang shader language (ray query, not ray pipeline)
- ECS architecture (entt library) + entt::meta reflection
- QuickJS TypeScript scripting with hot reload (bundled `tools/tsc`)
- Multi-platform: Windows x86_64 / Linux x86_64 / macOS arm64 / Android arm64 / iOS arm64

**Subprojects (under `src/Application/`):**
- Render: gkNextRenderer (main), gkNextBenchmark, gkNextVisualTest
- Editor: gkNextEditor (ImGui editor + node-based material editor)
- Game: MagicaLego, Brotato3D, KongLie3D, BrickPlayer, CharacterDemo, Flappy (Cpp + Js), Voyage3D
- Util: Packager (asset packaging to `.pkg`)

## Build Commands

**Build (vcpkg is auto-bootstrapped on first run):**
- Setup once: `./gnb setup` (Windows: `./gnb.bat setup`)
- Desktop build: `./gnb build` (Windows: `./gnb.bat build`)
- Specific target: `./gnb build gkNextEditor`
- Android: `./gnb android`
- Clean rebuild: `./gnb build --clean`
- Force vcpkg update: `./gnb setup --refresh`

**Targeted builds (IMPORTANT — prefer over full `gnb build`):**
随着 program 增多，全量 `gnb build` 很慢。AGENT 在验证改动时**默认只构建受影响的目标**，不要无脑全量构建：
- **改动 Engine 层**（`src/Engine/**`、shaders、公共 runtime/reflection）：只需 `./gnb build gkNextRenderer` + `./gnb build gkNextUnitTests`（可写成 `./gnb build gkNextRenderer gkNextUnitTests`）。这两个目标编译通过即代表 engine API 没有破坏面上调用。
- **改动某个具体 program**（`src/Application/**` 下的单一子项目，如 MagicaLego、Brotato3D、ScadStudio 等）：只构建该目标自身，例如 `./gnb build MagicaLego`。
- **改动 gnb / tools / 纯文档**：无需 C++ 构建。
- **大型 engine 重构、改动 ABI/广泛 header、不确定影响面，或用户明确要求**：才执行全量 `./gnb build --reconfigure`，确认所有 program 都能编译。
- 增量构建无需 `--reconfigure`；仅在改了 CMake/preset/新增文件未被 glob 收录时才加 `--reconfigure`。

**CMake presets:** `windows`, `linux`, `macos-arm64`, `ios`.

**Optional Features:**
- AVIF is manual: `cmake --preset windows -DENABLE_AVIF=ON -DVCPKG_MANIFEST_FEATURES=avif` then `./gnb build`
- DLSS/Streamline is always enabled on Windows and disabled elsewhere
- OIDN and MinGW support have been removed

**Build output:** `out/build/<platform>/bin/`

## Run Commands

- Default target: `./gnb run`
- Specific target: `./gnb run gkNextEditor`
- Editor shortcut: `./gnb editor`
- Visual test shortcut: `./gnb visual`
- Android: `./gnb android`
- Optional assets: `./gnb paks fetch` / `./gnb paks list`
- Source-line stats: `./gnb loc` (CLI) — also browsable in `./gnb dashboard`
- Local web dashboard: `./gnb dashboard` (todo / build / run / test / git / chat / LOC tabs)

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
gnb shot --target ScadStudio --scene assets/scad/beer_cup.scad --frames 60
```

机制（引擎层统一实现，所有 target 行为一致）：
- 底层是 `--agent-validation` flag：渲染到 `--agent-validation-frames`（默认 90）后，截图到**固定路径** `out/build/<preset>/screenshots/agent_validation.jpg`（覆盖式，无时间戳），随后**自动退出**。
- 窗口用 `SDL_WINDOW_HIDDEN` 创建：**不弹窗、不抢焦点**，不会打断你的 dev loop；present 自动切 immediate mode，渲染不受 vsync 限制，wall-clock 很快（几秒一张图）。
- AGENT 读那张 `agent_validation.jpg` 即可肉眼判断。需要换帧数用 `--frames`，换输出路径用 `--agent-validation-out <path-without-ext>`。

**何时用哪条路径：**
- **改了某个场景/材质/光照/着色，只想看一眼对不对** → `gnb shot --scene <X>`（最轻、最快）。
- **做渲染回归、需要和 baseline 对比 / 一次性扫多个场景** → 跑全量 `gkNextVisualTest`（生成 report + baseline diff + manifest，较重）。

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
├── Engine/                  # Engine library (gkNextEngine.lib)
│   ├── Common/              # CoreMinimal.hpp + shared platform abstraction
│   ├── Runtime/             # Engine runtime: ECS, scripting, reflection, command history
│   ├── Assets/              # Asset loading (glTF/textures), Scene, GPU resources, CPU acceleration
│   ├── Vulkan/              # Vulkan backend + RayTracing/ (HW ray tracing)
│   ├── Rendering/           # Render pipelines
│   │   ├── PathTracing/     # Full path tracing
│   │   ├── SoftwareTracing/ # Software ray tracing
│   │   ├── SoftwareModern/  # Modern rasterization + software GI + NoAmbient deferred
│   │   ├── Shadow/          # GPU CSM shadow pass (4 cascades, bindless sampling)
│   │   └── PipelineCommon/  # Shared pipeline utilities
│   ├── NextGameplay/        # Gameplay primitives shared across games
│   └── Utilities/           # Misc helpers
├── Application/             # Subproject entry points (per role)
│   ├── Render/, Editor/, Game/, Util/   # See "Subprojects" above
├── Tests/                   # Catch2 unit tests (gkNextUnitTests)
└── ThirdParty/              # Third-party code (DO NOT MODIFY)

assets/
├── shaders/                 # Slang shaders (.slang)
├── configs/                 # Runtime config (visual_test.json, ai_config.json, ...)
├── models/                  # glTF scenes
├── scripts/                 # TypeScript scripts (hot-reloadable via QuickJS)
└── typescript/              # TypeScript definitions for QuickJS scripting

tools/gnb/                   # Project CLI (Go) — see "gnb" section below
```

## Key Architectural Patterns

**Reflection System (entt::meta):**
- Provides auto-generated editor UI via PropertyPanel
- Exposes component properties to QuickJS JavaScript bindings
- Supports undo/redo for property modifications
- See `AGENT_GUIDE/ReflectionSystem.md` for detailed documentation
- Register components using `REFLECT_COMPONENT` macro in component's .cpp file
- TypeScript definitions in `assets/typescript/Engine.d.ts` mirror reflected properties

**QuickJS Scripting:**
- TypeScript hot reload support via bundled `tools/tsc/tsc[.exe]` (`tsc.exe` on Windows, `tsc` on macOS/Linux); no Node/npm/global `tsc` dependency is required at runtime
- ES module loading supports compiled TypeScript relative imports under `assets/scripts`
- Components reflected via `entt::meta` are auto-exposed to JavaScript
- Global namespace: `Global.GetEngine()`, `Global.GetScene()`, `Global.spdlog()`
- Scripted games should extend `assets/typescript/NextGameInstanceBase.ts` and call `RunGameInstance(new YourGameInstance())`
- Scene API: `Scene.FindNodeIdWithComponent()`, `Scene.GetNodeById()`, `SceneBuild.*` for rebuild-time procedural scene construction, `Scene.AddRenderNode()` for runtime nodes
- See `AGENT_GUIDE/QuickJSBindings.md`; `FlappyCpp` / `FlappyJs` replay parity is the binding regression demo

**Component System:**
- ECS via entt library
- All components inherit from `Assets::Component`
- Must implement `GetMetaType()` for reflection support
- Common components: RenderComponent, PhysicsComponent, SkinnedMeshComponent

**Rendering / Shadows:**
- Three rendering paths: PathTracing (HW RT), SoftwareTracing (SW DDA on ambient cubes), SoftwareModern (rasterizer + GPU CSM). `SwModernNoAmbient` is the deferred Lambert+IBL+CSM-only variant.
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
   - Engine 层改动：`./gnb build gkNextRenderer gkNextUnitTests`（Windows: `./gnb.bat build ...`）
   - 单个 program 改动：`./gnb build <该 target>`
   - 仅大型重构 / 广泛 header / ABI 改动 / 用户要求时才用 `./gnb build --reconfigure` 全量验证
2. **Run:** Verify application starts and logs `uploaded scene [...] to gpu`
3. **Test:** Run unit tests if touching core systems
4. **Visual:** 渲染类改动 → `gnb shot --scene <X>` 截一张图肉眼验证（不弹窗、自动退出，见上文 "Agent Visual Validation"）；需要 baseline 回归再跑 `gkNextVisualTest`

**Assistant Note:** 只有大型重构或不确定影响面的改动，才需要全量 `gnb build --reconfigure` 并修复全部编译错误后再报告完成；常规改动用对应的 targeted build 即可。

## Key References

- **AGENT_GUIDE/** - Layered documentation:
  - `core-patterns.md` / `contextual-rules.md` / `coding-standards.md` / `quick-commands.md` - General rules
  - `ReflectionSystem.md` - entt::meta reflection (editor UI + JS bindings)
  - `QuickJSBindings.md` - JS/TS engine bindings and replay parity demo
  - `HotReload.md` - Shader/script hot reload mechanics
  - `LDrawLoader.md` - LDraw model loading (used by MagicaLego/BrickPlayer)
  - `SCADLoader.md` - OpenSCAD (.scad) DSL loading (parser/evaluator/CSG via Manifold/text via FreeType)
  - `PrefabSceneWorkflow.md` - KayKit procedural scene prefab workflow
  - `MagicaLego.md` - MagicaLego subproject notes
  - `Brotato3D.md` - Brotato3D code structure (god-class + per-system split, runtime data model, object pools)
  - `CharacterDemo.md` - CharacterDemo + NextGameplay shared layer (CharacterActor facade, ECS components, NavGrid A*, AI behavior tree)
- **README.en.md** - Project overview and quick start
- **.clang-tidy** - Naming conventions (source of truth)

## gnb Dashboard

`gnb dashboard` 启动本地 HTTP UI（默认 127.0.0.1:某端口，自动开浏览器），htmx 驱动的 SPA。提供 tabs：
- **TODO**：可视化 `.spec/TODO.md` 的工作流操作（增删改、move、spec 创建、标 done/blocked）
- **Build / Run / Test**：触发 cmake build、运行 target、Catch2 测试，SSE 实时流日志
- **Git**：分支管理、stash、commits、本地改动 stage/unstage、LLM 生成 commit message
- **Chat**：直接对接本地 llama-server，流式 + 工具调用，多会话归档
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

引擎层 `NextAI::FAIService` 通过 `localllm` provider 复用同一个 llama-server（无需独立模型进程）：默认读 `external/llm/run/server.pid` 自动发现 host/port/model，PID 文件缺失或解析失败时回退到 `ai_config.json` 的 `localllm.endpoint`。`gnb llm serve --model <id>` 切模型时引擎会在下次 Chat 调用前重读 PID，无需重启编辑器。

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
