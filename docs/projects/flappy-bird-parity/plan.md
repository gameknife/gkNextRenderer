# Flappy Bird Parity — C++ 与 QuickJS/TypeScript 双语言实现计划

## Context

引擎已经过一轮收束（commits `4bc633f9 Refactor engine cleanup APIs`、`10be993a engine application refactoring`），是时候用一个**比 Brotato3D 更小**的 demo 来验证「同一引擎在 C++ 与 QuickJS/TS 两条路径下能否产出完全一致的运行结果」。

**目标**：实现两份 Flappy Bird —
- **`FlappyCpp`**：标准 `NextGameInstanceBase` 子类（参考 Brotato3D / KongLie3D 写法），作为参考实现 / 行为基准（baseline）。
- **`FlappyJs`**：宿主侧只放最薄的壳（一个空 `NextGameInstanceBase`，把 lifecycle/input 全部桥接给 QuickJS），所有玩法逻辑写在 `assets/typescript/flappy/*.ts`。

**核心约束**：在固定随机种子 + 固定输入序列下，两份实现的逐帧鸟坐标 / 管道坐标 / 分数 / 死亡帧编号必须严格一致（用作 parity 验收）。

**这件事的真正价值**：Flappy Bird 玩法极简，但要把它**完全用 TS 实现**，会暴露 QuickJS 绑定的所有缺口（输入、音频、场景增删、相机、计时、UI、ESM 模块加载）。每补一个缺口，引擎对脚本化游戏的支撑就强一档。把 C++ 版本当成"绑定缺什么"的清单生成器。

---

## ⚠️ 阶段化执行（先 C++、后 JS — 不要一口气做完）

```
Stage 1: FlappyCpp  ──┐
                      ├─→  ⏸ 用户验收 Gate ──→  Stage 2: 绑定补齐 + FlappyJs ──→  Stage 3: Parity & 文档
```

- **Stage 1 = 仅做 Phase B（FlappyCpp）**。完成后**停下**，向用户提交：可执行文件 + 玩法演示 + `gameplay.json` 中的全部数值。
- **必须等用户在本文档底部 "Stage 1 验收" 段落里**勾选确认，**才能开始 Stage 2**。
- 用户确认 Stage 1 的目的：锁定玩法手感与所有数值，把"参考实现"冻结成 baseline；后续 FlappyJs 是去对齐它，而不是反过来。
- Stage 2 = Phase A（QuickJS 绑定）+ Phase C（FlappyJs），两者顺序执行（A 完成后再 C）。
- Stage 3 = Phase D（Parity 报告 + visual test + 文档）。

> **执行 agent 注意**：如果你看到 Phase B 已完成但本文档底部"Stage 1 验收"未勾选，**不要**继续 Phase A / C。回报用户请求验收。

---

## 玩法规格（两份实现共享，必须严格一致）

> 所有数值/规则在 `assets/configs/flappy/gameplay.json` 中**单一来源**，C++ 与 TS 都从同一个 JSON 读取，避免漂移。

**世界坐标系 / 相机**（侧视，**透视相机**）：

> ⚠️ 引擎当前 `Assets::Camera` 只有 `FieldOfView + ModelView`（见 [src/Assets/Core/Model.hpp:11-20](../../../src/Assets/Core/Model.hpp)），**不支持正交相机**。所有现存 demo（Brotato3D / KongLie3D / MagicaLego）都用 `glm::lookAtRH(...) + FieldOfView` 透视。Flappy Bird 也用透视，靠"相机拉远 + 玩法平面在 Z=0"模拟侧视。

- 玩法平面：`Z = 0`
- 相机：position = `(0, 0, 12)`，target = `(0, 0, 0)`，up = `(0, 1, 0)`，FieldOfView = `50°`
- 视野估算（按 `aspect = 16:9`，z=0 平面距相机 12）：
  - 半视野高 ≈ `12 * tan(25°) ≈ 5.6`
  - 半视野宽 ≈ `5.6 * 16/9 ≈ 9.95`
  - **可视区域**：X∈`[-10, 10]`、Y∈`[-5.6, 5.6]`（数值取整后用作管道生成 / 销毁 / 死亡边界）
- 鸟初始位置：`(-3, 0, 0)`，半径 `0.4`

**物理**（Y 方向）：
- 重力：`gravity = -22.0`（m/s²）
- 跳跃：按下 → 把鸟的 Y 速度**直接设为** `flapVelocity = 7.5`（不累加）
- 速度上下限：`[-10.0, +10.0]`

