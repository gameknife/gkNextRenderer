# QuickJS 模块化迁移计划

> 状态：待执行
> 编写日期：2026-06-12
> 目标模块：`src/Modules/NextQuickJS/`

## 1. 目标

把 QuickJS 集成从 `src/Engine/**` 移到独立可选模块 `NextQuickJS`，使不使用脚本的
program 不再编译、链接、初始化 QuickJS，同时保持现有 TypeScript ESM、热重载、
反射绑定、编辑器 JavaScript 执行和 Flappy C++/JS parity 行为。

这里的“移动到 module”指 C++ 架构模块化，不是 JavaScript 的 `Engine` ESM 模块；
后者已经存在，迁移后继续作为脚本侧兼容入口。

### 验收目标

1. `src/Engine` 不再 include QuickJS/quickjspp 头文件，也不再持有 `QuickJSEngine`。
2. QuickJS 第一方集成代码全部位于 `src/Modules/NextQuickJS/`。
3. `gkNextEngine` 不链接 `quickjs`；只有显式使用 `NextQuickJS` 的 target 链接它。
4. 不使用脚本的 program 不创建 JS runtime、不编译 TypeScript、不加载默认脚本。
5. FlappyJs 生命周期、编辑器 `Editor.*`、StudioSim 脚本入口和热重载行为保持不变。
6. `WITH_QUICKJS=OFF` 时核心引擎仍可独立构建，不需要核心层 stub。

## 2. 当前基线

QuickJS 相关第一方代码约 3,695 行：

| 当前位置 | 职责 |
| --- | --- |
| `Engine/Runtime/Subsystems/QuickJSEngine.*` | runtime/context、模块加载、生命周期、热重载 |
| `Engine/Runtime/Subsystems/QuickJSBindings*` | Engine/Input/Audio/UI/Scene/SceneBuild 绑定、TS 定义 |
| `Engine/Runtime/Reflection/QuickJSTypeConverter.*` | `entt::meta_any` 与 JSValue 转换 |
| `Engine/Runtime/Reflection/QuickJSReflectionBridge.h` | 反射类型绑定和 `.d.ts` 生成 |

当前耦合不只是目录位置：

- `NextEngine::FRuntimeServices` 直接拥有 `std::unique_ptr<QuickJSEngine>`。
- `NextEngine` 负责构造、初始化、输入转发、每帧 Tick 和回调注册。
- `Engine.cpp` 与 `RuntimeFwd.hpp` 暴露 QuickJS 具体类型。
- `Options::QuickJSEntry` 是核心配置的一部分。
- `NextEngine::RegisterJSCallback` 是 QuickJS 专用 API，并进入核心反射注册。
- CMake 给 `AllTargets` 统一定义 `WITH_QUICKJS=1` 并链接 `quickjs`。
- QuickJS 默认入口是 `assets/scripts/test.js`，因此所有 program 启动时都会运行测试脚本；
  该脚本会切换首个 `RenderComponent` 的可见性，不能在模块化时无意保留为全局默认行为。

已确认的直接使用方：

| Target | 用法 |
| --- | --- |
| `FlappyJs` | 完整脚本 GameInstance 生命周期、输入、场景构建、相机覆盖 |
| `StudioSim` | `assets/scripts/studiosim_entry.js` 脚本入口 |
| `gkNextEditor` | `Eval()` 与 `Editor.*` 自定义绑定 |
| `gkNextRenderer` | 当前仅依赖全局默认 `test.js` 作为绑定 smoke test |

## 3. 目标架构

```text
src/
├── Engine/
│   └── Runtime/
│       └── ScriptRuntime.hpp          # 只含通用脚本运行时接口
└── Modules/
    └── NextQuickJS/
        ├── NextQuickJSModule.hpp/.cpp # Install/Get 与配置
        ├── QuickJSEngine.hpp/.cpp
        ├── QuickJSBindings*.cpp
        ├── QuickJSBindings.Internal.hpp
        └── Reflection/
            ├── QuickJSTypeConverter.hpp/.cpp
            └── QuickJSReflectionBridge.hpp
```

