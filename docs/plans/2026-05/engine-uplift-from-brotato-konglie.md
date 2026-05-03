# Engine Uplift Report — Brotato3D / KongLie3D 共性下沉

> **作者：** 探索 Agent · 2026-05-03
> **范围：** `src/Application/Brotato3D/` (~7.2k LOC) + `src/Application/KongLie3D/` (~6.7k LOC) 中可下沉到 `src/Runtime/` 的实现。
> **目的：** 为后续重构 Agent 列出明确的、可独立交付的子任务，每条都包含证据、目标位置、迁移路径与风险。
> **执行约定：** 每条任务一个 PR；优先级 P0 → P1 → P2；命名遵循 `.clang-tidy`（PascalCase 类型/函数、camelCase 变量、camelCase_ 私有成员）。

---

## TL;DR — 重点结论

1. 两款已 MVP+生产化打磨的小游戏在 Application 层重复实现了 **大量本应属于引擎层的工具**：JSON 加载、节点显隐/材质/变换 setter、`AddLambertMaterial`、`CreateRenderNode`、`WorldToScreen`、SFX 防抖、字体加载、ImGui 绘制基元。
2. **优先级 P0 共 7 项**（共节省 ~1.0k LOC 重复代码、解锁第三个游戏 Voyage3D），改动局部、风险低。
3. **优先级 P1 共 6 项**（Toast/世界空间 UI/i18n/存档/UI 绘制），是把 Brotato/KongLie 提升到生产级所需的"软基建"，也是 Voyage3D 起步必须的。
4. **优先级 P2 共 4 项**（屏幕震动、Style 预设、HitStop/时间缩放、窗口默认），收益偏小但顺手做掉能让 GameInstance 显著瘦身。

---

## P0 任务清单（先做）

### P0-1 · 统一节点显隐/变换 helpers

**问题：** `Brotato3DGameInstance` 和 `KongLie3DGameInstance` 都重复实现以下 5 个一行包装：

```cpp
// Brotato3DEffectSystem.cpp:519-577
HideNode / ShowNode / SetNodeMaterial / SetNodeTranslation / SetNodeRotation / SetNodeScale
```

KongLie3D 在 `KongLie3DGameInstance.cpp:836-839`、`KongLie3DBoard.cpp:20-27` 也用 `RenderComponent->SetVisible(...)` 等内联了相同模式。

**目标位置：** `src/Runtime/Components/RenderComponent.h` 旁边新建 `src/Runtime/Scene/NodeUtils.h`：

```cpp
namespace NodeUtils
{
    void SetVisible(const std::shared_ptr<Assets::Node>& node, bool visible);
    void SetMaterial(const std::shared_ptr<Assets::Node>& node, uint32_t materialId);
    void SetTranslation(const std::shared_ptr<Assets::Node>& node, const glm::vec3& translation);
    void SetRotation(const std::shared_ptr<Assets::Node>& node, const glm::quat& rotation);
    void SetScale(const std::shared_ptr<Assets::Node>& node, const glm::vec3& scale);
}
```

**迁移：**
1. 新建 `Runtime/Scene/NodeUtils.{h,cpp}`，函数体复用 [Brotato3DEffectSystem.cpp:519](src/Application/Brotato3D/Brotato3DEffectSystem.cpp:519) 的实现。
2. 删除 `Brotato3DGameInstance::HideNode/ShowNode/SetNode*`（hpp:180-185 + cpp 实现）。
3. KongLie3D 内联点替换为 `NodeUtils::SetVisible`。
4. MagicaLego 与 BrickPlayer 也搜索 `RenderComponent>SetVisible/SetMaterial` 一并替换。

**风险：** 极低。纯纯改名 + 头文件移动。

---

### P0-2 · 把 `AddLambertMaterial` + `CreateRenderNode` 下沉到 SceneBuilder

**问题：** 至少 3 处独立实现：
- [Brotato3DCommon.hpp:25-46](src/Application/Brotato3D/Brotato3DCommon.hpp:25)
- [KongLie3DBoard.cpp:15-34](src/Application/KongLie3D/KongLie3DBoard.cpp:15)（叫 `CreateBoardCell` + `AddLambert`）
- [KongLie3DGameInstance.cpp:77-92, 194-198](src/Application/KongLie3D/KongLie3DGameInstance.cpp:77)（再写一遍）