**管道**：
- 管宽 `1.0`，缝隙高 `2.6`
- 缝隙中心 Y 范围：`[-2.5, +2.5]`（留一些边距给上下管子，防止贴边死）
- 生成节奏：每 `1.6 秒` 在 `X = +12` 生成一对（上下两段），向左以 `speed = 3.0 m/s` 移动
- 销毁：`X < -12`
- 计分：当鸟 X **从左侧**越过管道中心 X 那一帧，`score += 1`

**碰撞**：
- 鸟 vs 管道：AABB（鸟用半径外接矩形 `bird.x ± 0.4` × `bird.y ± 0.4`）
- 鸟 vs 边界：`bird.y < -5.6 + 0.4` 或 `bird.y > +5.6 - 0.4`（即 ±5.2）

**状态机**：
- `Ready`：鸟悬停（不受重力），按 Space / 鼠标左键 / Gamepad A 进入 `Playing` 并立即触发一次 flap
- `Playing`：常规更新
- `Dead`：定格 0.5s（hitstop），任意键 → 重新加载场景，回到 `Ready`

**输入**：
- `Space` / 鼠标左键 / `Gamepad A`：flap
- `Esc`：退出

**HUD**（ImGui）：
- 屏幕顶部居中：当前分数（大字号）
- `Ready` 状态：屏幕中央 `Press Space to Start`
- `Dead` 状态：屏幕中央 `Score: N\nPress Any Key to Restart`

**音效**（可选；缺文件静默不报错）：
- flap：`assets/sounds/flappy_flap.wav`
- score：`assets/sounds/flappy_score.wav`
- hit：`assets/sounds/flappy_hit.wav`

**确定性**（parity 关键）：
- 管道缝隙中心 Y 用**自实现 xorshift32** RNG（不用 `std::mt19937` / `Math.random`，两边实现不一致），种子固定 `0xC0FFEE`
- 时间步长：固定 `dt = 1/60`；引擎给的 `deltaSeconds` 不一定等于 1/60，内部用累加器拆成多步定步长更新
- 输入"序列"在 parity 测试模式下从 `assets/configs/flappy/replay.json` 读取（每帧的 flap 标志），跑完输出 `bird.y` / `score` / `deathFrame` trace

---

## 引擎能力盘点（已具备 vs 待补）

### 已具备（直接复用，不重造）

| 需求 | API | 路径 |
|---|---|---|
| Application 入口 | `NextGameInstanceBase` | [src/Runtime/Engine.hpp:36](../../../src/Runtime/Engine.hpp) |
| 输入回调 | `OnKey / OnGamepadInput / OnMouseButton` | [src/Runtime/Engine.hpp:69](../../../src/Runtime/Engine.hpp) |
| 程序化几何 | `Assets::FProcModel::CreateBox / CreateSphere` | [src/Assets/Loaders/FProcModel.h:12](../../../src/Assets/Loaders/FProcModel.h) |
| 场景动态构建 | `BeforeSceneRebuild` 回调 | [src/Runtime/Engine.hpp:56](../../../src/Runtime/Engine.hpp) |
| 运行时增删 Node | `Scene::AddNode / RemoveNodeByInstanceId` | [src/Assets/Core/Scene.hpp:144](../../../src/Assets/Core/Scene.hpp) |
| 透视相机覆盖 | `OverrideRenderCamera`（参考 Brotato3D） | [src/Application/Brotato3D/Brotato3DEffectSystem.cpp:240](../../../src/Application/Brotato3D/Brotato3DEffectSystem.cpp) |
| ImGui HUD | `OnRenderUI / OnInitUI` | [src/Runtime/Engine.hpp:44](../../../src/Runtime/Engine.hpp) |
| 音频 | `NextAudio::PlaySfx / PlayMusic` | [src/Runtime/Subsystems/NextAudio.h:23](../../../src/Runtime/Subsystems/NextAudio.h) |
| 场景重载 | `NextEngine::RequestLoadScene({"Empty.proc"})` | [src/Runtime/Engine.hpp:222](../../../src/Runtime/Engine.hpp) |
| QuickJS 反射桥 | `entt::meta` + `JSExposed` 自动暴露 | [src/Runtime/Subsystems/QuickJSEngine.cpp:823](../../../src/Runtime/Subsystems/QuickJSEngine.cpp), [AGENT_GUIDE/ReflectionSystem.md](../../../AGENT_GUIDE/ReflectionSystem.md) |
| TS 热重载 | `QuickJSEngine` 自动调 `tsc -p` 检测时间戳 | [src/Runtime/Subsystems/QuickJSEngine.cpp:1028](../../../src/Runtime/Subsystems/QuickJSEngine.cpp) |
| JSON 解析 | nlohmann-json | `vcpkg.json` |
| CMake 注册套路 | KongLie3D / Brotato3D | [src/cmake/SourceFiles.cmake:95](../../../src/cmake/SourceFiles.cmake), [src/CMakeLists.txt:127](../../../src/CMakeLists.txt) |