依赖方向固定为：

```text
Application -> NextQuickJS -> gkNextEngine
                         \-> quickjs
```

禁止出现 `gkNextEngine -> NextQuickJS` 或核心头文件引用 QuickJS 类型。

### 3.1 核心注入点

在核心层增加最小接口 `Runtime::IScriptRuntime`：

```cpp
class IScriptRuntime
{
public:
    virtual ~IScriptRuntime() = default;
    virtual void Initialize() = 0;
    virtual void Tick(double deltaSeconds) = 0;
    virtual void HandleEvent(const SDL_Event& event) = 0;
};
```

`NextEngine` 只持有该接口，并提供 factory 注入：

```cpp
using ScriptRuntimeFactory =
    std::function<std::unique_ptr<Runtime::IScriptRuntime>(NextEngine&)>;

void SetScriptRuntimeFactory(ScriptRuntimeFactory factory);
Runtime::IScriptRuntime* GetScriptRuntime();
```

采用 factory 而不是直接注入实例，原因是当前 QuickJS 初始化发生在 renderer、physics、
audio 启动之后；factory 可以由 GameInstance 构造阶段安装，再由 `NextEngine::Start()`
按现有时序创建和初始化。

核心继续负责三个通用时机：

1. `Start()` 中创建并初始化脚本运行时。
2. UI 未消费输入时调用 `IScriptRuntime::HandleEvent()`。
3. 场景 Tick 后、GameInstance Tick 前调用 `IScriptRuntime::Tick()`。

QuickJS 特有的 `Eval`、生命周期 hook、相机覆盖和 Editor binding callback 不进入通用
接口，由模块 accessor 暴露。

### 3.2 模块入口

模块提供显式安装和强类型访问：

```cpp
namespace Modules::NextQuickJS
{
    struct FConfig
    {
        std::string entryScript;
        bool compileTypeScript = true;
        bool enableHotReload = true;
    };

    void Install(NextEngine& engine, FConfig config);
    QuickJSEngine* Get(NextEngine& engine);
}
```

应用必须显式调用 `Install()`。没有安装模块时，核心脚本运行时为空，不需要
`#if WITH_QUICKJS` stub，也不会隐式执行 `test.js`。

## 4. API 与行为迁移

### 4.1 TypeScript tick 注册

移除核心层的 `NextEngine::RegisterJSCallback`。在脚本侧新增模块函数：

```ts
NE.RegisterTickCallback((deltaSeconds: number) => { ... });
```

同步修改：

- `assets/typescript/NextGameInstanceBase.ts`
- `assets/typescript/test.ts`
- `BuildTypeScriptDefinitions()`
- `assets/typescript/Engine.d.ts` 的生成结果
- `NextEngine::RegisterReflection()` 中的 QuickJS 专用注册

可在一个过渡 Phase 内同时保留旧函数别名，但最终验收时核心 API 中不能残留
`RegisterJSCallback`。

### 4.2 应用安装点

- `FlappyJsGameInstance` 构造函数调用 `NextQuickJS::Install()`，传入 Flappy 入口；
  生命周期调用改为 `NextQuickJS::Get(engine)`。
- `StudioSimGameInstance` 构造函数安装模块，传入 `studiosim_entry.js`。
- `gkNextEditor` 在 Editor GameInstance 构造阶段安装模块；`EditorAIService` 和
  `EditorScriptExecutor` 通过模块 accessor 获取 runtime。
- `gkNextRenderer` 不再默认安装。`test.ts` smoke test 应迁入专用测试路径，或由
  明确的 CLI/测试 target 安装，避免普通 renderer 启动时修改场景。

### 4.3 Editor bindings