`grep` 显示 `FProcModel::CreateBox/Sphere` 在 Application 层调用 27 处，每处都跟一个手写 `materials.push_back({Material::Lambertian(...)})` + 手写 `Node::CreateNode + RenderComponent`。

**目标位置：** `src/Runtime/Scene/SceneBuilder.h`：

```cpp
namespace SceneBuilder
{
    uint32_t AddLambertianMaterial(std::vector<Assets::FMaterial>& materials, const glm::vec3& color);
    uint32_t AddDiffuseLightMaterial(std::vector<Assets::FMaterial>& materials, const glm::vec3& color, float intensity);

    std::shared_ptr<Assets::Node> CreateRenderNode(
        std::string_view name,
        const glm::vec3& translation,
        const glm::vec3& scale,
        uint32_t instanceId,
        uint32_t modelId,
        uint32_t materialId,
        bool visible = true,
        const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
}
```

**迁移：**
1. 实现拷自 `Brotato3DCommon.hpp:25-46`。
2. 删除 `Brotato3DUtil::AddLambertMaterial`/`CreateRenderNode`、`KongLie3DBoard.cpp` 匿名工具、`KongLie3DGameInstance.cpp` 匿名工具。
3. 27 处调用点全部改为 `SceneBuilder::CreateRenderNode(...)`。
4. Brotato3D 中的 `Brotato3DEffectSystem.cpp:24-35` `addLightMaterial` lambda（含按颜色去重缓存）保留在原处，但底层调用改为 `SceneBuilder::AddDiffuseLightMaterial`。

**风险：** 低。`CreateRenderNode` 默认参数顺序需对齐：当前 Brotato 版与 KongLie 版的参数顺序不一致，统一以 Brotato 版为准。

---

### P0-3 · 统一 `WorldToScreen`，废弃应用层副本

**问题：** 引擎已有 [`NextEngineHelper::ProjectWorldToScreen`](src/Runtime/Utilities/NextEngineHelper.cpp:27)，但两个游戏都没用：

- Brotato3D 写自己的 `Brotato3DGameInstance::WorldToScreen`（[Brotato3DGameFlowSystem.cpp:10](src/Application/Brotato3D/Brotato3DGameFlowSystem.cpp:10)），用 `prevUBO.ViewProjection` + ImGui viewport，返回 `bool` + 写出 `ImVec2`，处理 `clip.w<=0` 与 `ndc.z` 范围。
- KongLie3D 写自己的 `ProjectWorldToScreen`（[KongLie3DUI.cpp:289](src/Application/KongLie3D/KongLie3DUI.cpp:289)），从 `OverrideRenderCamera` 重建 `Projection*ModelView`（因为它的相机是 override，**不进 prevUBO**？需要验证）。

引擎现有 API 的不足：
- 返回 `glm::vec3`，`.z >= 1` 表示被剔除——使用方还得自行判断；
- 未处理 `clip.w <= 0`（背后点）会得到错误结果；
- 未提供 ImGui viewport 适配。

**目标位置：** 扩展 `src/Runtime/Utilities/NextEngineHelper.h`：

```cpp
namespace NextEngineHelper
{
    // 替代或补充现有 ProjectWorldToScreen
    bool TryProjectWorldToScreen(const glm::vec3& worldPos, ImVec2& outImGuiPos);

    // 当 GameInstance::OverrideRenderCamera 的相机与 prevUBO 不一致时使用
    bool TryProjectWorldToScreen(const Assets::Camera& camera, const glm::vec3& worldPos, ImVec2& outImGuiPos);
}
```

**迁移：**
1. 在 `NextEngineHelper` 实现两个 `Try*` 重载，正确处理 clip.w 和 NDC 范围（参考 KongLie 版本里 `ndc.z<-1||>1||ndc.x/y<-1.2||>1.2` 的剔除逻辑）。
2. 删除 `Brotato3DGameInstance::WorldToScreen`（hpp:126 + cpp:10）；UI 中 12 处调用点改为 `NextEngineHelper::TryProjectWorldToScreen`。
3. 删除 KongLie3D 匿名 `ProjectWorldToScreen`（UI:289）；调用点改为 `TryProjectWorldToScreen(camera, worldPos, screen)`，相机从 `gameInstance.OverrideRenderCamera(camera)` 取。
4. 第二个重载需要内部从 viewport 推算 aspect、构建 `glm::perspective` 投影矩阵——抄 KongLie 版的逻辑。