### 待补的 QuickJS 绑定（**Stage 2 / Phase A 工作量**）

| 缺口 | 现状 | 需要新增 | 优先级 |
|---|---|---|---|
| **ESM 模块加载** | ⚠️ 当前 `QuickJSEngine.cpp` **没有调用 `JS_SetModuleLoaderFunc`**，唯一可 import 的模块是 C++ 通过 `context_->addModule("Engine")` 注册的 "Engine"。任何相对路径 import（`import { x } from "./bar"`）都会**直接抛 ReferenceError**。`tsconfig.json` 的 `"files": ["test.ts"]` 也只编一个入口文件 | 见下面 "Phase A · A0" 详细方案 | **P0 阻塞** |
| **引擎计时** | 无 `GetTime / GetDeltaSeconds` 暴露；callback 入参的 delta 已可用 | `module.class_<NextEngine>` 加 `GetTime / GetDeltaSeconds / GetSmoothDeltaSeconds` | P0 |
| **输入查询** | JS 无法读键盘/鼠标/手柄状态 | `Engine.Input` 子对象：`IsKeyDown / IsKeyPressed / IsMouseButtonDown / GetGamepadButton`，宿主在 `Tick` 前刷新 frame-cache | P0 |
| **音频** | 无 | `Engine.Audio.PlaySfx(path, volume)` / `PlayMusic / StopMusic` | P0 |
| **场景动态构建** | JS 只能改已有 Node 属性，无法新建 / 删除 | `Scene.AddBoxNode / AddSphereNode → nodeId`、`Scene.RemoveNodeById(nodeId)`；底层包 `FProcModel + SceneBuilder + Scene::AddNode` | P0 |
| **生命周期 Hook** | JS 只有 Tick callback，没有 OnInit / OnDestroy / OnSceneLoaded | `Engine.RegisterLifecycleHooks({ onInit, onDestroy, onSceneLoaded })` | P1 |
| **相机覆盖** | 无 | `Engine.SetOverrideCamera({ position, target, up, fov })`（**仅透视**，不要假装支持 ortho）；宿主 `OverrideRenderCamera` 中读取 | P0 |
| **ImGui 绑定** | 无 | 最小子集：`Engine.UI.Begin/End/Text/SetCursorPos/PushFont/GetWindowSize`；调用时机：宿主 `OnRenderUI` 触发 JS 注册的 `onRenderUI` 回调 | P1 |
| **场景重载触发** | 无 | `Engine.RequestLoadScene("Empty.proc")` | P1 |
| **配置 JSON 加载** | JS 没有 fs，但 `PakSystem` 已能读 asset | `Engine.LoadJson(assetPath) → any`（`PakSystem.LoadFile` + `nlohmann::json::parse` + `QuickJSTypeConverter` 转 JSValue） | P0 |
| **退出 / 屏幕尺寸** | 无 | `Engine.RequestClose()` / `Engine.GetScreenSize()` | P1 |

> **绑定原则**：每个 binding 同步更新 `BuildTypeScriptDefinitions()`（`QuickJSEngine.cpp:823`）让 `Engine.d.ts` 自动重新生成；TS 端只引用 `import * as NE from "Engine"`，不依赖手写类型。

---

## 文件结构（最终态）

