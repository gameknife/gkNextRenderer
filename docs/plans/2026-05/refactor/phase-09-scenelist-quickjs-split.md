# Phase 09 · SceneList 与 QuickJSEngine 拆分

> **目的：** 拆分两个剩余的核心引擎 god-file：`Runtime/Scene/SceneList.cpp 1765L` 与 `Runtime/Subsystems/Scripting/QuickJSEngine.cpp 2650L`。
> **依赖：** phase-08 完成
> **范围：** 2 个 cpp；不动其 header
> **预计 diff：** 2 cpp 各拆为 4~6 个文件

---

## 1. SceneList.cpp 拆分

### 1.1 当前状况

`Runtime/Scene/SceneList.cpp 1765L` 是"一切场景的工厂"：
- 内置测试场景注册
- glTF 场景加载入口
- 程序生成场景（ProcModel）
- LDraw / KayKit 场景（lego / 模块化）

include 的头：`FProcModel`、`FLDrawLoader`、`FSceneLoader`、`KayKitPieceLoader`、`SceneBuilder` —— 多种来源混在一处。

### 1.2 目标拆分

```
src/Runtime/Scene/
├─ SceneList.{cpp,h}              # header 不动；cpp 残留：注册表 + Discovery + 入口（≤ 400 LoC）
├─ SceneList_Procedural.cpp       # 程序生成场景 (FProcModel 相关)
├─ SceneList_GltfTests.cpp        # glTF 测试场景集
├─ SceneList_LDraw.cpp            # LDraw / Lego 场景
├─ SceneList_KayKit.cpp           # KayKit 模块化场景
└─ SceneList_Builders.cpp         # 共享的 SceneBuilder helper
```

### 1.3 步骤

1. `cd src/Runtime/Scene && cp SceneList.cpp /tmp/SceneList.cpp.bak`
2. **不**改 `SceneList.h`
3. 读 `SceneList.cpp`，识别每个 `static void RegisterXxxScene(...)` 或 `SceneList::SceneXxx(...)` 函数：按 "用了什么 loader" 决定归属：
   - 用 `FProcModel.h` → `SceneList_Procedural.cpp`
   - 用 `FLDrawLoader.h` → `SceneList_LDraw.cpp`
   - 用 `KayKitPieceLoader.h` → `SceneList_KayKit.cpp`
   - 用 `FSceneLoader.h`（glTF）且是测试场景 → `SceneList_GltfTests.cpp`
   - 通用 builder helper → `SceneList_Builders.cpp`
   - 注册表本身 + Discovery + Lookup → 残留 `SceneList.cpp`
4. **每个新 cpp 顶部固定 include**：
   ```cpp
   #include "Common/CoreMinimal.h"
   #include "Runtime/Scene/SceneList.h"
   #include "Runtime/Scene/SceneBuilder.h"
   // 该文件实际用到的 loader / asset 头
   ```
5. SourceFiles.cmake 的 `src_files_engine` GLOB 自动覆盖。无需改。
6. 检查 unity build：所有 `SceneList_*.cpp` 加入 SKIP 列表或 batch_size=1（同 phase-08 §3.5）

### 1.4 验收

```bash
wc -l src/Runtime/Scene/SceneList.cpp                    # ≤ 400
ls src/Runtime/Scene/SceneList_*.cpp | wc -l             # ≥ 4
git diff --stat src/Runtime/Scene/SceneList.h            # 0
./gnb build --reconfigure
./out/build/macos-arm64/bin/gkNextUnitTests
# 期望: Test_SceneList 也通过
```

---

## 2. QuickJSEngine.cpp 拆分

### 2.1 当前状况

`Runtime/Subsystems/Scripting/QuickJSEngine.cpp 2650L` 是脚本引擎主实现，混以下 4 类：

- **Engine 生命周期**：JSRuntime / JSContext 创建销毁、tick、error 处理
- **模块加载**：ES module resolver、文件路径解析、热重载触发
- **绑定层**：把 reflected components 注入 JS、TypeScript→JS 编译触发
- **Native 桥**：spdlog 输出、时间 API、文件 IO 暴露

### 2.2 目标拆分

```
src/Runtime/Subsystems/Scripting/
├─ QuickJSEngine.{cpp,h}          # header 不动；cpp 残留：Engine 生命周期（≤ 600 LoC）
├─ QuickJSEngine_ModuleLoader.cpp # ES module + 路径解析 + 热重载入口
├─ QuickJSEngine_Bindings.cpp     # Reflected component 注入 + entt::meta 桥
├─ QuickJSEngine_NativeBridge.cpp # spdlog / 时间 / 文件 IO 等 native API 暴露
└─ QuickJSEngine_TypeScript.cpp   # tsc 调用 + 编译产物管理
```

