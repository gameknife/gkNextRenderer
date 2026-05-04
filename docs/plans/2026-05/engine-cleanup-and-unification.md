# Engine Layer Cleanup & Unification Plan

> **作者：** 探索 Agent · 2026-05-04
> **范围：** `src/Runtime/` (~19.8k LOC) + 与之耦合的 `src/Utilities/` 全局工具
> **目的：** Brotato3D / KongLie3D 提炼出的引擎能力已落地。下一步收敛 Runtime 层自身：删冗余 façade、合并重叠 API、统一命名、淘汰被替代但仍在用的旧接口。
> **执行约定：** 每条任务一个 PR；优先级 P0 → P3；P0/P1 可并行；P2/P3 在前两层稳定后再做。每个 PR 必须给出净 LOC 变化与 callsite 替换计数。

---

## TL;DR

| # | 类别 | 问题 | 净收益 |
| --- | --- | --- | --- |
| **C0** | NextEngine façade 净化 | `PlaySound`/`PauseSound`/`IsSoundPlaying` 是 `GetAudio()->...` 的纯转发；`ExecuteCommand`/`Undo`/`Redo`/`CanUndo`/`CanRedo` 同样是 `GetCommandHistory()->...` 的转发；`GetCommandSystem()`=`GetCommandHistory()` 是别名 | -7 公共方法、-1 间接层 |
| **C1** | Getter 二态 | `GetRenderer()`/`GetRendererPtr()`、`GetScene()`/`GetScenePtr()` 各保留一对 ref/ptr 形式 | -4 方法 |
| **C2** | NextEngineHelper 投影 API 收敛 | 6 个投影/反投影函数，命名与契约不一致；`ProjectScreenToWorld` 实际只返回方向却名字像点 | -3 方法 |
| **C3** | RenderComponent 双套 setter | `SetMaterial`/`SetMaterials`、`Materials()`/`GetMaterials()` 重复 | -2 方法 + 修一个 NodeUtils 的隐 bug |
| **C4** | 两套 i18n 共存 | `Utilities::Localization` (`LOCTEXT`, 119 处) 与 Brotato3D 内部 `Localize()` (JSON) 没合并；KongLie3D 用 `U8Text` 内联 u8 字面量 | 删一个系统、统一一个新 API |
| **C5** | CVar 匹配 3 个 | `ListCVars`/`CompleteCVars`/`GetMatchingNames` 三套相似返回 | -1 方法、统一返回 shape |
| **C6** | Node 变换 API 漏抽象 | `Node::SetTranslation/Rotation/Scale` 不调 `RecalcTransform()`，靠 `NodeUtils::SetXxx` 兜底，调用混用 | 修隐 bug、删 NodeUtils 的位姿 setter |
| **C7** | 调试可见性标志位置错 | `showPhysicsDebugOverlay_`/`showGraphicsDebugPanel_` 是 NextEngine 私有字段 + 4 个 Is/Set 方法；语义上属于 `ShowFlags` | -4 方法、归并到 ShowFlags + CVar |
| **C8** | Engine.hpp 顶部死代码 | `NextComponent` / `NextActor` / `NextEngine::GetTestNumber()` | 直接删 |
| **C9** | 三段式 Scene 加载 | `RequestLoadScene` / `RequestLoadSceneAdd(file)` / `RequestLoadSceneAdd(file, options)` 三个公共 + 3 个 private 实现 | -1 重载 |
| **C10** | 截图 API 三件套 | `SaveScreenShot` (sync) / `RequestScreenShot` / `RequestHighQualityScreenShot` 各自管状态 | 合并为 `CaptureScreenShot(spec)` |
| **C11** | 命名空间方言不一致 | `NextEngineHelper`/`NextJson`/`NextCVar`/`NextUI` vs `SceneBuilder`/`NodeUtils`/`FontLoader` vs `Runtime::Editor` vs `NextPlatform::UserPaths` | 统一为单一规则 |
| **C12** | SceneBuilder 渗透不全 | 应用层仍直接 `materials.push_back({Material::Lambertian(...)})` | 完成迁移 |
| **C13** | 任务系统模糊 | `AddTickedTask`/`AddTimerTask` (引擎内置 lambdas) vs `TaskCoordinator` (后台线程) | 划清边界并文档化 |
| **C14** | Singleton 漏 DI | `NextEngine::GetInstance()` 158 处调用，多数是 `GameInstance` 已持有 engine ref 时绕道 | 收敛到 ~30 处合理调用 |

---

## P0 任务 — 立即做（净收益大、风险低）

### C0 · 删除 NextEngine 上的 audio / command façade