```
src/Application/Flappy/
├── FlappyCommon.hpp                  # 玩法常量、坐标系、配置 struct（C++ 与 JS host 共用）
├── FlappyConfig.hpp/cpp              # JSON 加载（gameplay.json + replay.json）
├── FlappyCpp/
│   ├── FlappyCppGameInstance.hpp/cpp # 完整 C++ 实现（OnInit/OnTick/OnRenderUI/OnKey…）
│   ├── FlappyCppBird.hpp/cpp         # 鸟运行时（位置 / 速度 / flap / 状态机）
│   ├── FlappyCppPipes.hpp/cpp        # 管道生成 / 移动 / 计分 / 碰撞
│   ├── FlappyCppHud.cpp              # ImGui HUD
│   └── FlappyCppRng.hpp              # xorshift32（确定性 RNG）
└── FlappyJs/
    └── FlappyJsGameInstance.hpp/cpp  # 极薄壳；只 register lifecycle 给 QuickJS

assets/typescript/flappy/
├── tsconfig.json                     # 单独 outDir；module=ESNext；rootDir=.
├── main.ts                           # 入口：注册 onInit/onTick/onRenderUI
├── bird.ts                           # 鸟逻辑（与 FlappyCppBird 一一对应）
├── pipes.ts                          # 管道逻辑
├── rng.ts                            # xorshift32（与 FlappyCppRng 算法完全一致）
└── config.ts                         # 加载 + 校验 gameplay.json

assets/configs/flappy/
├── gameplay.json                     # 物理 / 管道 / 视野 / 颜色（两份实现共享）
└── replay.json                       # parity 测试用的固定输入序列

assets/sounds/                        # 可选；无文件时静默
├── flappy_flap.wav
├── flappy_score.wav
└── flappy_hit.wav

docs/projects/flappy-bird-parity/
├── plan.md                           # 本文档
└── parity-report.md                  # （Stage 3 产出）逐帧 diff 的对比报告
```

---

# Stage 1 · FlappyCpp（参考实现）

> ⚠️ Stage 1 完成后 **必须停下** 等用户验收，详见本文档底部"Stage 1 验收"段。

### Phase B · FlappyCpp（**不依赖任何 QuickJS 改动**）

- **B1 · 工程骨架**
  - `src/cmake/SourceFiles.cmake` 加 `src_files_flappycpp` glob：`Application/Flappy/FlappyCommon.hpp`、`Application/Flappy/FlappyConfig.*`、`Application/Flappy/FlappyCpp/*`
  - `src/CMakeLists.txt` 加 `add_executable(FlappyCpp ${src_files_flappycpp} DesktopMain.cpp)` + 加进两份 `AllTargets`（MINGW + 默认）
  - `FlappyCppGameInstance` 配窗口 1280×720，加载 `Empty.proc`
  - 实现 `OverrideRenderCamera`：参考 [Brotato3DEffectSystem.cpp:240](../../../src/Application/Brotato3D/Brotato3DEffectSystem.cpp)，`lookAtRH((0,0,12), (0,0,0), (0,1,0))` + `FieldOfView=50.0f`
  - 验收：能编译并启动到空场景；`./run.bat --target FlappyCpp` 成功

- **B2 · 配置加载 + RNG**
  - 写 `FlappyConfig`：从 `assets/configs/flappy/gameplay.json` 反序列化所有数值
  - 写 `FlappyCppRng`（xorshift32），单元测试 `tests/Unit/FlappyRngTest.cpp` 验证前 100 次输出与一组**手算金标**对齐（金标列在 `FlappyCppRng.hpp` 注释里，方便后续 TS 端对齐）
  - 验收：单测通过 `gkNextUnitTests "[Unit][FlappyRng]"`

- **B3 · 鸟 + 物理 + 输入**
  - `BeforeSceneRebuild` 里建鸟（sphere + lambertian 黄色）+ 地面（box 绿色，y=-5.6 高 0.4） + 天花板（同色，y=+5.6） + 远处一片 lambertian 蓝色背景板（z=-2）
  - 固定步长（1/60）累加器；`Ready / Playing / Dead` 状态机
  - `OnKey` 处理 Space / Esc；`OnMouseButton` 处理左键；`OnGamepadInput` 处理 A 键
  - 验收：能跳，能落地，能死

- **B4 · 管道生成 + 计分 + 碰撞**
  - 管道用 box 模型 + 灰色 lambertian
  - 每 1.6s 生成（用 RNG 决定缝隙中心 Y），向左移动；`X < -12` 销毁
  - AABB 碰撞 + 计分
  - 验收：完整玩法可玩到死

- **B5 · HUD + 音效 + 重启**
  - ImGui 中心绘制分数 / Ready / Dead 三种文案
  - `Dead` 后任意键 → `RequestLoadScene("Empty.proc")` 复位
  - 调 `engine_->GetAudio()->PlaySfx(...)`
  - 验收：完整循环可玩