**风险：** 中。**关键验证：** Brotato3D 的 prevUBO 与它自己的 `OverrideRenderCamera` 是否每帧同步。如果 prevUBO 始终是上一帧的相机（包括屏幕震动），那 Brotato 必须用第二个重载，否则 HUD 会和震动相机错位。后续 Agent 实现时务必做一次跑测验证。

---

### P0-4 · 抽离防抖 SFX + BGM 管理为引擎子系统

**问题：** [`Brotato3DAudio.hpp:19-289`](src/Application/Brotato3D/Brotato3DAudio.hpp:19) 和 [`KongLie3DAudio.hpp:10-76`](src/Application/KongLie3D/KongLie3DAudio.hpp:10) 各自重复实现：
- 全局 `static unordered_map<string,uint64_t> lastPlayMsBySound` 防抖；
- `static unordered_set<string> missingSounds` 缺文件告警去重；
- `engine->PlaySound(path, false, clamp(volume,0,1))` 包装；
- Brotato 还独有 `PickVariantPath` 随机变体、`StartBgm/StopBgm/RefreshBgmVolume` 单轨 BGM 管理。

**目标位置：** `src/Runtime/Subsystems/NextAudio.h` 增加方法（或新建 `NextSfxBus.h`）：

```cpp
class NextAudio
{
    // existing PlaySound/PauseSound...

    // Debounced one-shot sfx, warns once per missing path.
    void PlaySfx(const std::string& path, float volume = 1.0f, uint64_t minIntervalMs = 50);

    // Random variant selection (replaces Brotato3D::PickVariantPath).
    void PlaySfxVariant(std::initializer_list<std::string_view> candidates,
                        float volume = 1.0f,
                        uint64_t minIntervalMs = 50);

    // Single-track BGM with auto pause-on-switch.
    void PlayMusic(const std::string& path, float volume);
    void StopMusic();
    void SetMusicVolume(float volume);
    const std::string& GetCurrentMusicPath() const;
};
```

**迁移：**
1. 把 `lastPlayMsBySound + missingSounds` 移成 `NextAudio` 的成员；`PickVariantPath` 内嵌到 `PlaySfxVariant`。
2. `Brotato3DAudio.hpp` 保留 *游戏专属* 的语义包装（`PlayWeaponFireSfx(weaponId)`、`PlayHitSfx(damage,isCrit)` 等），但只调 `engine->GetAudio()->PlaySfx*(...)`。
3. `KongLie3DAudio.hpp` 同样保留 `PlayUiClickSfx/PlayAttackHitSfx` 等。
4. `inline float SfxVolume = 0.7f`/`MusicVolume = 0.5f` 改成走 CVar (`audio.sfxVolume` / `audio.musicVolume`)，引擎读取 CVar 应用到 `PlaySfx`/`PlayMusic`。

**风险：** 低。需要注意 `engine->PlaySound` 当前是按路径去重播放（同一路径覆盖前一次）；BGM 在切歌时需要 `PauseSound(currentPath, true)`，迁移时不要改这个语义。

---

### P0-5 · 通用 JSON 配置加载工具

**问题：** [`Brotato3DDataLoader.cpp:13-77`](src/Application/Brotato3D/Brotato3DDataLoader.cpp:13) 和 [`KongLie3DDataLoader.cpp:14-103`](src/Application/KongLie3D/KongLie3DDataLoader.cpp:14) 各自实现：
- `LoadJsonFile(path) -> json` (用 `Utilities::FileHelper::GetPlatformFilePath`)
- `RequireObject/RequireArray` 字段断言
- `ReadVec3` glm::vec3 解析
- KongLie 还有模板 `GetRequiredValue<T>/GetOptionalValue<T>`、`GetRequiredColor`，错误风格是抛异常 + LogAndThrow；Brotato 是返回 bool + SPDLOG_ERROR。

