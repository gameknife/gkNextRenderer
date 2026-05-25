# AGENTS.md

This file is the single source of truth for AI coding assistants (Claude Code, Gemini CLI, Codex, GitHub Copilot, Cursor, etc.) working in this repository. `CLAUDE.md` imports it via `@AGENTS.md`; other tools read `AGENTS.md` directly.

## Communication Preference

**Language: 中文 (Chinese)**
Always communicate with the user in Chinese (中文).

## Project Overview

gkNextRenderer is a cross-platform 3D game engine built with modern C++20 and Vulkan, featuring hardware/software ray tracing, real-time global illumination, and GPU-driven rendering. Target codebase size is <50k LOC (currently ~15k).

**Key Technologies:**
- C++20/C11, Vulkan API, Slang shader language
- ECS architecture (entt library)
- QuickJS TypeScript scripting with hot reload
- Multi-platform: Windows x86_64 / Linux x86_64 / macOS arm64 / Android arm64 / iOS arm64

**Subprojects:**
- gkNextRenderer (main renderer)
- gkNextEditor (ImGui editor with node-based material editor)
- MagicaLego (voxel building game with AI assistant)
- gkNextBenchmark
- gkNextVisualTest (automated visual testing)
- Packager (asset packaging to `.pkg`)

## Build Commands

**Build (vcpkg is auto-bootstrapped on first run):**
- Setup once: `./gnb setup` (Windows: `gnb.bat setup`)
- Desktop build: `./gnb build` (Windows: `gnb.bat build`)
- Specific target: `./gnb build gkNextEditor`
- Android: `./gnb android`
- Clean rebuild: `./gnb build --clean`
- Force vcpkg update: `./gnb setup --refresh`

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
- Android: `./gnb android`

Desktop binaries can now be launched from any working directory; no `cd out/build/<preset>/bin` is required.

**Runtime success indicator:** Log shows `uploaded scene [...] to gpu`

## Testing

Tests no longer require the current working directory to be `bin`; launch them via their executable path.

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
├── Runtime/           # Core engine runtime
│   ├── Platform/      # Platform abstraction (via PlatformCommon.h)
│   ├── Components/    # ECS components (entt)
│   ├── Reflection/    # Property reflection (entt::meta) for editor + JS bindings
│   └── Command/       # Command history system (undo/redo)
├── Vulkan/            # Vulkan backend
│   └── RayTracing/    # Hardware ray tracing
├── Rendering/         # Render pipelines
│   ├── PathTracing/   # Full path tracing
│   ├── SoftwareTracing/  # Software ray tracing
│   ├── SoftwareModern/   # Modern rasterization + software GI
│   └── PipelineCommon/   # Shared pipeline utilities
├── Assets/            # Asset loading (glTF, textures, etc.)
├── Tests/             # Catch2 unit tests
├── Application/       # App entry points grouped by role
│   ├── Editor/
│   │   └── gkNextEditor/
│   │       ├── Panels/    # Property panel (auto-generated from reflection)
│   │       ├── Nodes/     # Node-based material editor
│   │       └── Overlays/  # Editor overlays and chrome
│   ├── Game/
│   │   ├── MagicaLego/
│   │   ├── BrickPlayer/
│   │   ├── Brotato3D/
│   │   ├── CharacterDemo/
│   │   ├── Flappy/
│   │   ├── KongLie3D/
│   │   └── Voyage3D/
│   ├── Render/
│   │   ├── gkNextRenderer/
│   │   ├── gkNextBenchmark/
│   │   └── gkNextVisualTest/
│   └── Util/
│       └── Packager/
└── ThirdParty/        # Third-party code (DO NOT MODIFY)

assets/
├── shaders/           # Slang shaders (.slang)
├── configs/           # Runtime config (visual_test.json, ai_config.json)
├── models/            # glTF scenes
└── typescript/        # TypeScript definitions for QuickJS scripting
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