现有 `SetEditorBindingsCallback(std::function<void(void*)>)` 可以在第一轮迁移中保留，
以控制改动面。后续建议把 `void*` 收敛为模块侧的 `BindingsRegistrar` API，避免
Application 直接 include `quickjspp.hpp`；这不是完成物理迁移的前置条件。

### 4.4 TypeScript 工具链

以下资源仍留在原位置：

- `assets/typescript/`
- `assets/scripts/`
- `tools/tsc/`
- `gnb setup` 的 tsc 下载逻辑

但 CMake 的 tsc 必需性应从“核心引擎全局必需”改为“启用 NextQuickJS 时必需”。
`assets/CMakeLists.txt` 可继续复制 bundled tsc；若后续要求精简纯运行包，再单独增加
按 target 打包。

## 5. CMake 迁移

1. 在 `src/cmake/SourceFiles.cmake` 的 `GK_MODULE_NAMES` 增加 `NextQuickJS`。
2. `src/CMakeLists.txt` 保留 `ThirdParty/quickjs-ng` target，但：
   - 删除 `AllTargets` 循环中的全局 `WITH_QUICKJS` 定义和 `quickjs` 链接。
   - `NextQuickJS` 自身定义 `WITH_QUICKJS=1`。
   - `NextQuickJS` 链接 `gkNextEngine`、`quickjs`、SDL/ImGui 等实际依赖。
3. 仅给 `FlappyJs`、`StudioSim`、`gkNextEditor` 和 QuickJS 测试 target 链接
   `NextQuickJS`。
4. 根 CMake 不再 `FORCE` 所有 target 启用 QuickJS；改为模块 option，例如
   `GK_WITH_NEXT_QUICKJS`，默认 ON，但可关闭。
5. `cmake/SetupExternalLibs.cmake` 的 tsc 校验改用模块 option。

Android 当前把 `src_files_modules_all` 全部并入单一 SHARED target。迁移时不能只把
`NextQuickJS` 加入列表，否则 Android 即使不使用也仍会编译它。需同步完成以下二选一：

- 为 Android 建立显式 `GK_ANDROID_MODULES` allowlist；或
- 仅在 `GK_WITH_NEXT_QUICKJS` 开启时追加 `src_files_module_NextQuickJS`。

iOS/桌面继续使用独立静态库，需验证静态链接不会因未引用符号丢失模块安装入口。

## 6. 分阶段执行

### Phase 0：建立回归基线

- 记录 `gnb loc` 中 QuickJS 和 Engine 行数。
- 构建 `gkNextRenderer`、`gkNextUnitTests`、`FlappyJs`、`gkNextEditor`、`StudioSim`。
- 保存 Flappy replay trace、`Engine.d.ts` 和一次 TypeScript 热重载日志。
- 确认编辑器 JavaScript `Eval` 与 `Editor.*` 可执行。

### Phase 1：增加通用脚本运行时注入点

- 新增 `IScriptRuntime` 和 factory。
- `NextEngine` 的初始化、输入、Tick 改走接口。
- QuickJS 仍暂留原目录，通过 adapter 接入，保证行为不变。
- 增加无脚本 runtime 的单测，确认空 factory 不影响引擎生命周期。

风险：中。此阶段改变 Engine 主循环扩展点，但不移动实现。

### Phase 2：创建 `NextQuickJS` 并物理迁移

- 移动 `QuickJSEngine`、全部 bindings、converter 和 reflection bridge。
- 更新 include、namespace 和 SourceFiles/CMake。
- 从 `FRuntimeServices`、`RuntimeFwd.hpp`、`Engine.cpp` 移除 QuickJS 具体类型。
- 模块实现 `Install/Get`。

风险：中。主要是 CMake、Unity Build 和静态库符号边界。

### Phase 3：迁移应用与脚本 API