**问题：** [src/Runtime/Engine.hpp:201-203](src/Runtime/Engine.hpp:201) + [Engine.cpp:612-636](src/Runtime/Engine.cpp:612)

```cpp
// 引擎暴露的两个并行入口
engine.PlaySound(path, loop, vol);   // 内部就是
engine.GetAudio()->PlaySound(...);   // 原始

engine.ExecuteCommand(cmd);                  // 内部就是
engine.GetCommandHistory().Execute(cmd);     // 原始
```

且 `GetCommandSystem()` 是 `GetCommandHistory()` 的别名（[Engine.hpp:251-252](src/Runtime/Engine.hpp:251)），两条命名都在用：4 处用 `GetCommandSystem`、5 处用 `GetCommandHistory`，混乱。

**目标：** 删除引擎层 façade，保留唯一入口：
- 删 `NextEngine::PlaySound/PauseSound/IsSoundPlaying`，调用方走 `engine.GetAudio()->...`。
- 删 `NextEngine::ExecuteCommand/UndoCommand/RedoCommand/CanUndo/CanRedo`，调用方走 `engine.GetCommandHistory().Execute/Undo/Redo/CanUndo/CanRedo`。
- 删 `GetCommandSystem()`，全部统一到 `GetCommandHistory()`。

**迁移：**
1. `grep` 替换：
   - `engine_->PlaySound` / `GetEngine().PlaySound` → `GetEngine().GetAudio()->PlaySound`（注意 null check 原本就没做，audioEngine_ 启动后非空）
   - 同理 PauseSound / IsSoundPlaying（影响：BrickPlayer 2 处 + MagicaLego 4 处 = **6 callsites**）
   - `engine.ExecuteCommand` / `GetEngine().ExecuteCommand` → `GetEngine().GetCommandHistory().Execute`（影响：EditorScriptExecutor ~16 处 + EditorMain 1 处 = **17 callsites**）
   - 同理 Undo/Redo/CanUndo/CanRedo
2. `GetCommandSystem` → `GetCommandHistory`（影响：EditorMain 2 处 = **2 callsites**）
3. 删除 6 个 façade 方法 + 1 个别名。

**风险：** 极低。纯改名 + 删行。

---

### C1 · `GetRenderer()` / `GetRendererPtr()` / `GetScene()` / `GetScenePtr()` 统一

**问题：** [Engine.hpp:165-167](src/Runtime/Engine.hpp:165), [178-179](src/Runtime/Engine.hpp:178)

```cpp
Vulkan::VulkanBaseRenderer& GetRenderer() { return *renderer_; }
Vulkan::VulkanBaseRenderer* GetRendererPtr() { return renderer_.get(); }
const Vulkan::VulkanBaseRenderer* GetRendererPtr() const { return renderer_.get(); }

Assets::Scene& GetScene() { return *scene_; }
Assets::Scene* GetScenePtr() { return scene_.get(); }
```

两份 ref/ptr 等价 API。`renderer_`、`scene_` 在 `Start()` 后保证非空。

**目标：** 只保留 ref 形式 `GetRenderer()` / `GetScene()`。如果有调用方需要 ptr 用 `&engine.GetRenderer()`。

**迁移：** grep `GetRendererPtr` + `GetScenePtr` 替换为 `&...`。预计 < 10 处。

**风险：** 低。

---

### C2 · 收敛 NextEngineHelper 投影 API

**问题：** [NextEngineHelper.h:17-22](src/Runtime/Utilities/NextEngineHelper.h:17)

```cpp
glm::vec3 ProjectScreenToWorld(glm::vec2 locationSS);          // 名字像点，实际只返回 dir
glm::vec3 ProjectWorldToScreen(glm::vec3 locationWS);          // legacy，z>=1 表示剔除
bool TryProjectWorldToScreen(const glm::vec3&, ImVec2&);       // 新风格
bool TryProjectWorldToScreen(const Camera&, const glm::vec3&, ImVec2&);
bool TryProjectWorldToScreen(const NextGameInstanceBase&, const glm::vec3&, ImVec2&);
void GetScreenToWorldRay(glm::vec2, glm::vec3& origin, glm::vec3& dir);
```

`ProjectScreenToWorld` 名字暗示返回点，实际丢弃 `origin` 只返回 `dir`，BrickPlayer/MagicaLego 4 处调用都在外面单独维护 `cachedCameraPos_` —— 是一个会引导用错的 API。

`ProjectWorldToScreen(vec3)->vec3` 是 legacy，`DrawAuxLine`/`DrawAuxPoint` 内部还在用它（[NextEngineHelper.cpp:160, 246](src/Runtime/Utilities/NextEngineHelper.cpp:160)），但应用层都迁到 `TryProjectWorldToScreen` 了。