- **B6 · Replay parity 输出（C++ 端 trace）**
  - 加一个 CLI flag `--flappy-replay`（在 `FlappyCppGameInstance::OnInit` 中读 `Options::CmdLineArgs`，或者从环境变量 `FLAPPY_REPLAY=1` 读）：
    - 启动后从 `replay.json` 读输入序列
    - 跑完关闭并写 `out/flappy_cpp_trace.json`（每帧 `frame, bird.y, bird.vy, score, state`）
  - 验收：跑两次 trace 完全一致（确定性自检）

### ✅ Stage 1 完成标准

1. `./run.bat --target FlappyCpp` 能启动并完成完整玩法循环（Ready → Playing → 死 → 重启）
2. `gkNextUnitTests "[Unit][FlappyRng]"` 通过
3. `FlappyCpp.exe --flappy-replay` 跑两次产生**完全相同**的 `flappy_cpp_trace.json`
4. `assets/configs/flappy/gameplay.json` 完整、所有数值已 freeze
5. `full-windows` preset 全量编译无 warning（按 AGENTS.md "Verification After Changes" 要求）

---

## ⏸ Stage 1 验收（用户填写）

> Stage 1 完成的 agent 把以下条目填好后**停止**，等用户勾选 ✅ 才进入 Stage 2。

- [ ] 玩法手感符合预期（重力 / 跳跃 / 管道节奏）
- [ ] HUD 文案、字号、位置 OK
- [ ] `gameplay.json` 数值已最终确认（任何修改都同步进 git）
- [ ] `flappy_cpp_trace.json` 用作 baseline 已归档（路径写在下面）

**Baseline trace 路径**：`out/flappy_cpp_trace.json`

**用户备注**：（agent 不要填，留给用户）

---

# Stage 2 · QuickJS 绑定补齐 + FlappyJs

> ⚠️ **进入此阶段的前提**：上面"Stage 1 验收"四个条目都被用户勾选 ✅。否则不要开始。

### Phase A · QuickJS 绑定补齐（引擎工作）

> 完成顺序按 A0 → A7；每张卡都要：
> 1. C++ 改 `QuickJSEngine.cpp` + 必要子系统接口
> 2. 在 `BuildTypeScriptDefinitions()` 加 d.ts 行（如适用）
> 3. 在 `assets/typescript/test.ts` 写一段最小调用，跑通后**保留**作为回归样本
> 4. 跑 `full-windows` preset 全量编译，启动 `gkNextRenderer` 验证日志 `uploaded scene [...] to gpu`

#### A0 · ESM 模块加载器（**P0，最优先**）

**问题现状**：
- `QuickJSEngine::ResetContextAndLoadScript()`（[QuickJSEngine.cpp:899](../../../src/Runtime/Subsystems/QuickJSEngine.cpp)）只对 `assets/scripts/test.js` 做了一次 `eval(..., JS_EVAL_TYPE_MODULE)`
- 没有调用 `JS_SetModuleLoaderFunc`，QuickJS 的运行时层面**没有任何模块解析能力**
- 唯一能用的是 `import * as NE from "Engine"` —— 因为 "Engine" 是 C++ 用 `context_->addModule("Engine")` 注册到模块表的内置模块
- 任何 `import { x } from "./bar"` / `import { x } from "../foo/baz"` 都会以 **`ReferenceError: could not load module './bar'`** 结束

**目标**：让 TS 项目能多文件分模块，`bird.ts` / `pipes.ts` 用相对路径相互 import。

**实现方案**（推荐，长期收益）：

1. 在 `QuickJSEngine::ResetContextAndLoadScript()` 中，创建完 `runtime_ + context_` 之后，调用 `JS_SetModuleLoaderFunc(rt, nullptr, &ModuleLoader, nullptr);`

2. 实现 `ModuleLoader`：
```cpp
static JSModuleDef* ModuleLoader(JSContext* ctx, const char* moduleName, void* opaque)
{
    // 1. 内置模块"Engine"由 addModule 注册，QuickJS 会先查内置表，不会进到这里
    //    所以这里只处理外部文件
    // 2. moduleName 已经是 QuickJS normalized 后的路径（默认 normalizer 会处理 "./"/"../"）
    //    例：当前模块 "assets/scripts/main"  里 import "./bird" → moduleName = "assets/scripts/bird"

    std::string assetPath = std::string(moduleName);
    // 自动补 .js 后缀
    if (assetPath.size() < 3 || assetPath.substr(assetPath.size()-3) != ".js") {
        assetPath += ".js";
    }

    std::vector<uint8_t> buffer;
    if (!Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(assetPath, buffer)) {
        JS_ThrowReferenceError(ctx, "could not load module '%s'", moduleName);
        return nullptr;
    }

    JSValue funcVal = JS_Eval(ctx,
        reinterpret_cast<const char*>(buffer.data()), buffer.size(),
        moduleName,
        JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(funcVal)) return nullptr;

    // import.meta 设置（用于 TS source map 等）
    js_module_set_import_meta(ctx, funcVal, /*use_realpath*/ false, /*is_main*/ false);
    JSModuleDef* m = (JSModuleDef*)JS_VALUE_GET_PTR(funcVal);
    JS_FreeValue(ctx, funcVal);
    return m;
}
```