**目标位置：** 新建 `src/Runtime/Utilities/JsonHelpers.h`（搭配 `nlohmann/json.hpp`）：

```cpp
namespace NextJson
{
    nlohmann::json LoadFile(const std::string& path);   // throws on error
    bool TryLoadFile(const std::string& path, nlohmann::json& out);

    template <typename T>
    T GetRequired(const nlohmann::json& obj, const char* key, std::string_view context);

    template <typename T>
    T GetOptional(const nlohmann::json& obj, const char* key, T defaultValue);

    glm::vec3 GetVec3(const nlohmann::json& obj, const char* key, const glm::vec3& fallback);
    glm::vec3 GetRequiredVec3(const nlohmann::json& obj, const char* key, std::string_view context);

    void RequireObject(const nlohmann::json& doc, const char* key, std::string_view context);
    void RequireArray(const nlohmann::json& doc, const char* key, std::string_view context);
}
```

**迁移：**
1. 实现拷自两边的并集；统一抛 `std::runtime_error`（KongLie 风格更鲁棒），上游可用 `try/catch` 转 bool。
2. `Brotato3DDataLoader.cpp` 头部匿名命名空间整体删除，每个 `LoadXxx` 函数体内的 `RequireObject/Array/ReadVec3` 改名。
3. `KongLie3DDataLoader.cpp` 同样删除匿名命名空间里的工具。
4. `LogAndThrow` 也统一到 `NextJson` 内部，不再每个 DataLoader 各自定义。

**风险：** 低。两边异常 vs bool 风格不一致，需要确认调用方都能接受异常；若不能，保留 `TryLoadFile` 重载即可。

---

### P0-6 · ImGui 字体加载 helpers

**问题：** 字体加载在以下 4 处独立实现，模式高度一致：
- [Brotato3DGameInstance.cpp:90-131](src/Application/Brotato3D/Brotato3DGameInstance.cpp:90)
- [KongLie3DGameInstance.cpp:121-133, 350-380](src/Application/KongLie3D/KongLie3DGameInstance.cpp:121)
- [MagicaLegoUserInterface.cpp:179, 192](src/Application/MagicaLego/MagicaLegoUserInterface.cpp:179)
- [gkNextRenderer.cpp:253-256](src/Application/gkNextRenderer/gkNextRenderer.cpp:253)
- [Runtime/Editor/UserInterface.cpp:140](src/Runtime/Editor/UserInterface.cpp:140) (引擎自身)

每处都做：构建中文 glyph range（`ImFontGlyphRangesBuilder` 或 `GetGlyphRangesChineseFull`）→ `AddFontFromFileTTF` → 设置默认字体。

**目标位置：** 新建 `src/Runtime/Editor/FontLoader.h`：

```cpp
namespace FontLoader
{
    struct FFontRequest
    {
        std::string filePath;        // 自动经 FileHelper::GetPlatformFilePath
        float pixelSize = 16.0f;
        bool includeChineseFull = true;
        const char* extraGlyphsUtf8 = nullptr;
        bool setAsDefault = false;
    };

    ImFont* Load(const FFontRequest& request);

    // Convenience for the common case (Chinese-capable UI font):
    ImFont* LoadDefaultUiFont(std::string_view fontPath = "assets/fonts/DroidSansFallback.ttf",
                              float pixelSize = 16.0f);
}
```

**迁移：**
1. 实现内部缓存 glyph range builder 结果（避免重复扫描）。
2. Brotato/KongLie/MagicaLego 的 `OnInitUI()` 改为多次调 `FontLoader::Load`。
3. KongLie 的 `KongLieFonts::Body/Title/Display` 三档由 `Load` 返回值赋值。

**风险：** 低。

---

### P0-7 · 引擎侧 ImGui 纹理请求快捷方法

**问题：** Brotato3D 在 [Brotato3DUI.cpp:36-87](src/Application/Brotato3D/Brotato3DUI.cpp:36) 写了 `LoadUiTexture` + `GetTexturePixelSize`：用 `Assets::GlobalTexturePool::LoadTexture` 异步上传，再 `engine->GetUserInterface()->RequestImTextureByName` 取描述符，再 `stbi_info` 拿尺寸缓存。