**目标：**
- 删除 `ProjectScreenToWorld`，4 处调用改为 `GetScreenToWorldRay(mousePos, origin, dir)`，原本读 `cachedCameraPos_` 改为读 `origin`（更符合实际语义）。
- 把 legacy `ProjectWorldToScreen(vec3)->vec3` 设为 `private`/`detail`，仅供 `DrawAux*` 内部使用，或改用 `TryProjectWorldToScreen`。
- 三个 `TryProjectWorldToScreen` 重载保留，但改命名前缀避免混淆：保留 `TryProjectWorldToScreen` 覆盖 `(vec3, ImVec2&)` 的常用形态，其它两个改名 `TryProjectWorldToScreenWithCamera(camera, ...)` / `TryProjectWorldToScreenForGame(gameInstance, ...)`，让调用点显式选择。
- 顺便把 `DrawAuxOBB` 仅用于 NextPhysics（[NextPhysics.cpp:932](src/Runtime/Subsystems/NextPhysics.cpp:932)），如果只有这一处可以下沉到 `Runtime::DrawPhysicsDebugOverlay` 内联，删 `DrawAuxOBB` 公共 API。

**迁移：**
1. `ProjectScreenToWorld` 4 处替换为 `GetScreenToWorldRay`：
   - [BrickPlayer:772](src/Application/BrickPlayer/BrickPlayerGameInstance.cpp:772)
   - [MagicaLego:1220, 1357, 1386](src/Application/MagicaLego/MagicaLegoGameInstance.cpp:1220)
2. `ProjectWorldToScreen(vec3)->vec3` 仅在 NextEngineHelper.cpp 内部用，标 `static` 或挪到匿名命名空间。
3. `DrawAuxOBB` 仅 1 处使用，评估是否值得保留为公共 API；不保留则内联到调用处。
4. 给保留的 `TryProjectWorldToScreen` 加文档：什么时候用 game-instance 重载 vs 普通版本（前者 + override-camera 时取相机视图）。

**风险：** 低。投影几何已在 P0-3 验证过；这次只是改名+删冗余。

---

### C3 · RenderComponent 双 setter / getter 收敛 + 修 NodeUtils 隐式 bug

**问题：** [RenderComponent.h:19-25](src/Runtime/Components/RenderComponent.h:19)

```cpp
void SetMaterial(const std::array<uint32_t, 16>& materials)  { materialIdx_ = materials; }
std::array<uint32_t, 16>& Materials()       { return materialIdx_; }
const std::array<uint32_t, 16>& Materials() const { return materialIdx_; }

// 反射用：
const std::array<uint32_t, 16>& GetMaterials() const            { return materialIdx_; }
void SetMaterials(const std::array<uint32_t, 16>& materials)    { materialIdx_ = materials; }
```

`SetMaterial` 与 `SetMaterials`、`Materials()` 与 `GetMaterials()` 完全等价；reflection 注册的是 `SetMaterials/GetMaterials`，业务代码却都调 `SetMaterial`。两套并存 = 维护负担。

**而且** [NodeUtils.cpp:62-73](src/Runtime/Scene/NodeUtils.cpp:62)：

```cpp
void SetMaterial(const std::shared_ptr<Assets::Node>& node, uint32_t materialId)
{
    ...
    render->SetMaterial({materialId});  // <- {materialId} 构造 array<16>，槽位 1..15 全部置 0
}
```

这是一个**隐式 bug**：调用者期望"只覆盖第 0 槽"，但实际把其余 15 个材质槽全清零。LDraw 加载的多材质模型一旦经手 `NodeUtils::SetMaterial` 就会丢材质。`SetPrimaryMaterial`（[NodeUtils.cpp:75-88](src/Runtime/Scene/NodeUtils.cpp:75)）才是正确实现。

**目标：**
1. RenderComponent 只保留 `SetMaterials/GetMaterials`（与 reflection 一致），删 `SetMaterial`/`Materials()` 二态 getter。
2. NodeUtils::SetMaterial **直接删除**——它的语义本来就是 "设置第 0 槽"，应该用 `SetPrimaryMaterial` 调用。把 `SetPrimaryMaterial` 重命名为 `SetMaterial0` 或保留 `SetPrimaryMaterial`。
3. 全部调用点（共 ~12 处）改为 `SetPrimaryMaterial`。
4. 给 `SetMaterials`（多槽设置）补一个 `NodeUtils::SetAllMaterials(node, std::array<...>)`，避免应用层反射 `render->SetMaterials(matArr)`。