3. **入口加载方式调整**：把 `assets/scripts/test.js` 的加载也改成"以模块名 `assets/scripts/test` import"的形式（调一次 `JS_Eval` 走 module loader），这样从入口就能 `import "./bird"` 到 `assets/scripts/bird.js`。

4. **路径规范化**：QuickJS 默认的模块名 normalizer 会把 `"./bird"`（在 `assets/scripts/main` 中）解析成 `assets/scripts/bird`（不带 .js）。loader 里手动补 `.js` 即可。如果默认 normalizer 不够用，再注册自定义 normalizer。

5. **测试方法**：
   - 把 `assets/typescript/` 下加两个文件：`helper.ts` 导出一个常量、`test.ts` `import { x } from "./helper"` 然后打印
   - tsconfig 的 `"files"` 改成 `["test.ts", "helper.ts"]` 让两个都编译
   - 启动后日志应显示 helper 里那个常量

6. **失败兜底**：如果实现 module loader 工作量超出预期（>1 天），**降级方案**：
   - 改 tsconfig：`"module": "none"` + `"outFile": "../../assets/scripts/test.js"` —— 但这要求 ts 代码不能用 `import/export` 语法，必须用全局 `namespace` 或者 `///<reference path="..."/>`
   - 这条路能保住 Stage 2 不卡死，但写法别扭，不推荐
   - **走降级方案前必须先 escalate 给用户确认**，因为它会污染后面所有 TS 代码风格

**验收**：
- TS 端写 `bird.ts` + `pipes.ts` + `main.ts`，main 里 `import` 前两者，能正常运行
- `assets/typescript/test.ts` 里加一段 `import { x } from "./helper"` 的回归测试，hot reload 也能跑

#### A1 · 计时

- `module.class_<NextEngine>` 加 `GetTime / GetDeltaSeconds / GetSmoothDeltaSeconds`
- d.ts 同步
- 验收：TS 中 `console.log(NE.Global.GetEngine().GetTime())` 每秒递增

#### A2 · 生命周期 Hooks

- 新增 `Engine.RegisterLifecycleHooks({ onInit, onDestroy, onSceneLoaded })`
- 宿主侧：`FlappyJsGameInstance` 在对应虚函数里 fan-out
- 因为 `RegisterLifecycleHooks` 接收一个 JS 对象，不直接走反射桥，需要手写一个 `JS_NewCFunction` 把对象拆成 4 个 `JSValue` 存在 `QuickJSEngine` 内（参考已有 `RegisterJSCallback` 的写法）
- 验收：TS 注册后，启动 / 退出 / 场景加载时 onXxx 都被调到

#### A3 · 输入查询

- 在 `NextEngine` 加 `FrameInput` 结构（每帧 `Tick` 开始时刷新当前键 / 鼠标 / 手柄 + 上一帧状态）
- 暴露 `Engine.Input.IsKeyDown(name) / IsKeyPressed(name) / IsMouseButtonDown(idx) / GetGamepadButton(name)`
- 键名映射建一个 `string → SDL_Keycode` 查表函数（至少覆盖：`"space"`, `"esc"`, `"a"`, `"w"`, `"s"`, `"d"`, `"return"`, `"any"`）
- 验收：TS 里按 Space 输出 "flap"

#### A4 · 音频

- `Engine.Audio.PlaySfx(path, volume?) / PlayMusic(path, volume?) / StopMusic()`
- 验收：TS 调任意 wav 路径，无文件不崩

#### A5 · 场景动态增删 + JSON 加载