- 迁移 FlappyJs、StudioSim、gkNextEditor 三个真实使用方。
- 用 `RegisterTickCallback` 替代 `NextEngine::RegisterJSCallback`。
- 移除 `Options::QuickJSEntry`，入口改由 `FConfig` 管理。
- 把默认 `test.js` 从所有 program 的隐式行为改为显式 smoke test。

风险：高。此阶段决定最终运行行为和脚本兼容面。

### Phase 4：收紧构建依赖

- 删除 AllTargets 的 QuickJS 宏和链接。
- 只给 allowlist target 链接 `NextQuickJS`。
- 修正 Android 模块源选择。
- 验证 `GK_WITH_NEXT_QUICKJS=OFF` 的纯核心构建。

风险：中高。需要全 target reconfigure，重点检查平台分支。

### Phase 5：文档与清理

- 更新 `docs/typescript-integration.md`、`AGENT_GUIDE/QuickJSBindings.md`、
  `AGENT_GUIDE/HotReload.md` 和 `src/Modules/README.md`。
- 全仓 grep 确认 `src/Engine` 内无 `QuickJS|quickjs|JSContext|JSValue`。
- 清理过渡 alias、stubs 和失效的 `WITH_QUICKJS` 分支。

## 7. 验证矩阵

由于这是跨核心、模块、CMake 和多 program 的广面重构，收尾必须使用全量验证：

```powershell
.\gnb.bat build --reconfigure
.\out\build\windows\bin\gkNextUnitTests.exe
.\out\build\windows\bin\FlappyCpp.exe --flappy-replay
.\out\build\windows\bin\FlappyJs.exe --flappy-replay
python tools\flappy\diff_traces.py
```

还需执行：

- `tools\tsc\tsc.exe -p assets\typescript\tsconfig.json`
- 启动 `gkNextEditor`，验证 `Editor.*` JavaScript 执行
- 启动 `StudioSim`，确认脚本入口加载且无重复 runtime
- `gnb shot --scene assets/models/playground.glb`
- 配置 `GK_WITH_NEXT_QUICKJS=OFF` 后构建 `gkNextRenderer`
- 条件允许时至少做一次 Android configure/build，验证模块源 allowlist

## 8. 风险与决策

| 风险 | 应对 |
| --- | --- |
| 脚本初始化时序变化 | factory 仍在 audio/physics 后、GameInstance `OnInit` 前初始化 |
| 输入事件重复或被 UI 吞掉 | 保持现有 RmlUi consumed 判断和事件类型过滤 |
| 默认 `test.js` 行为消失掩盖绑定回归 | 建立显式 QuickJS smoke test，不依赖普通 renderer 启动 |
| Editor 直接使用 QuickJS C API | 第一阶段允许应用通过模块传递依赖，后续再封装 registrar |
| 静态库链接顺序/符号裁剪 | 安装入口由 Application 显式调用，不依赖静态初始化 |
| Android 仍编译全部模块 | 调整 Android 模块源 allowlist，列为 Phase 4 验收项 |
| `.d.ts` 与运行时绑定漂移 | smoke test 比较生成结果并运行 bundled tsc |

## 9. 明确不做

- 不修改 `src/ThirdParty/quickjs-ng`。
- 不更换 JS 引擎，不升级 QuickJS 版本。
- 不改变脚本 `Engine` ESM 的主要 API 语义。
- 不引入 Node/npm/global tsc。
- 不在迁移中重写全部绑定为新框架。
- 不把 TypeScript 源码或编译输出搬进 C++ 模块目录。

## 10. 完成定义

迁移完成时，以下命令应无输出：

```powershell
rg -n "QuickJS|quickjs|JSContext|JSValue" src/Engine
```

同时满足：

- `gkNextEngine` 的 link dependencies 不含 `quickjs`。
- 不链接 `NextQuickJS` 的 target 不含 QuickJS runtime 和默认脚本副作用。
- Flappy replay 完全一致。
- Editor JavaScript 与 StudioSim 脚本正常。
- 全量构建和单测通过。