**迁移：**
1. 重命名 `Materials()` (non-const) → 删除（调用方用 `GetMaterials()` + `SetMaterials()`）。`Materials() const` 重命名为 `GetMaterials()`，去掉 non-const 引用通道。
2. `Materials()` → `GetMaterials()`，影响 ~13 处。
3. `render->SetMaterial(...)` → `render->SetMaterials(...)`，影响 ~6 处。
4. NodeUtils::SetMaterial 删除，所有引用（~12 处）改为 `SetPrimaryMaterial`。

**风险：** 中。Bug 修复可能"修好"了 LDraw 多材质场景但暴露其他依赖错误行为的代码——需跑一遍 BrickPlayer 多材质 LDraw 场景。

---

### C8 · 删除 Engine.hpp 中的死代码

**问题：** [Engine.hpp:138-150, 192](src/Runtime/Engine.hpp:138)

```cpp
class NextComponent : std::enable_shared_from_this<NextComponent>
{
public:
    NextComponent() = default;
    std::string name_;
    int id_;
};

class NextActor
{
public:
    std::vector<NextComponent*> components;
};

// in NextEngine
uint32_t GetTestNumber() const { return 20; }
```

`NextComponent`/`NextActor` 在 C++ 业务代码里没人用（`grep` 仅命中 QuickJS 反射注册）。`GetTestNumber()` 返回硬编码 20，仅用于 QuickJS 反射 demo。

**目标：** 全部删除。如果 QuickJS 测试需要它们，挪到 Tests 目录或直接给 QuickJS 测试用例换一个真实方法。

**迁移：**
1. 删除 `NextComponent`/`NextActor`/`GetTestNumber`。
2. [QuickJSEngine.cpp:854, 942-951](src/Runtime/Subsystems/QuickJSEngine.cpp:854) 移除注册行（或换成 `GetTotalFrames` 等已有的真方法）。
3. JS 端 `Engine.d.ts` 同步删 `NextComponent` 类型与 `GetTestNumber` 方法。

**风险：** 极低。

---

## P1 任务 — 接着做（结构性改动、需要小心 callsite）

### C7 · 调试 ShowFlag 归位

**问题：** [Engine.hpp:182-185](src/Runtime/Engine.hpp:182) + [ShowFlags.hpp](src/Runtime/Config/ShowFlags.hpp)

NextEngine 有 4 个调试可见性 API：

```cpp
bool IsPhysicsDebugOverlayVisible() const;
void SetPhysicsDebugOverlayVisible(bool);
bool IsGraphicsDebugPanelVisible() const;
void SetGraphicsDebugPanelVisible(bool);
```

且字段 `showPhysicsDebugOverlay_`/`showGraphicsDebugPanel_` 是 `NextEngine` 私有成员。语义上这两个跟 `ShowFlags::DebugDraw_PhysicsBodies/ShowVisualDebug` 是一类——是显示开关。

**目标：** 把这两个布尔挪到 `ShowFlags`，注册成 cvar `debug.physics.overlay` / `debug.graphics.panel`。删 4 个 Engine 方法，调用方读写 `engine.GetShowFlags().DebugPhysicsOverlay`。

**迁移：**
1. ShowFlags 添加 `DebugPhysicsOverlay`/`DebugGraphicsPanel` 字段。
2. EngineCVars 注册它们为 cvar（保持 F1/F2 快捷键能切换）。
3. NextEngine 内部 `OnRendererPostRender` 读 `showFlags_.DebugPhysicsOverlay` 而不是私有字段。
4. F1/F2 处理 [Engine.cpp:1318-1328](src/Runtime/Engine.cpp:1318) 切换 `showFlags_` 字段。
5. 删除 4 个 Is/Set 方法 + 2 个私有字段。
6. 调用方（CharacterDemo:348-349、BrickPlayerGameInstance.hpp:72-73、Brotato3D:70、KongLie3D:293-294）改为读写 ShowFlags。

**风险：** 低-中。F1/F2 行为需要回归测试。

---

### C5 · CVar 列表/匹配 API 收敛

**问题：** [CVarSystem.cpp:557-632](src/Runtime/Config/CVarSystem.cpp:557)

```cpp
ListCVars(prefix)         -> "name = value" 字符串、排序、无上限
CompleteCVars(query, limit, *total) -> 名字、prefix-then-substring、有上限
GetMatchingNames(prefix, limit)     -> 名字、prefix-only、有上限
```

三套相似 API，`ListCVars` 实际只在 `ExecuteCommand` 内部用一次（[CVarSystem.cpp:352](src/Runtime/Config/CVarSystem.cpp:352)），`CompleteCVars` 也仅 [362](src/Runtime/Config/CVarSystem.cpp:362) 一处 + Tests，`GetMatchingNames` 在 UserInterface 用 2 次。