### 2.3 步骤

与 SceneList 拆分同模式：

1. `cp QuickJSEngine.cpp /tmp/QuickJSEngine.cpp.bak`
2. **不**改 `QuickJSEngine.h`
3. 按上述 5 大类剪切粘贴
4. 每个新 cpp 顶部 include 对应需要的：
   - 全部需要 `quickjs.h` 与 `Runtime/Subsystems/Scripting/QuickJSEngine.h`
   - `_Bindings.cpp` 需 `Runtime/Reflection/...`
   - `_NativeBridge.cpp` 需 `spdlog` / `<chrono>` / 文件 helper
   - `_TypeScript.cpp` 需 `Utilities/FileHelper.h` 等
5. SKIP unity build 同 phase-08 §3.5

### 2.4 风险特别提示

QuickJS 用大量 `JS_NewCFunction` `JS_FreeRuntime` 等 C API。拆分时**最容易**：

- 漏掉 `JSClassDef` 的全局静态变量定义 → 链接错误
- `static JSValue js_xxx(JSContext *ctx, ...)` 函数被 Bindings 注册指针引用，拆开后引用方失效

**对策**：所有 `js_xxx` C 函数（JS 桥接函数）**全部留在被注册的那个文件**（多半是 `_Bindings.cpp`）。只有它们的注册调用 `JS_NewCFunction(ctx, js_log, "log", 1)` 一行可以从 cpp 间共享。

### 2.5 验收

```bash
wc -l src/Runtime/Subsystems/Scripting/QuickJSEngine.cpp                # ≤ 600
ls src/Runtime/Subsystems/Scripting/QuickJSEngine_*.cpp | wc -l         # ≥ 3
git diff --stat src/Runtime/Subsystems/Scripting/QuickJSEngine.h        # 0
./gnb build --reconfigure
./out/build/macos-arm64/bin/gkNextUnitTests
# 期望: Test_FlappyRng（依赖 JS）通过；MagicaLego script 测试通过
```

---

## 3. 综合验收门

```bash
# SceneList 拆分
wc -l src/Runtime/Scene/SceneList.cpp                   # ≤ 400
ls src/Runtime/Scene/SceneList_*.cpp | wc -l            # ≥ 4

# QuickJSEngine 拆分
wc -l src/Runtime/Subsystems/Scripting/QuickJSEngine.cpp        # ≤ 600
ls src/Runtime/Subsystems/Scripting/QuickJSEngine_*.cpp | wc -l # ≥ 3

# Header 未动
git diff --stat src/Runtime/Scene/SceneList.h src/Runtime/Subsystems/Scripting/QuickJSEngine.h
# 期望: 0 行变更

# 全量
./gnb build --reconfigure
./out/build/macos-arm64/bin/gkNextUnitTests
git status --porcelain
```

---

## 4. 自我审查清单

- [ ] SceneList.h 一字未改；QuickJSEngine.h 一字未改
- [ ] SceneList.cpp 拆为残留 + 4~5 伴生 cpp，每个 ≤ 500 LoC
- [ ] QuickJSEngine.cpp 拆为残留 + 3~4 伴生 cpp
- [ ] 无方法定义跨多个 cpp 重复
- [ ] JS 桥函数 `js_xxx` 全部留在同一个 cpp（`_Bindings.cpp` 或就近放置）
- [ ] 单元测试 `Test_SceneList` `Test_FlappyRng` `Test_MagicaLegoScript` 全部通过
- [ ] PR 标题：`refactor(phase-09): SceneList 与 QuickJSEngine 拆分`

## 5. 风险与回退

| 风险 | 应对 |
| --- | --- |
| QuickJS C 函数注册指针失效 | 保持注册函数与 `js_xxx` 实现同一文件 |
| SceneList 中跨场景共享的 helper（如材质工厂） | 抽到 `SceneList_Builders.cpp`，前向声明放到一个 `SceneListInternal.h`（不进 public 头） |
| Unity build 遇到 anonymous namespace 重复 | 同 phase-08 §3.5 |
| `Test_SceneList.cpp` 引用了 SceneList 的 internal helper | 该 helper 需要 export；本 phase 暴露策略：放到 `SceneListInternal.h` 或加 `friend class SceneListTest` |

回退：`git revert` 单一 squash commit。

## 6. 关于"应用侧 god-UI 不在本 phase"

README §8 已声明。`Brotato3DUI.cpp 2039L` `MagicaLegoUserInterface.cpp 1972L` `KongLie3DUI.cpp 1971L` 等应用 UI 文件**本 phase 不动**，留给游戏作者后续单独发起。