- `Scene.AddBoxNode(name, minX, minY, minZ, maxX, maxY, maxZ, r, g, b) → nodeId`
- `Scene.AddSphereNode(name, cx, cy, cz, radius, r, g, b) → nodeId`
- `Scene.RemoveNodeById(nodeId)`
- `Engine.LoadJson(assetPath) → any`：`PakSystem.LoadFile` + `nlohmann::json::parse` + 写一个 `JsonToJSValue` 递归转换器（数组 / 对象 / 字符串 / 数字 / bool / null）
- 验收：TS 里加 1 box + 1 sphere，10 秒后删除；TS 里 LoadJson 一份 gameplay.json 解出嵌套数值

#### A6 · 相机覆盖

- `Engine.SetOverrideCamera({ position: Vec3, target: Vec3, up: Vec3, fov: number })` —— **仅透视**
- 宿主：`FlappyJsGameInstance::OverrideRenderCamera` 读取 `QuickJSEngine` 缓存的 override 状态，填 `outRenderCamera.ModelView = lookAtRH(...) ; outRenderCamera.FieldOfView = fov;`
- 验收：TS 设置后画面是侧视图

#### A7 · ImGui 最小子集

- `Engine.UI`：`Begin(name, flags?) / End() / Text(s) / SetCursorPos(x, y) / GetWindowSize() → Vec2 / SetWindowFontScale(f) / GetScreenSize() → Vec2`
- 注册 `onRenderUI` 回调（Lifecycle Hooks 的扩展），宿主 `OnRenderUI` 调用之
- 验收：TS 在屏幕中央画一段文字

#### A8 · 场景重载 / 退出

- `Engine.RequestLoadScene(filename) / RequestClose()`
- 验收：TS 能调用并触发对应行为

### Phase C · FlappyJs（依赖 Phase A 全部完成）

- **C1 · 工程骨架（极薄壳）**
  - `src/Application/Flappy/FlappyJs/FlappyJsGameInstance.cpp`：
    - `OnInit`：调 QuickJS 端注册的 `onInit`
    - `OnTick`：什么都不做（QuickJS 自己有 tick callback）
    - `OnRenderUI`：调 QuickJS 端注册的 `onRenderUI`
    - `OnKey / OnMouseButton / OnGamepadInput`：什么都不做（A3 让 JS 主动查询 input）
    - `OverrideRenderCamera`：读 A6 缓存
  - `src/cmake/SourceFiles.cmake` + `CMakeLists.txt` 加 `FlappyJs` target
  - `assets/typescript/flappy/tsconfig.json`：参考根 `typescript/tsconfig.json`，`"outDir": "../../../assets/scripts/flappy"`，`"files"` 列出所有 .ts；`extends` 根 tsconfig 复用 module=ESNext 设置
  - **关键**：根 tsconfig 的 `"files"` 加上 `flappy/main.ts`（或者 flappy/ 用独立 tsconfig，QuickJSEngine 的 `ResolveTypeScriptPaths` 当前只识别 `assets/typescript/tsconfig.json`，需要扩展或合并）
  - 验收：能编译启动到空场景，TS `main.ts` 里 `console.log("hello")` 输出

- **C2 · TS 端 RNG + 配置**
  - `flappy/rng.ts`：xorshift32，与 C++ 端**逐位对齐**（同一个种子前 100 次输出 hash 与 C++ 一致 — 加一个 dev 自检日志）
  - `flappy/config.ts`：`Engine.LoadJson("assets/configs/flappy/gameplay.json")` + 字段类型断言
  - 验收：TS 控制台打印的 RNG 头 10 次输出 == B2 的金标

- **C3 · 鸟 + 管道 + 玩法（TS 实现）**
  - 完全镜像 FlappyCpp 行为，所有数值从 config 来，所有节点用 `Scene.AddBoxNode / AddSphereNode` 创建，状态机用 TS 类
  - 输入用 `Engine.Input.IsKeyPressed("space")`
  - 相机：`onInit` 中 `Engine.SetOverrideCamera({ position, target, up, fov })`
  - 验收：可玩，行为肉眼一致

- **C4 · TS HUD + 音效**
  - `onRenderUI` 中调 `Engine.UI.*` 画分数 / Ready / Dead
  - `Engine.Audio.PlaySfx(...)`
  - 验收：HUD 与 FlappyCpp 像素级近似（字号 / 位置）

- **C5 · Replay parity 输出（TS 端 trace）**
  - 同 B6：CLI 启动加 `--flappy-replay`（宿主侧 `FlappyJsGameInstance` 检测后通过 `Engine.IsReplayMode()` binding 让 TS 切换输入源；该 binding 在 A 阶段补）
  - TS 跑完写 `out/flappy_js_trace.json`（用 `Engine.WriteFile(path, content)` ——这个 binding 在此卡顺手补）
  - 验收：trace 与 FlappyCpp 完全相同