**目标：** 合并成一个：

```cpp
struct FCVarMatchOptions
{
    bool prefixThenSubstring = false; // false = prefix-only
    bool includeValue = false;         // true = "name = value"
    size_t limit = 0;                  // 0 = unlimited
};

std::vector<std::string> Match(const std::string& query,
                               const FCVarMatchOptions& options,
                               size_t* totalMatches = nullptr) const;
```

**迁移：**
1. 实现新 `Match`，让旧三个内部转发，标 `[[deprecated]]`。
2. ~3 个 callsite 改造。
3. 删旧三个。

**风险：** 低。内部 API。

---

### C6 · Node 变换 setter 自动 RecalcTransform

**问题：** [Node.cpp:60-73](src/Assets/Core/Node.cpp:60)

```cpp
void Node::SetTranslation(glm::vec3) { translation_ = ...; }   // 不调 RecalcTransform!
void Node::SetRotation(glm::quat)    { rotation_ = ...; }
void Node::SetScale(glm::vec3)       { scaling_ = ...; }
```

调用方必须自己接 `node->RecalcTransform(true)`，否则 `transform_` / `WorldTranslation()` 返回的还是旧值。`NodeUtils::SetTranslation/Rotation/Scale` 是为这个补的兜底（[NodeUtils.cpp:104-135](src/Runtime/Scene/NodeUtils.cpp:104)），但代码里仍有混用：直接 `node->SetTranslation` 不调 RecalcTransform 的地方（[Brotato3D 多处](src/Application/Brotato3D/Brotato3DEffectSystem.cpp:514)）。

**目标：** 让 `Node::SetTranslation/Rotation/Scale` 内部自动 `RecalcTransform(true)`。删除 `NodeUtils::SetTranslation/Rotation/Scale`（或改为 `BatchTranslate(node, list)` 性能版）。

**迁移：**
1. `Node::SetXxx` 末尾调 `RecalcTransform(true)`。
2. 删除 `NodeUtils::SetTranslation/Rotation/Scale`（共 ~50 处使用）。
3. 调用点 `NodeUtils::SetTranslation(n, t)` → `n->SetTranslation(t)`（注意 null check —— `NodeUtils` 包了 null guard，`Node::SetTranslation` 是成员函数，调用方需要自己 null check 或保证非空）。
4. 性能敏感场景（每帧批量更新很多节点）保留批量 API：`NodeUtils::BatchSetTranslations(span<Node*>, span<vec3>)` 内部一次性更新所有 + 一次根 RecalcTransform。**仅在 profile 显示是热点时才加**。

**风险：** 中。
- 风险点：原本"先 SetTranslation 再 SetRotation 再 SetScale 再统一 RecalcTransform"的代码会从 1 次重算变 3 次，性能退化。
- 需 profile 物理同步 / 战斗系统的高频 setter，确认无回归。

---

### C9 · Scene 加载 API 三合一

**问题：** [Engine.hpp:231-240](src/Runtime/Engine.hpp:231) + [Engine.cpp:1467-1488](src/Runtime/Engine.cpp:1467)

```cpp
void RequestLoadScene(std::string sceneFileName);
void RequestLoadSceneAdd(std::string sceneFileName);
void RequestLoadSceneAdd(std::string sceneFileName, const SceneAppendOptions& options);
```

第 2、3 个 `RequestLoadSceneAdd` 是同一函数的重载（无 options 转发到有 options 版本）。`RequestLoadScene` 与 `RequestLoadSceneAdd` 路径几乎一样，只是 `LoadScene` 会清场景而 `LoadSceneAdd` 累加。

**目标：** 合并为一个：

```cpp
struct FSceneLoadRequest
{
    std::string filename;
    bool append = false;
    bool placeOnHit = false;
    glm::vec3 hitPosition{0.0f};
};
void RequestLoadScene(FSceneLoadRequest request);
```

**迁移：**
1. 新 API 实现包装现有 `LoadScene/LoadSceneAdd` 路径。
2. 18 处 callsite 改造（多数 `RequestLoadScene("X.proc")` 改成 `RequestLoadScene({.filename = "X.proc"})`）。
3. 删除两个 SceneAdd 重载。

**风险：** 低。

---

### C10 · 截图 API 三合一

**问题：** [Engine.hpp:206, 226-228](src/Runtime/Engine.hpp:206)