MagicaLego 在 [MagicaLegoUserInterface.cpp:1055](src/Application/MagicaLego/MagicaLegoUserInterface.cpp:1055) 走的是同一条路径但没缓存尺寸。

**目标位置：** `src/Runtime/Editor/UserInterface.hpp` 新增：

```cpp
class UserInterface
{
    struct FUiTextureHandle
    {
        ImTextureID textureId = 0;
        ImVec2 pixelSize{0.0f, 0.0f};
        bool valid = false;
    };

    // Loads + caches the texture, registers ImGui descriptor, reads pixel size lazily.
    FUiTextureHandle RequestUiTexture(const std::string& path, bool srgb = true);
};
```

**迁移：**
1. 内部维护 `unordered_map<string, FUiTextureHandle>`。
2. `Brotato3DUI.cpp:36-88` 整段删除，改为 `gameInstance.GetEngine().GetUserInterface()->RequestUiTexture(path)`。
3. `MagicaLegoUserInterface.cpp:1055` 同步替换。

**风险：** 低。

---

## P1 任务清单（生产化必备）

### P1-1 · 通用 Toast / 通知中心

**问题：** [`KongLie3D::FNotificationCenter`](src/Application/KongLie3D/KongLie3DNotifications.cpp) 实现完整：push、生命周期、滑入+淡出动画、最多 4 条、按 kind（Info/Success/Warning/Critical）取强调色、右下角堆叠渲染。

Brotato3D 没有 toast，但有 wave banner / weapon merge banner / damage flash 等"短暂提示"逻辑分散在 `Brotato3DGameInstance` 里——能用 toast 统一掉。

**目标位置：** `src/Runtime/Editor/NotificationCenter.{h,cpp}`：

```cpp
namespace NextUI
{
    enum class ENotificationKind { Info, Success, Warning, Critical };

    class FNotificationCenter
    {
    public:
        void Push(std::string text, ENotificationKind kind, float durationMs = 2500.0f);
        void Update(float deltaMs);
        void Render(float bottomInset, float rightInset) const;
        void Clear();

        // 可选：风格通过依赖注入，避免硬编码 KongLie 配色
        struct FStyle { ImVec4 infoAccent, successAccent, warningAccent, criticalAccent, surface, border; };
        void SetStyle(const FStyle& style);
    };
}
```

**迁移：**
1. 把 `KongLie3DNotifications.cpp` 实现完整搬到引擎层；删除颜色对 `KongLie3D::Style` 的依赖。
2. KongLie3D 改为构造 `NextUI::FNotificationCenter` + `SetStyle(KongLie 配色)`。
3. Brotato3D 起一个 `NextUI::FNotificationCenter` 实例处理 wave/level-up/merge banner（保留大字幕 banner 自己做就行，toast 处理低优先级提示）。

**风险：** 低。Style 注入可避免颜色耦合。

---

### P1-2 · 世界空间浮动文字 / 伤害弹窗

**问题：** 同一概念两处实现：
- Brotato `FFloatingText` / `PushFloatingText` ([Brotato3DGameInstance.hpp:35-43](src/Application/Brotato3D/Brotato3DGameInstance.hpp:35), [Brotato3DCombatSystem.cpp:150](src/Application/Brotato3D/Brotato3DCombatSystem.cpp:150))
- KongLie `FDamagePopup` / `RecordDamagePopup` / `UpdateDamagePopups` ([KongLie3DBattleSystem.cpp:925, 1372](src/Application/KongLie3D/KongLie3DBattleSystem.cpp:925))

字段几乎一样：`worldPos / text / color / lifeMs / fontScale`。渲染都需 `WorldToScreen`。

**目标位置：** `src/Runtime/Editor/WorldSpaceTextOverlay.{h,cpp}`，依赖 P0-3 的 `TryProjectWorldToScreen`：

```cpp
namespace NextUI
{
    struct FWorldTextSpec
    {
        glm::vec3 worldPos;
        std::string text;
        glm::vec4 color = glm::vec4(1.0f);
        float lifeMs = 800.0f;
        float fontScale = 1.0f;
        glm::vec3 driftPerSec = glm::vec3(0.0f, 1.5f, 0.0f); // 默认上飘
    };

    class FWorldSpaceTextOverlay
    {
    public:
        void Push(FWorldTextSpec spec);
        void Update(float deltaMs);
        void Render(ImFont* font = nullptr) const;  // 内部 fadeOut + WorldToScreen
        void Clear();
    };
}
```