---

# Stage 3 · Parity 验证 & 文档

### Phase D · Parity 报告

- **D1 · Parity diff 工具**
  - 写 `tools/flappy/diff_traces.py` 比对两份 `flappy_*_trace.json`
  - 输出对比结果 → `docs/projects/flappy-bird-parity/parity-report.md`，附复现命令

- **D2 · 接入 gkNextVisualTest**
  - `assets/configs/visual_test.json` 加两个 scene entry（FlappyCpp / FlappyJs 各跑 5 秒确定性输入），生成截图 hash 应一致
  - 验收：visual test 报告里两张截图视觉对齐

- **D3 · 收尾文档**
  - `AGENT_GUIDE/QuickJSBindings.md`：把 Phase A 加的所有 binding 整理成"如何加新 binding"的 cookbook，**特别强调 ESM module loader 的实现要点**
  - 更新 `AGENTS.md` 的 "Key Architectural Patterns / QuickJS Scripting" 段，把 Flappy parity 列为 binding 完整性的回归 demo

---

## 注意事项 / 防呆清单

- ❌ **不要假装支持正交相机**：Camera 结构体里没这字段；用透视相机 + lookAtRH，参考 Brotato3D
- ❌ **不要在 Stage 1 完成后直接进 Stage 2**：必须等用户在"Stage 1 验收"勾选 ✅
- ❌ **不要写 `import "./bar"` 形式的 TS 代码，除非 A0 的 module loader 已经合并并验证过**：当前引擎跑这种代码会 ReferenceError；Stage 1 不需要写 ts，所以 Stage 1 不受影响
- ❌ **不要重造确定性**：xorshift32 自实现就够了；不要引入第三方 RNG 库
- ❌ **不要把 gameplay.json 拆成两份**：C++ / TS 共享同一个文件是 parity 的前提
- ❌ **不要在 TS 里用 `Math.random` / `Date.now`**：除了显式 `Engine.GetTime()` 用于显示，其它一律用 RNG + 固定步长
- ❌ **不要修改 ThirdParty/quickjs-ng 内部源码**：模块 loader 通过公开 API `JS_SetModuleLoaderFunc` 注册，不要去改 quickjs.c
- ✅ **音频缺文件不要报错**：现有 `NextAudio` 已有 missing-file 静默逻辑，沿用
- ✅ **绑定每加一个就跑一次 `full-*` preset 的全量编译**（按 AGENTS.md 要求），不要积攒
- ✅ **TS 热重载**：`QuickJSEngine` 已有 tsc 时间戳检测；改完 ts 不要手动重启程序，看日志是否自动 `Reloading QuickJS context`
- ✅ **场景重载会清空运行时 Node**：`RequestLoadScene` 后所有动态加的 Node 都没了；TS 端要监听 `onSceneLoaded` 重建
- ✅ **JS Vec3 写法**：现有反射桥支持 `node.Translation = {x, y, z}` **整体赋值**，**不支持** `node.Translation.x = 1`（会写到临时对象上）—— 这点要在 TS 代码里严格遵守，否则会出诡异 bug

## 完成标准（DoD）

1. `FlappyCpp` 与 `FlappyJs` 都能用 `./run.bat --target FlappyCpp` / `--target FlappyJs` 跑起来，玩法完整
2. `out/flappy_cpp_trace.json` 与 `out/flappy_js_trace.json` `diff` 为空
3. `gkNextVisualTest` 报告里两份实现截图视觉一致
4. Phase A 的所有 binding 在 `Engine.d.ts` 中可见，`assets/typescript/test.ts` 里有对应回归调用
5. `parity-report.md` 与 `QuickJSBindings.md` 入库

## 后续可拓展（不在本计划范围）

- Android / iOS 上的 FlappyJs 验证（QuickJS 已编进移动端，但 TS 编译需要预编译产物入包，参考 `Packager`）
- 把 Flappy 玩法逻辑做成可加载 mod（不同 ts 入口 → 不同游戏），用以验证脚本化 modding 路径
- AI 生成关卡：给 `FAIService` 一个 prompt 让它输出 `gameplay.json` 的变体
- 引擎层加正交相机支持（在 `Assets::Camera` 加 `bool IsOrthographic + float OrthoSize`，UBO 里改投影矩阵生成）—— 当前 demo 不需要，但完整的 2D 工作流以后用得上