```cpp
void SaveScreenShot(string, x, y, w, h);                         // 同步立即写
void RequestScreenShot(string filename);                          // 异步：下一帧 ScreenShot::SaveSwapChainToFile
void RequestHighQualityScreenShot(string, uint32_t accumulateFrames); // 累积 N 帧后写
bool IsCapturingHighQuality() const;
```

三个状态、三组管线变量（`screenShotRequested_/Filename_`、`hqCaptureFramesRemaining_/TotalFrames_/Filename_/PrevProgressive_/PrevPreFrames_`）。

**目标：** 合一：

```cpp
struct FScreenShotSpec
{
    std::string filename;       // 空则自动时间戳
    int x = 0, y = 0, width = 0, height = 0;  // 0 = full
    uint32_t accumulateFrames = 0;   // 0 = 当前帧；>0 = 累积模式
    bool sync = false;          // true = 立即；false = next-frame
};
void RequestScreenShot(FScreenShotSpec spec);
bool IsCapturingScreenShot() const;
```

**迁移：**
1. 一个内部状态机统一三种模式。
2. 调用 callsites 全部走 spec。
3. 删 3 个旧方法 + 重命名 `IsCapturingHighQuality` → `IsCapturingScreenShot`。

**风险：** 低-中。HQ 累积要正确恢复 progressive 状态——需测一次 HQ 截图。

---

### C4 · 统一 i18n / 删 Utilities::Localization

**问题：** 当前两套并存：

1. **`Utilities::Localization`**（[Utilities/Localization.hpp](src/Utilities/Localization.hpp)）：纯 header，static 全局 map，`LOCTEXT(text)` 宏、key=英文文本，119 处使用，几乎都在 `gkNextRenderer.cpp` 与 Editor 面板。
2. **Brotato3DGameInstance::Localize**（[Brotato3DGameInstance.cpp:222](src/Application/Brotato3D/Brotato3DGameInstance.cpp:222)）：JSON-based、key=点路径、值=zh 文本；Brotato3DUI 内 50+ 处使用。
3. **KongLie3D::U8Text**（[KongLie3DAudio.hpp:11](src/Application/KongLie3D/KongLie3DAudio.hpp:11)）：`u8"中文"` 编译期硬编码。

P1-4（上轮提出的引擎 i18n）当时未实施。

**目标：** 引入 `Runtime/Subsystems/NextLocalization.{h,cpp}`：

```cpp
class NextLocalization
{
public:
    bool LoadFromJson(string path, string_view language = "zh");
    bool LoadFromTxt(string path, string_view language);  // 兼容旧 ; 分隔
    void SetLanguage(string_view);

    std::string Get(string_view key, string_view fallback = {}) const;
    template <typename... Args>
    std::string Format(string_view key, string_view fallback, Args&&... args) const;
};
```

通过 `NextEngine::GetLocalization()` 暴露。**`LOCTEXT` 宏保留**，重定向到 `NextEngine::GetInstance()->GetLocalization()->Get(srcText, srcText)`，所有 119 处现有调用零改动。

**迁移路线：**

阶段 1（兼容并存）：
1. 实现 `NextLocalization`，初始化时既能读 `assets/locale/*.txt` 又能读 `assets/configs/*/i18n.json`。
2. 删除 `Utilities/Localization.hpp` 的实现，仅保留 `LOCTEXT` 宏 forward 到新系统。
3. Brotato3D `Localize()` 改为 `engine_->GetLocalization()->Get(key, fallback)`，删私有方法。
4. KongLie3D 把 `u8"羁绊激活：{}"` 等 ~30 处迁到 JSON（独立 PR，文本量大）。

阶段 2（清场）：
5. 验证 119 处 `LOCTEXT` 全走新系统后，把宏从 `Localization.hpp` 完全挪到 `NextLocalization.h`，删除 `Utilities/Localization.hpp`。

**风险：** 中。
- 文本编码需测；
- KongLie 文本迁移大；建议拆成 PR-A（引擎能力 + Brotato 接管）+ PR-B（KongLie 文本迁移）。

---

### C12 · SceneBuilder 渗透到所有 BeforeSceneRebuild

**问题：** 上一轮已完成 SceneBuilder 引入（[Runtime/Scene/SceneBuilder.cpp](src/Runtime/Scene/SceneBuilder.cpp)），但应用层 `BeforeSceneRebuild` 中仍存在直写：

```cpp
// CharacterDemo:187, gkNextRenderer:164, Brotato3DEffectSystem:32
materials.push_back({Assets::Material::Lambertian(glm::vec3(...))});
materials.push_back({Assets::Material::DiffuseLight(color * intensity)});
```

未走 `SceneBuilder::AddLambertianMaterial` / `AddDiffuseLightMaterial`。

**目标：** 全场替换。同时把场景**运行时**新增材质的路径也走统一封装：