**迁移：**
1. Brotato: `floatingTexts_` / `PushFloatingText` / `UpdateFloatingTexts` 全替换为 `FWorldSpaceTextOverlay` 成员；UI 端 [Brotato3DUI.cpp:1417](src/Application/Brotato3D/Brotato3DUI.cpp:1417) 渲染循环改为 `overlay.Render()`。
2. KongLie: `FBattleSystem::damagePopups_` / `RecordDamagePopup` / `UpdateDamagePopups` 替换。

**风险：** 低，但要求 P0-3 先完成。

---

### P1-3 · ImGui 通用绘制基元

**问题：** [Brotato3DUI.cpp:99-318](src/Application/Brotato3D/Brotato3DUI.cpp:99) 实现了通用且不绑游戏的工具：

| 函数 | 行 | 作用 |
| --- | --- | --- |
| `DrawImageContain` | 99 | 居中等比缩放图片到 box |
| `DrawImageCover` | 137 | 填充 box 并裁切 UV |
| `DrawNineSlicePanel` | 178 | 9-slice |
| `DrawPanel` | 239 | 9-slice + 颜色回退 |
| `DrawBar` | 257 | 带文字进度条 |
| `DrawTexturedBar` | 269 | 带 BG/Fill/Frame 三贴图的进度条 |
| `DrawFullscreenDim` | 695 | 全屏遮罩 |
| `GetUiScale` | 16 | viewport 自适应 scale |
| `GetTexturePixelSize` | 68 | `stbi_info` 缓存（已被 P0-7 覆盖） |

KongLie 在 `KongLie3DStyle.hpp` 也有 `ScaleUi(float)` / `ScaleUi(x,y)` 等 viewport 适配。

**目标位置：** `src/Runtime/Editor/ImGuiPainter.h` + `ImGuiScaling.h`：

```cpp
namespace NextUI::Painter
{
    void DrawImageContain(ImDrawList* dl, ImTextureID tex, ImVec2 texSize, ImVec2 boxMin, ImVec2 boxMax,
                          float padding = 0.0f, ImU32 tint = IM_COL32_WHITE);
    void DrawImageCover(...);
    void DrawNineSlicePanel(...);
    void DrawPanel(...);
    void DrawBar(...);
    void DrawTexturedBar(...);
    void DrawFullscreenDim(const ImGuiViewport* viewport, float alpha);
}

namespace NextUI::Scaling
{
    float GetViewportUiScale(const ImGuiViewport* viewport, float baseWidth = 1280.0f, float baseHeight = 720.0f);
}
```

**迁移：**
1. 把 [Brotato3DUI.cpp:99-318, 695-701](src/Application/Brotato3D/Brotato3DUI.cpp:99) 整体迁出；保留 game-specific 的 `DrawMenuBackdrop` 等在原处。
2. KongLie 的 `ScaleUi` 用 `Scaling::GetViewportUiScale` 替代或并存（`UiScale=1.5f` 是固定值，可作为 `Scaling::GetFixedUiScale(1.5f)`）。

**风险：** 低。这些函数无状态，纯绘制。

---

### P1-4 · 引擎侧 i18n 服务

**问题：** Brotato3D 有 [`LoadI18n`](src/Application/Brotato3D/Brotato3DDataLoader.cpp:357) + [`Brotato3DGameInstance::Localize`](src/Application/Brotato3D/Brotato3DGameInstance.cpp:228) + UI 层 `Tr` / `TrFormat` ([Brotato3DUI.cpp:810-822](src/Application/Brotato3D/Brotato3DUI.cpp:810))。仅 zh-CN。

KongLie3D 没有走 i18n，所有中文用 `u8"..."` 内联。Voyage3D 起步前最好统一。

**目标位置：** `src/Runtime/Subsystems/NextLocalization.{h,cpp}`：

```cpp
class NextLocalization
{
public:
    bool LoadFromJson(const std::string& path, std::string_view language = "zh");
    void SetLanguage(std::string_view language);

    std::string Get(std::string_view key, std::string_view fallback = {}) const;

    template <typename... Args>
    std::string Format(std::string_view key, std::string_view fallback, Args&&... args) const;
};

// 通过 NextEngine::GetLocalization() 暴露。
```