1. **Build:** For AI assistant verification, run the platform default through gnb:
   - macOS/Linux: `./gnb build --reconfigure`
   - Windows: `gnb.bat build --reconfigure`
   - If only one target needs verification, pass it as `./gnb build <target>`
2. **Run:** Verify application starts and logs `uploaded scene [...] to gpu`
3. **Test:** Run unit tests if touching core systems
4. **Visual:** For rendering changes, validate visually in gkNextRenderer or run gkNextVisualTest

**Assistant Note:** Large refactors must include a full `gnb build --reconfigure` and fix any compile errors before reporting completion.

## Key References

- **AGENT_GUIDE/** - Layered documentation:
  - `core-patterns.md` - Essential patterns and commands (Layer 1)
  - `contextual-rules.md` - Context-specific rules (Layer 2)
  - `coding-standards.md` - Detailed code review guidelines
  - `quick-commands.md` - Command reference (Layer 3)
  - `ReflectionSystem.md` - Reflection system documentation
  - `PrefabSceneWorkflow.md` - KayKit procedural scene prefab workflow and review rules
  - `MagicaLego.md` - MagicaLego subproject notes
- **README.en.md** - Project overview and quick start
- **.clang-tidy** - Naming conventions (source of truth)

## Local LLM (gnb llm)

gnb 集成了基于 **llama.cpp + Gemma 4** 的本地 LLM，用于离线辅助任务（首个用例：自动生成 commit message）。当前默认模型为 **gemma-4-E4B-it (Q4_K_M)**，备选模型为 **gemma-4-E2B-it (Q4_K_M)**，两者默认按 128K context（`context = 131072`）启动；可在 `gnb.toml` 的 `[external.llm].active` 切换，也可在命令上用 `--model <id>` 临时覆盖。

**首次安装**（下载 llama.cpp 预编译二进制 + GGUF 模型到 `external/llm/`）：

```bash
gnb llm setup                       # 下载 active 模型
gnb llm setup --model <id>          # 下载指定模型
gnb llm setup --all                 # 下载所有配置的模型
```

**模型管理**：

```bash
gnb llm models    # 列出所有配置模型，标星号者为 active，并显示是否已下载
```

**生命周期管理**：

```bash
gnb llm serve                       # 后台拉起 llama-server（OpenAI 兼容 HTTP，默认 127.0.0.1:8765）
gnb llm serve --model <id>          # 用非 active 模型启动（已在跑且模型不同会自动重启）
gnb llm status                      # 查看 PID / endpoint / active 模型 / 实际运行的模型
gnb llm stop                        # 关掉后台 server
gnb llm chat "你好"                 # 一次性 prompt（自动按需启动 server）
gnb llm chat --model <id> "你好"    # 一次性切换模型（必要时重启 server）
```

**MVP：根据 local change 生成 commit message**：

```bash
gnb git commit-msg                  # 仅生成并打印
gnb git commit-msg --stage-all      # 先 git add -A 再生成
gnb git commit-msg --commit         # 生成后直接 git commit
gnb git ai-commit                   # commit-msg 的短别名
gnb git commit-msg --dry-run        # 仅打印将要发送给 LLM 的完整 prompt，不调用模型
gnb git commit-msg --model <id>     # 用指定模型生成（与上面各开关可叠加）
```

Prompt 内容包含：模式（staged / working tree）、文件清单（含 `??` 未跟踪）、`git diff --stat` 总览、已跟踪文件 diff、未跟踪文件合成的 `+++ b/<path>` 新文件 diff（带二进制/64KB 大小保护）。当总字节超出 `--max-diff-chars` 时按文件边界截断而不是字节硬切。

诊断要点：
- llama.cpp 版本、模型 URL、端口在 `gnb.toml` 的 `[external.llm.*]` 段配置
- server 日志：`external/llm/run/server.log`
- PID/端口快照：`external/llm/run/server.pid`
- Windows / Linux 默认拉 **Vulkan 后端**，利用本项目已有的 Vulkan SDK；macOS 走 Metal
- Gemma 4 需要新版 llama.cpp；如更新模型遇到加载失败，先升级 `external.llm.llama.version`
- 多模型按 `[[external.llm.models]]` 数组配置，`[external.llm].active` 指定默认，每个模型的 GGUF 落在 `external/llm/models/<file>.gguf`，可并存
- PID 文件第 4 行记录当前 server 加载的模型 id；切换 active 后 `gnb llm serve` / `chat` / `git commit-msg` 会自动停掉旧 server 再重启

## Spec Workflow

**完整规范见 [.spec/README.md](.spec/README.md)**。下面是 AGENT 必须遵守的核心规则，规范与本节不一致以 README 为准。

文件位置：
- 任务列表：`.spec/TODO.md`
- 任务详细规格（可选，仅复杂任务）：`.spec/specs/<id>.md`
- 完成报告（一任务一文件）：`.spec/journal/<id>.md`
- 卡住提问（一任务一文件）：`.spec/blockers/<id>.md`
- 归档：`.spec/ARCHIVE.md`

执行规则：

1. 用户触发"启动交互式工作流"时，读取 `.spec/TODO.md`
2. 在"下一步"段找第一个 `[ ]` 任务
3. 若 `.spec/specs/<id>.md` 存在，先读它再执行
4. 执行任务
5. 完成后：
   - 在 TODO.md 把该任务的 `[ ]` 改为 `[x]`
   - 行末追加 ` → journal/<id>.md (YYYY-MM-DD)`
   - 写 `.spec/journal/<id>.md`（frontmatter + 做了什么 + 改动文件 + 风险/遗留）
   - 进入**活跃模式**：重置等待计数器为 0，后续 10 次等待每次只等 60 秒
6. 回到 2，继续下一个 `[ ]` 任务
7. "下一步"段没有 `[ ]` 任务时（**这不是结束条件，必须继续轮询**）：
   - 比较 TODO.md mtime 与 `.spec/journal/` 目录中最新文件的 mtime
   - TODO 较新 → 用户可能加了任务，回到 2 重扫
   - 否则按当前模式执行 sleep command 等待，等待期间不要思考，这时 TODO.md 可能更新：
     - **活跃模式**（等待计数器 < 10）：sleep 60 秒，计数器 +1，**sleep 返回后立即回到步骤 2，不要输出总结/结束 turn**
     - **低功耗模式**（等待计数器 ≥ 10）：sleep 600 秒，**sleep 返回后立即回到步骤 2，不要输出总结/结束 turn**
   - 任一任务完成（步骤 5）会重置回活跃模式
   - **禁止行为**：在此步骤输出"无任务可做"、"工作流完成"、"等待用户指示"之类的话并结束 session。唯一允许的输出是简短状态行（如"无任务，活跃等待 60s"），然后必须紧接 sleep 工具调用
8. **唯一退出条件**：里程碑状态被改为 `done` → 退出工作流。除此之外（包括"下一步"段为空、所有任务已完成但里程碑未 done），都必须按步骤 7 继续轮询

特殊情况：
- 任务歧义无法判断时：写 `.spec/blockers/<id>.md`，任务状态改 `[!]`，**跳过该任务继续做下一个**，不要瞎猜
- 启动工作流时若"最近完成"段超过 10 条：在首次回复中提醒用户运行 `gnb todo archive`，但不要自己归档
- 用户在工作流期间修改 TODO.md：下一轮重扫时会发现

边界：
- AGENT **可改**：TODO.md 中任务的状态字符、行末 journal 链接；`journal/`、`blockers/` 下的文件
- AGENT **不可改**：TODO.md 中任务标题/ID/优先级/类型/所属段落；`specs/` 下的文件；`ARCHIVE.md`；"待规划"段任何任务
- 不要建立自动化任务（hooks、scheduled tasks 等）
- 不要调用其他 agent 处理任务