```cpp
// 当前：
const uint32_t id = engine_->GetScene().AddMaterial({Material::DiffuseLight(color * 650.0f)});
// 目标：
const uint32_t id = SceneBuilder::AddDiffuseLightMaterialToScene(engine_->GetScene(), color, 650.0f);
```

**迁移：**
1. 在 SceneBuilder 增 `AddXxxMaterialToScene(Scene&, ...)` 重载（运行时增量加材质）。
2. grep 替换：`Assets::Material::Lambertian|DiffuseLight` 在 `src/Application/` 与 `src/Runtime/` 内的裸用法（约 7 处）。
3. 应用层不应该 `#include "Assets/Data/Material.hpp"` 直接构造材质——验证迁移完后能否从应用层移除该头文件依赖。

**风险：** 极低。

---

## P2 任务 — 巩固层（影响面广，单 PR 工作量大）

### C11 · 命名空间方言统一

**问题：** Runtime 层下命名混杂：

| 当前命名 | 文件 | 风格 |
| --- | --- | --- |
| `NextEngineHelper`, `NextJson`, `NextCVar`, `NextAI`, `NextRenderer`, `NextUI::Painter`, `NextUI::Scaling`, `NextUI::Notification`, `NextPlatform::UserPaths` | 多处 | `Next*` 前缀 |
| `SceneBuilder`, `NodeUtils`, `FontLoader`, `ScreenShot` | Runtime/Scene、Runtime/Editor、Runtime/ | 无前缀、平铺 |
| `Runtime::Editor`, `Runtime::Component::SelectionUtils`, `Runtime::GraphicsDebugPanel` | Runtime/Editor、Runtime/Command、Runtime/Utilities | `Runtime::` 嵌套 |
| 类前缀 `NextEngine`, `NextAudio`, `NextPhysics`, `NextAnimation`, `NextCharacterController` | Runtime/Subsystems | `Next*` 类前缀 |

**目标：** 收敛到二选一规则：

**方案 A（推荐）：** 新规则——`namespace Runtime` 下分模块。
- `Runtime::Engine`（去掉 `Next` 前缀，类名 `Runtime::Engine`）
- `Runtime::Audio`、`Runtime::Physics`、`Runtime::Animation`
- `Runtime::AI::Service` / `Runtime::AI::VoiceInput`
- `Runtime::CVar`、`Runtime::Json`、`Runtime::Localization`
- `Runtime::Scene::SceneBuilder` / `Runtime::Scene::NodeUtils`
- `Runtime::Editor::FontLoader` / `Runtime::Editor::Painter` / ...
- `Runtime::Platform::UserPaths`

**方案 B（保守）：** 保留 `Next*` 前缀，但所有自由函数命名空间也加上：
- `NextScene::Builder`, `NextScene::NodeUtils`, `NextEditor::FontLoader`, `NextEditor::Painter`, ...
- `NextRuntime::ScreenShot`

**风险：** 大。整个代码库 grep+替换；影响每个 PR 的合并冲突。建议 P3 阶段在所有 P0/P1 落地后做一次性扫尾。

---

### C13 · 任务系统边界文档化（不重构）

**问题：** 三种任务通道：

| API | 用途 | 线程 | 用例 |
| --- | --- | --- | --- |
| `NextEngine::AddTickedTask(lambda)` | 每帧调；返回 true 移除 | 主线程 (`Tick`) | 持续 ~N 帧的视觉 UI fade、cooldown 等 |
| `NextEngine::AddTimerTask(delay, lambda)` | 延时执行、返回 false 重启 | 主线程 (`Tick`) | 自动重连、定时存档 |
| `TaskCoordinator::AddTask` / `AddParralledTask` | 后台 worker 线程池 | worker | CPU 加速结构构建、纹理上传 |

不算冗余，但语义/线程安全/生命周期不清晰，新人无所适从。

**目标：** **不重构**，只在 `Engine.hpp` 与 `TaskCoordinator.hpp` 头部加注释明确界限：

```cpp
// AddTickedTask: runs every Tick on the main thread until the lambda returns true. Use for
// frame-bound effects (UI fades, cooldown ticks). Lambdas can touch the scene safely.
// For long-running CPU work or anything off the render path, use TaskCoordinator instead.
```

如果将来发现混用导致 bug，再考虑统一为单一 API。

**风险：** 0（仅文档）。

---

## P3 任务 — 低收益但顺手做

### C14 · `NextEngine::GetInstance()` 的合理收敛

**现状：** `GetInstance()` 出现 158 处，多数是 `GameInstance::OnTick` 或类成员里直接调，明明 `GameInstance` 已持有 `engine_` 指针。