**迁移：**
1. 引擎初始化时按 cvar `i18n.language` 决定语言；GameInstance 在 `OnInit` 调 `LoadFromJson`。
2. Brotato3D `Localize/Tr/TrFormat` 全部替换为 `engine->GetLocalization()->Get/Format(...)`。
3. KongLie3D 把硬编码 `u8"羁绊激活：{}"` 等迁到 JSON。可以分阶段——先把 P1-4 的引擎能力上线，KongLie 文本迁移作为单独的小任务。

**风险：** 中。涉及大量字符串迁移；建议 KongLie 文本迁移做成独立 PR。

---

### P1-5 · 跨平台用户存档目录

**问题：** [`Brotato3DGameInstance.cpp:24-31`](src/Application/Brotato3D/Brotato3DGameInstance.cpp:24) 只处理 Windows `%APPDATA%`：

```cpp
if (const char* appData = std::getenv("APPDATA"))
    return std::filesystem::path(appData) / "Brotato3D" / "best.json";
return std::filesystem::current_path() / "Brotato3D" / "best.json";  // macOS/Linux 落到 cwd 不正确
```

KongLie3D 还没存档需求但很快会有；Voyage3D 必有。

**目标位置：** `src/Runtime/Platform/UserPaths.h`：

```cpp
namespace NextPlatform::UserPaths
{
    // Windows: %APPDATA%/<appId>
    // macOS:   ~/Library/Application Support/<appId>
    // Linux:   ${XDG_DATA_HOME:-~/.local/share}/<appId>
    // Android: <internalFilesDir>/<appId>  (经 SDL_GetAndroidInternalStoragePath)
    std::filesystem::path GetUserDataDir(std::string_view appId);

    // 自动 create_directories 后返回路径
    std::filesystem::path EnsureUserFile(std::string_view appId, std::string_view relativePath);
}
```

**迁移：**
1. 实现里调用 SDL3 提供的 `SDL_GetPrefPath(org, app)`（已有 SDL 依赖，无需新代码）。
2. Brotato3D `GetBestRecordPath()` 改用 `UserPaths::EnsureUserFile("Brotato3D", "best.json")`。

**风险：** 低。SDL 已封好平台差异。

---

### P1-6 · App Modal 辅助 + 默认窗口配置

**问题：** 两个 GameInstance 构造函数完全相同模式：

```cpp
// Brotato3D + KongLie3D 各一份
config.Title = "Xxx";
config.Width = 1920; config.Height = 1080; config.ForceSDR = true;
options.Width = 1920; options.Height = 1080; options.ForceSDR = true;
```

且 modal 渲染都是 `DrawFullscreenDim + BeginPopupModal`。

**目标位置：**
- `NextGameInstanceBase` 增加 protected 帮助函数：

```cpp
class NextGameInstanceBase
{
protected:
    static void ConfigureWindow(Vulkan::WindowConfig& config, Options& options,
                                std::string_view title, int width, int height, bool forceSDR);
};
```

- `NextUI::Painter` 增加 `BeginAppModal(title, dimAlpha) -> bool` / `EndAppModal()`。

**风险：** 极低。

---

## P2 任务清单（顺手做掉）

### P2-1 · 相机屏幕震动

[Brotato3DEffectSystem.cpp:243-250](src/Application/Brotato3D/Brotato3DEffectSystem.cpp:243) 在 `OverrideRenderCamera` 里手写 sin/cos 抖动，配 `screenShakeMs_/screenShakeIntensity_/StartScreenShake` 三件套。抽成 `NextUI::FCameraShake { Push(durationMs, intensity); UpdateAndApply(camera, time); }`。Voyage3D 多半也会要。

### P2-2 · ImGui Style 预设

`Brotato3DUI` 散落 `PushStyleVar(FrameRounding, 8.0f * uiScale)`，`KongLie3DStyle::ApplyKongLieImGuiStyle` 是完整一套，`MagicaLegoStyle::ApplyStyle` 又一套。可在 `Runtime/Editor/ImGuiStylePresets.h` 提供 `Glassmorphism()` / `MaterialDark()` 等一键应用预设，新游戏可挑一个再覆盖。**优先级低**：每个游戏想要的视觉差异本来就大，强行抽象反而碍事。