**目标：** 把构造时已可注入 engine ref 的类（`Brotato3DAudio.hpp` 各 inline 函数、`MagicaLego` 等）改为接受 `NextEngine&` 参数，不再 `GetInstance()`。

**例外保留：**
- 工具/帮助函数（`NextEngineHelper::*`）必须无参；
- `Node::TickVelocity` 用 `GetInstance()` 取物理引擎（可考虑改为通过 `NodeContext` 注入，但成本高）；
- Skinned mesh / scene 内部访问主线程帮助函数。

**风险：** 中。改动面广；建议增量做。

---

## 推荐执行顺序

| 周次 | PR 列表 | 备注 |
| --- | --- | --- |
| 1 | **C0**（删 audio/command façade）· **C1**（删 Ptr getter）· **C8**（删死代码） | 三个超低风险 PR 并行；先收 trivial 红利 |
| 2 | **C2**（投影 API 收敛）· **C3**（RenderComponent + NodeUtils bug 修复）· **C12**（SceneBuilder 渗透） | 关键 bug 修复在此周；C3 后跑 LDraw 多材质回归 |
| 3 | **C7**（调试标志归位 ShowFlags）· **C5**（CVar 匹配统一）· **C9**（Scene 加载三合一） | 中等改动 |
| 4 | **C10**（截图 API 三合一）· **C6**（Node setter 自动 Recalc，需 profile） | C6 需性能验证 |
| 5 | **C4 PR-A**（NextLocalization + Brotato 接管）· **C13**（任务系统注释） | 大头 |
| 6 | **C4 PR-B**（KongLie 文本迁移） | 文本工作量 |
| 7+ | **C14**（singleton 收敛）· **C11**（命名空间统一） | 扫尾，巨量 grep |

---

## 验证清单

每 PR 必须：

1. **Build:** `./build.bat --preset full-windows --reconfigure`（或对应平台 full-* preset）
2. **Run sanity:**
   - `gkNextRenderer` 启动 → 确认 `LOCTEXT` UI 文字仍显示（C4）
   - `Brotato3D` 一局打满 10 波（C0/C2/C6/C7 后必跑）
   - `KongLie3D` 一局打通三个 level（拖拽/伤害弹窗/F1/F2 切换）
   - `BrickPlayer` LDraw 多材质场景加载（C3 必跑）
   - `gkNextEditor` 一次 Undo/Redo 链（C0 必跑）
3. **Tests:** `gkNextUnitTests`（含 `Test_RenderComponent`、`Test_CVarSystem`）
4. **LOC 校验:** PR 描述写明删了哪几个公开方法、修改了多少 callsites

---

## 显式不要做的事

下面看上去也是冗余、但不建议在本计划周期内动：

- **`NextEngine` 单例化重构**：不要尝试干掉 `GetInstance()` 把所有依赖改为 DI——158 处 callsites 涉及静态生命周期、QuickJS 反射、Editor 命令系统、物理 body lookup，单 PR 改不动。仅做 C14 的渐进收敛。
- **`SceneList::AllScenes` 静态数组**：MagicaLego/gkNextBenchmark 依赖；改成 cvar 列表收益小，破坏面大。
- **`Statistics` 结构体**：UserInterface 用，字段杂乱但稳定。不动。
- **`UserSettings` 字段拆分**：很多字段是 Renderer 配置 + Camera + UI 混在一起，但 Renderer 还在跟着改。等 Renderer 重构稳定后再分。
- **`ImGuiPainter` / `NotificationCenter` / `FontLoader` 等新 helper**：刚上线，先观察使用模式，不重构。

---

## 评估收益

| 类别 | 估计删除 LOC | 估计改名 callsite |
| --- | --- | --- |
| P0 全部完成（C0+C1+C2+C3+C8） | -250 LOC + 修 1 个隐 bug | ~50 callsites |
| P1 全部完成（C5+C6+C7+C9+C10+C12+C4） | -350 LOC + 1 个新引擎子系统 (NextLocalization +200 LOC) | ~120 callsites |
| P2/P3 命名空间 + singleton 收敛 | -50 LOC | ~200 callsites |

**核心收益不在 LOC，而在 API 表面缩小：**
- `NextEngine` 公共方法从 ~70 个降到 ~55 个
- `NextEngineHelper` 从 6 个投影函数降到 3 个，命名清晰
- `RenderComponent` 双套 setter 收敛、修一个材质丢失的隐 bug
- 删除一个完整的旧 i18n 系统（`Utilities::Localization`）
- 消除"调引擎方法"vs"调子系统方法"的二态选择困境