### P2-3 · HitStop / 时间缩放

Brotato3D `EAppState::Hitstop` ([Brotato3DGameInstance.cpp:181](src/Application/Brotato3D/Brotato3DGameInstance.cpp:181)) 把 80ms 内的 OnTick 全跳过。可在 `NextEngine` 提供 `RequestTimeScale(scale, durationMs)` 或更直接的 `RequestHitStop(ms)`。**仅在第二个游戏也需要时再做**——目前只是 Brotato 用。

### P2-4 · 临时光池

Brotato3D `tempLightPool_`（32 个预创建 area light，按需启用）属于"游戏自带打光风格"——和效果系统耦合很重，**不建议下沉**。但 `EnsureLightMaterial(color)`（按颜色去重 emissive material）可作为 `SceneBuilder::AddDiffuseLightMaterial` 的可选缓存版本。

---

## 不要做的事情

下面这些看起来"也是重复"，但不建议下沉：

- **Game-specific audio API**（`PlayWeaponFireSfx` / `PlayHitSfx` / `PlayAttackHitSfx`）：是游戏的语义层 helper，应留在各自游戏内、底层走 P0-4 提供的 `NextAudio::PlaySfx*`。
- **`Brotato3D::FCharacterDef` / `FWaveDef` / KongLie `FPieceDef` 等数据结构**：是游戏本体，与引擎无关。
- **菜单/角色选择/结果界面 UI 渲染函数**：每个游戏布局完全不同，强行抽象只会增加复杂度。只抽底层基元（P1-3）即可。
- **`tempLightPool_` 整个池机制**：耦合 emissive 渲染管线，留在 Brotato3D。

---

## 推荐执行顺序

| 周次 | 任务 | 备注 |
| --- | --- | --- |
| 1 | P0-1 节点 helpers · P0-2 SceneBuilder · P0-5 JSON helpers | 三个低风险 PR 并行；为后续做准备 |
| 2 | P0-3 WorldToScreen · P0-4 NextAudio 扩展 · P0-6 FontLoader | P0-3 需要跑测验证，预留更长时间 |
| 3 | P0-7 RequestUiTexture · P1-1 NotificationCenter · P1-3 ImGui 绘制基元 | 完成 P0；P1 同时启动 |
| 4 | P1-2 WorldSpaceTextOverlay · P1-5 UserPaths · P1-6 ConfigureWindow | 收尾 P1 高价值项 |
| 5+ | P1-4 i18n · P2-1 屏幕震动 · 其余 P2 | 伴随 Voyage3D 起步 |

---

## 验证清单（后续 Agent 完成各任务时必做）

每个 PR 必须：

1. **Build:** `./build.bat --preset full-windows --reconfigure`（或对应平台 full-* preset）。
2. **Run sanity:**
   - `gkNextRenderer` 启动正常（核心引擎无回归）；
   - `Brotato3D` 一局打满 10 波（验证 SFX/HUD/存档正常）；
   - `KongLie3D` 一局打通三个 level（验证 Toast/拖拽/伤害弹窗）；
3. **Tests:** 只要碰到 `Runtime/Components/` 或 `Runtime/Subsystems/`，跑 `gkNextUnitTests`。
4. **LOC 校验:** 每个 P0/P1 PR 的描述里写明从 `src/Application/` 删了多少行、`src/Runtime/` 加了多少行——目标净负值。

---

## 评估收益（粗略）

| 类别 | 预估迁出 LOC | 受益游戏数 |
| --- | --- | --- |
| P0 全部完成 | ~900 LOC 从 Application 层消失 | 已有 4（Brotato/KongLie/MagicaLego/BrickPlayer）+ 未来 N |
| P1 全部完成 | ~700 LOC 进一步净减 | 同上 |
| P2 全部完成 | ~150 LOC | Brotato（部分 Voyage） |

完成 P0 + P1 后，Brotato3D 与 KongLie3D 应分别瘦身约 12-15%，并且 Voyage3D 起步可直接复用引擎层基础设施而无需再"抄一遍"。
