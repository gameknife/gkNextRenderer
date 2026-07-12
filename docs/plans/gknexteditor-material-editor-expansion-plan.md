---
title: "gkNextEditor MaterialEditor 扩展设计与开发计划"
category: plan
status: 实施中
owner: editor
created: 2026-06-25
last_updated: 2026-06-29
---

# gkNextEditor MaterialEditor 扩展设计与开发计划

> 状态：🟡 实施中（新版参数式材质编辑器已作为当前方向）
> 范围：`src/Application/Editor/gkNextEditor/Panels`、`src/Engine/Assets/Data/Material.hpp`、`src/Engine/Assets/Core/Scene*`、`assets/shaders/common/BasicTypes.slang`、`src/Engine/Assets/{Savers,Loaders}`、`src/Engine/Rendering/Preview`
> 目标读者：接手实现的 AI coding agent / 工程师
> 前置阅读：[gkNextEditor 设置面板开发计划](gknexteditor-settings-panel-plan.md)（数据驱动 UI 与 CommandHistory 复用思路一致）

---

## 1. 背景与目标

gkNextEditor 的 `Material Editor` 已明确转向**参数式材质编辑器**。材质系统当前只需要高质量参数调节、纹理槽管理、撤销与实时预览；节点图不再是需求，也不应继续作为编辑器依赖或后续演进方向。

本计划的目标是把它扩展成一个**功能齐全、好用**的材质编辑器，能够全功能、方便地编辑场景里的各种材质：

1. **属性完整** —— `Material` 结构体里所有有意义的字段都能在编辑器里编辑（含当前漏掉的 ShadingMode/Emissive/Opacity/IOR2/NormalScale/各贴图槽）。
2. **所见即所得** —— 改动实时（或近实时）反映到视口；带材质预览球。
3. **资产管理完整** —— 新建 / 复制 / 重命名 / 删除材质，纹理槽可指派 / 替换 / 清除。
4. **可撤销** —— 所有编辑接入 `CommandHistory`（与 PropertiesPanel 一致）。
5. **持久化一致** —— 编辑结果通过现有 glTF/GLB 往返正确保存与读回。
6. **去节点化** —— 移除材质编辑器中的 node graph 与 ImNodeFlow 依赖，避免用节点图承载本质上是结构化参数表的编辑工作。

### 1.1 已拍板决策（Locked Decisions, 2026-06-25）

> 以下为已与负责人确认、**实现时必须遵守**的口径。后续 agent 不要再自行偏离；如需变更需重新确认。

| # | 决策项 | 结论 | 影响章节 |
| --- | --- | --- | --- |
| D1 | 节点图定位 | **彻底去掉 node graph**；材质编辑器只保留参数式编辑、纹理槽、资产管理与实时预览。不引入程序化 shader graph。 | §4.6、§5 |
| D2 | 材质预览球 | **实现实时预览渲染**；基于现有独立 `Scene` + 离屏 `RenderView`/独立视口能力，在材质编辑器中显示实时更新的材质球。 | §4.4、§5 Phase 2/4 |
| D3 | 自发光强度存储 | **沿用现有 `Diffuse.rgb` 反解约定**，不改数据结构/落盘格式；编辑器 color×strength 合成写入 `Diffuse.rgb`。 | §4.1、§4.8 |
| D4 | 删除被引用材质 | **允许删除并把引用重映射到默认/兜底材质**（非禁止删除）。 | §4.5 |

---

## 2. 现状分析

### 2.1 材质数据模型

CPU 侧 `Assets::Material`（`src/Engine/Assets/Data/Material.hpp:8`），`alignas(16)`，与 GPU 侧 `assets/shaders/common/BasicTypes.slang:405` 的 `Material` 一一对应（字段顺序、对齐必须保持同步）：

| 字段 | 类型 | 含义 | 备注 |
| --- | --- | --- | --- |
| `Diffuse` | `vec4` | 基础色 rgb + **alpha=不透明度** | DiffuseLight 模式下 rgb 当作辐射亮度；alpha<1 → 透明（saver 会写 `BLEND`） |
| `DiffuseTextureId` | `int32` | 基础色贴图 | -1 = 无 |
| `MRATextureId` | `int32` | MRA 贴图 | G=Roughness、B=Metalness（见 loader 注释 `FSceneLoader.cpp:605`）；R 约定为 AO |
| `NormalTextureId` | `int32` | 法线贴图 | -1 = 无 |
| `EmissiveTextureId` | `int32` | 自发光贴图 | -1 = 无 |
| `Fuzziness` | `float` | 粗糙度（roughness） | 命名历史遗留，语义=roughness |
| `RefractionIndex` | `float` | IOR | 默认 1.46；saver 阈值判断写 `KHR_materials_ior` |
| `MaterialModel` | `enum uint32` | 着色模型 | 见下表 |
| `Metalness` | `float` | 金属度 | |
| `RefractionIndex2` | `float` | 折射用第二 IOR | saver 写入 `extras.ior2` |
| `NormalTextureScale` | `float` | 法线强度 | saver/loader 走 `normalTexture.scale` |
| `Reserved2` / `Reserved3` | `float` | **空闲** | 可用于新增标量参数，无需扩容结构体 |

着色模型枚举（`Material.hpp:40` / `BasicTypes.slang:365`）：

| 枚举 | 值 | 语义（来自 `PathTracingRenderer.slang` / `Shading.slang`） |
| --- | --- | --- |
| `Lambertian` | 0 | 纯漫反射 |
| `Metallic` | 1 | 金属，specAlbedo=albedo（`PathTracingRenderer.slang:555`） |
| `Dielectric` | 2 | 介质/玻璃，走 `RefractionIndex/2` + 透射（`:225`、saver 写 `KHR_materials_transmission`） |
| `Isotropic` | 3 | 各向同性体积散射 |
| `DiffuseLight` | 4 | 自发光（Diffuse 当辐射亮度，`:318`） |
| `Mixture` | 5 | 金属-粗糙度 PBR 混合（glTF 导入默认） |

### 2.2 运行管线

- 场景持有 `std::vector<FMaterial> materials_`（`FMaterial = { Material gpuMaterial_; std::string name_; }`，`Material.hpp:80`）。访问：`Scene::Materials()`（`Scene.hpp:85`）、`GetMaterial`（`Scene.cpp:865`）、`AddMaterial`（`Scene.cpp:874`，会置 `materialDirty_`）。
- 上传：`Scene::UpdateAllMaterials()`（`Scene.Update.cpp:238`）把所有 `gpuMaterial_` 重新打包进 dynamic buffer（`GPU_SCENE_DYNAMIC_MATERIALS_OFFSET`），并 `SetProgressiveRendering(false)` 触发重收敛。
- 脏标记：`materialDirty_` 在 `Scene::UpdateNodes()`（`Scene.Update.cpp:280`）里被消费 → 调 `UpdateAllMaterials()`。**注意：当前编辑器是直接同步调用 `UpdateAllMaterials()`，没走 `materialDirty_` 这条延迟合批路径**（见 2.3）。
- 持久化（glTF/GLB 往返）：
  - Saver `FSceneSaver.cpp:240-320`：DiffuseLight→`emissiveFactor`+`KHR_materials_emissive_strength`；baseColor（gamma 平方）；metallic/roughness factor；MRA/normal/emissive 贴图；`normalTexture.scale`；alpha<1→`BLEND`；IOR→`KHR_materials_ior`；Dielectric→`KHR_materials_transmission`；ior2→`extras`。
  - Loader `FSceneLoader.cpp:592-675`：上述逆向 + 一组启发式（金属度/粗糙度阈值、是否纯自发光）来推断 `MaterialModel`。
  - **结论**：数据模型的"全部能力"已经在 saver/loader 里出现过，编辑器只是没把它们全暴露出来。这张映射表是 §4.1 的事实依据。

### 2.3 当前编辑器结构

文件：`src/Application/Editor/gkNextEditor/Panels/MaterialEditorPanel.cpp`。

- `OpenMaterialEditor` 只负责同步当前材质选择并聚焦 `Material Editor` 窗口，不再创建或 reset 节点图。
- `DrawMaterialEditorPanel` 直接绘制材质预览、工具条、参数表和纹理槽；控件编辑即时写回 `FMaterial` 并通过 `Scene::MarkMaterialsDirty()` 触发 GPU 侧合批同步。
- 入口：PropertiesPanel 的 Edit 按钮与 ContentBrowser 材质双击，均设 `ui.selected_material` + `ui.ed_material=true` 并调 `OpenMaterialEditor`。
- 渲染挂载：`EditorInterface.cpp` 根据 `ui.ed_material` 调 `DrawMaterialEditorPanel`。

ImNodeFlow 已不再是材质编辑器依赖。`src/Application/Editor/gkNextEditor/Nodes/Node{Material,SetFloat,SetInt}.*` 这类仅服务旧材质节点图的文件应删除；`src/CMakeLists.txt` 中不再 `add_subdirectory(ThirdParty/ImNodeFlow)` 或把 `ImNodeFlow` 链接进 `gkNextEditor`；`src/ThirdParty/ImNodeFlow` 目录也已物理移除。

### 2.4 能力缺口清单（Gap Analysis）

| 缺口 | 现状 | 影响 |
| --- | --- | --- |
| 字段不全 | 只编辑 Albedo/Roughness/Metalness/IOR | ShadingMode、Emissive、Opacity(alpha)、IOR2、NormalScale、Emissive 贴图、AO 全部改不了 |
| ShadingMode 不完整 | 旧流程没有统一字段写回 | 改了着色模型不生效 |
| Emissive 不完整 | 旧流程未提供 color × strength 编辑 | 自发光强度无法编辑 |
| 纹理只读 | 旧流程只显示贴图缩略图 | 不能指派/替换/清除贴图 |
| 非实时 | 旧流程必须手点 Apply | 编辑体验差，调参低效 |
| 参数标签缺失 | 控件 label 被隐藏或被可用宽度吞掉 | 只看得到数值滑块，难以判断字段含义 |
| 无撤销 | 直接写 `gpuMaterial_` | 与 PropertiesPanel 的 `CommandHistory` 体验不一致，误操作不可回退 |
| 无预览 | 旧窗口只显示参数 | 看不到材质球，调参靠盲猜 |
| 无资产管理 | 没有新建/复制/重命名/删除 | 只能编辑已存在材质 |

---

## 3. 设计原则

1. **材质数据是唯一真相（Material-as-source-of-truth）**。编辑器是 `FMaterial` 的参数视图，编辑结果始终落到 `FMaterial`；不做节点图、不做图序列化，也不把程序化 shader graph 纳入本计划。
2. **复用既有设施**：`CommandHistory`（撤销）、`GlobalTexturePool`（纹理）、glTF saver/loader（持久化）、PropertiesPanel 的 `NextUI::Theme` 控件风格。不要另起炉灶。
3. **数据驱动**：字段 → 控件 的映射尽量集中成一张表（C++ 静态描述或 manifest），新增字段时少改 UI 代码，与设置面板计划的思路保持一致。
4. **条件可见**：按 `MaterialModel` 显示相关字段（如 Dielectric 才显示 IOR2/透射，DiffuseLight 才显示 Emissive 强度），减少噪音。
5. **GPU 同步纪律**：任何改 `Material.hpp` 字段语义/顺序的改动，必须同步 `BasicTypes.slang` 与 saver/loader，并在验收里跑往返测试。

---

## 4. 详细设计

### 4.1 材质属性完整暴露（字段 → 控件映射）

下面是编辑器应当暴露的完整字段表（**复用现有字段，不需要扩容结构体**；空闲 `Reserved2/3` 留作后续 specular tint / anisotropy 等扩展）：

| UI 分组 | 字段 | 控件 | 范围/步长 | 写回目标 | 条件可见 |
| --- | --- | --- | --- | --- | --- |
| Surface | Albedo | ColorEdit3 | sRGB | `Diffuse.rgb` | 非 DiffuseLight |
| Surface | Opacity | SliderFloat | 0–1 | `Diffuse.a` | 非 DiffuseLight |
| Surface | Metalness | SliderFloat | 0–1 | `Metalness` | Mixture/Metallic |
| Surface | Roughness | SliderFloat | 0–1 | `Fuzziness` | 非 Dielectric/DiffuseLight |
| Shading | Material Model | Combo（6 项） | 枚举 | `MaterialModel` | 始终 |
| Refraction | IOR | SliderFloat | 1.0–2.5 | `RefractionIndex` | Dielectric/Mixture |
| Refraction | IOR (back) | SliderFloat | 1.0–2.5 | `RefractionIndex2` | Dielectric |
| Emission | Emissive Color | ColorEdit3(HDR) | — | `Diffuse.rgb` | DiffuseLight |
| Emission | Emissive Strength | DragFloat | ≥0 | 见注 | DiffuseLight |
| Normal | Normal Scale | SliderFloat | 0–2 | `NormalTextureScale` | 有法线贴图时 |
| Textures | Albedo / MRA / Normal / Emissive | 贴图槽控件（见 4.3） | — | 对应 `*TextureId` | 始终 |

> Emissive 注（按 D3）：**沿用现有 `Diffuse.rgb` 承载辐射亮度的约定，不新增字段、不改落盘格式**。编辑器提供 "Emissive Color × Strength" 两个控件，内部合成 `rgb = color × strength` 写入 `Diffuse.rgb`，并保持与 saver 反解逻辑（`FSceneSaver.cpp:252-262` 的 ÷strength / ×50 约定）一致。**不**启用 `Reserved` 显式 strength 字段。

实现建议：把上表做成一个 `struct MaterialFieldDesc { const char* group; const char* label; EWidget widget; float min,max; /*getter/setter on Material*/ }` 的静态数组，UI 遍历渲染。这样 Phase 1 之后新增字段只需加一行。

### 4.2 着色模型切换与条件 UI

- `MaterialModel` 用 Combo 暴露 6 个枚举名。切换时按上表 `条件可见` 重算可见字段集合。
- 切模型时给出**合理默认迁移**：例如切到 Dielectric 时若 IOR2 仍为初值，自动同步 `RefractionIndex2 = RefractionIndex`；切到 DiffuseLight 时把 Roughness/Metalness 控件隐藏。
- 这是修复 2.4 中"ShadingMode 不回写"的根因——把 `MaterialModel` 纳入统一写回路径。

### 4.3 纹理槽编辑

把只读贴图显示升级为可交互"贴图槽"组件：

- 缩略图：`ctx.ui.RequestImTextureId(textureId)`（`UserInterface.cpp:444`）渲染 128² 预览，沿用现有逻辑。
- 指派：
  - 从 Texture Browser 拖拽（已有 `EEditorDragPayloadType`，参考 Material 拖拽 `ContentBrowserPanel.cpp` 的 payload 模式）。
  - 弹出纹理选择器：遍历 `GlobalTexturePool::GetInstance()->TotalTextureMap()`（name→`GlobalIdx_`，`Texture.hpp`）做下拉/网格选择。
- 从磁盘导入：当前可使用 `GlobalTexturePool::LoadTexture(filename, srgb)` 或补齐 `RequestNewTextureFileAsync` 的实现后再走异步路径；注意 **sRGB 标记**（Albedo/Emissive=sRGB，MRA/Normal=linear）。
- 清除：设回 -1。
- MRA 通道提示：在槽位旁标注 "R=AO · G=Roughness · B=Metalness"，避免误用。

### 4.4 实时预览与即时应用

- **即时应用**：去掉"必须手点 Apply"。控件 `IsItemDeactivatedAfterEdit()` 触发一次写回 + 置 `materialDirty_`（走 `Scene` 的合批路径，而非每帧 `UpdateAllMaterials`），由 `UpdateNodes()` 在帧末统一上传，避免拖动 slider 时每帧重打包整张材质表。
- **材质实时预览球（按 D2：本期实现）**：现在引擎已有独立 `Scene`、离屏 `RenderView` 和独立视口输出到 sampled texture 的能力，因此本计划在材质编辑器内实现真正的实时预览。
  - 预览资源：新增一个 editor/preview 层的 `MaterialPreviewRenderer`（命名可调整），复用 `RenderViewServices`、`RenderViewManager`、`RenderViewResourceFactory` 和 `BuildViewCameraUbo` 的现有路径；不要把材质编辑器业务塞回 `VulkanBaseRenderer`。
  - 预览场景：维护一个独立小 `Assets::Scene`，只包含材质球模型（球体为主，可选平面/背景）、固定相机和稳定灯光/环境。可复用 `AssetThumbnailRenderer::EnsureMaterialThumbnailScene()` 的思路，但实时预览需要持久 view 和稳定 sample slot，不走一次性 thumbnail hash/cache 队列。
  - 数据同步：每次选中材质变化或字段写回时，把主场景 `FMaterial` 拷贝到预览场景 `Materials()[0]`，调用预览场景的 `UpdateAllMaterials()`/`UpdateNodes()` 并让 preview view `InvalidateTemporalHistory()`；避免重建预览 scene 和几何。
  - 调度：材质编辑器窗口可见时启用 preview view，窗口隐藏时禁用；预览区域尺寸变化时更新 render extent。默认使用 `ERT_SoftwareModernNoAmbient` 这类轻量稳定管线，缺失时回退当前 renderer。
  - UI：在材质编辑器顶部或右侧绘制固定最小尺寸的预览窗（建议 192²–256²，随 panel 宽度可放大），用 `ctx.ui.RequestImTextureIdRaw(sampleSlot)` + `ImGui::Image/AddImage` 显示。加载首帧前显示 "Initializing preview..."，不要再使用 "Preview (TBD)"。
  - 交互：Phase 2 至少支持实时材质更新；预览球 orbit/zoom、背景切换、曝光等作为 Phase 4 打磨项，不阻塞首版实时预览。

### 4.5 材质资产管理

在 Material Browser（`ContentBrowserPanel.cpp:734`）与材质编辑器工具条提供：

- **新建**：`Scene::AddMaterial(FMaterial{ Material::Lambertian(...), "Material_N" })`，新建后自动选中并打开。
- **复制**：深拷贝 `FMaterial`，名字加 `_copy`。
- **重命名**：编辑 `name_`（saver 用它写 `gltfMat.name`）。
- **删除（按 D4：删除并重映射到默认材质）**：删除前扫描所有 `RenderComponent` 的材质引用（`RenderComponent::GetMaterials()`，参考 `PropertiesPanel.cpp:367`），把指向被删材质的引用**重映射到默认/兜底材质**（若场景无默认材质则先 `AddMaterial` 一个 Lambertian 兜底）；同时修正"删除后索引位移"——删除会使后续材质索引整体前移，必须同步更新所有 `RenderComponent` 中大于被删索引的引用（减 1）。建议在 `Scene` 侧实现 `RemoveMaterial(id)` 统一处理重映射 + 索引修正 + 置 `materialDirty_`，避免 UI 层手抖。删除前可弹确认提示并列出受影响物体（可选）。

### 4.6 去节点图与参数 UI 整理

- 删除材质编辑器对 `ImNodeFlow`、`ImFlow::*`、`NodeMaterial`、`NodeSetFloat`、`NodeSetInt`、`NodeSetTexture` 的所有引用。
- 从 `src/CMakeLists.txt` 移除 `ThirdParty/ImNodeFlow` 子目录构建与 `gkNextEditor` 链接依赖，并删除 `src/ThirdParty/ImNodeFlow` 目录。
- 删除仅服务旧材质节点图的 `src/Application/Editor/gkNextEditor/Nodes/Node{Material,SetFloat,SetInt}.{hpp,cpp}`。
- 参数行采用显式 label + 隐藏 ImGui ID（例如先 `TextUnformatted("Roughness")`，控件使用 `"##roughness"`），保证字段名始终显示，控件宽度变化不会吞掉 label。
- 用固定 label 列宽或两列表格组织参数；紧凑窗口下优先保留 label 可读性，再收缩输入控件。

### 4.7 撤销集成

- 所有写回包成命令推入 `ctx.engine.GetCommandHistory()`（`CommandHistory.hpp:17`，PropertiesPanel 已用 `PropertyWidgets::DrawComponentProperties(component, &ctx.engine.GetCommandHistory(), ...)` 这个模式，`PropertiesPanel.cpp:448`）。
- 推荐实现一个通用 `MaterialEditCommand`（记录 materialId + 旧值/新值的 `Material` 快照或单字段 diff），`Execute/Undo` 都走"写 `gpuMaterial_` + 置 `materialDirty_`"。拖动连续编辑应做合并（同字段同材质的连续命令合并为一条）。

### 4.8 持久化一致性

- 编辑器不直接碰 glTF；只改 `FMaterial`，落盘交给现有 `FSceneSaver`。
- 验收必须覆盖往返：编辑 → 保存 GLB → 重载 → 字段一致（尤其 emissive strength 的 ×50 / ÷50 缩放约定 `FSceneSaver.cpp:259` 与 loader 的逆向，别改坏）。
- 按 D3，本期**不新增落盘字段**，往返格式不变，编辑器只需保证写入 `Diffuse.rgb` 后往返自洽。

---

## 5. 分阶段开发计划

> 每个 Phase 都是可独立交付、可编译验证的增量。建议构建命令：改 Editor 用 `./gnb build gkNextEditor`；改 `Material.hpp`/shader/Scene 用 `./gnb build gkNextRenderer gkNextUnitTests`（见 AGENTS.md）。

### Phase 1 — 属性完整化 + 统一写回（地基）
- 目标：6 大着色模型 + 全部有效字段都能改且生效；修掉 ShadingMode/Emissive 不回写。
- 工作：
  - 在 `MaterialEditorPanel.cpp` 内直接绘制参数表，覆盖所有字段，含 `MaterialModel`、`Diffuse.a`、`RefractionIndex2`、`NormalTextureScale`、Emissive。
  - 参数控件使用显式字段名 label + 隐藏 ImGui ID，避免只显示数值滑块。
  - 建立 §4.1 字段描述表或等价的集中渲染逻辑，做条件可见（§4.2）。
- 交付物：能完整编辑一个材质所有标量/颜色字段并即时生效。
- 验收：逐字段改值 → 视口变化正确；`gnb shot` 截图对比金属/玻璃/自发光三种模型。

### Phase 2 — 即时应用 + 撤销 + 实时预览
- 工作：即时写回走 `materialDirty_` 合批（§4.4）；接入 `CommandHistory`（§4.7）；实现独立 preview scene + 离屏 `RenderView` 的材质实时预览球（§4.4）。
- 交付物：拖 slider 主视口与预览球实时见效、Ctrl+Z 可回退、材质编辑器内显示真实渲染的预览球。
- 验收：性能（拖动不卡）、撤销/重做正确、预览窗口 resize 稳定；切换材质后预览不串材质、不复用错误历史；关闭材质编辑器后 preview view 停止调度。

### Phase 3 — 纹理槽编辑 + 资产管理
- 工作：可编辑贴图槽（指派/替换/清除/导入，sRGB 正确，§4.3）；新建/复制/重命名/删除材质（§4.5）；删除按 D4 走 `Scene::RemoveMaterial` 重映射到默认材质 + 索引修正；完成旧节点图/ImNodeFlow 依赖清理（§4.6）。
- 交付物：完整的材质 CRUD + 贴图管理。
- 验收：贴图增删改在视口生效；删除被引用材质后引用正确重映射到默认材质、其余引用索引不错位；新建材质可被物体引用。

### Phase 4 — 体验打磨
- 工作：预设库（金属/塑料/玻璃/自发光快速套用）；参数分组与控件风格统一；补预览球 orbit/zoom、背景/灯光/曝光切换等体验项。
- 交付物：可并排编辑/比较材质。
- 验收：切换/并排多材质不串味；预设套用正确；预览交互不影响主编辑器相机/主视口输入。

### Phase 5 —（不做）程序化材质节点图
- 按 D1，本计划**不含**程序化 shader graph（运算节点 + 图序列化 + bake/编译），也不保留 ImNodeFlow 作为材质编辑器未来扩展点。如未来要做须单开设计文档重新立项。

---

## 6. 受影响文件清单

| 文件 | 改动 |
| --- | --- |
| `src/Application/Editor/gkNextEditor/Panels/MaterialEditorPanel.cpp` | 参数式材质 UI、统一写回、即时应用、CRUD、纹理槽、实时预览 |
| `src/Engine/Rendering/Preview/{RenderViewServices,AssetThumbnailRenderer}.*` / 新 `MaterialPreviewRenderer.*` | 复用独立 scene + offscreen RenderView 能力实现实时材质预览；不要把业务逻辑塞回 `VulkanBaseRenderer` |
| `src/Application/Editor/gkNextEditor/Nodes/Node{Material,SetInt,SetFloat}.{hpp,cpp}` | 删除旧材质节点图实现 |
| `src/CMakeLists.txt` / `src/ThirdParty/ImNodeFlow` | 移除 `ThirdParty/ImNodeFlow` 构建与 `gkNextEditor` 链接依赖，并删除第三方目录 |
| `src/Application/Editor/gkNextEditor/Panels/ContentBrowserPanel.cpp` | Material Browser 增 CRUD 入口（`:734`、`:806`） |
| `src/Application/Editor/gkNextEditor/Panels/PropertiesPanel.cpp` | 编辑入口/引用展示（`:363-410`） |
| `src/Application/Editor/gkNextEditor/Core/EditorUiState.hpp` | 记录材质选择与材质预览窗口/交互状态 |
| `src/Engine/Assets/Data/Material.hpp` + `assets/shaders/common/BasicTypes.slang` | 按 D3 本期**不改**（不启用 `Reserved`）；列此仅为提醒：若未来要动则必须成对改 |
| `src/Engine/Assets/{Savers/FSceneSaver,Loaders/FSceneLoader}.cpp` | 按 D3 本期**不改落盘格式**；仅在往返测试中作为验证对象 |
| `src/Engine/Assets/Core/Scene*.{cpp,hpp}` | 新增 `RemoveMaterial(id)`（含 D4 重映射+索引修正）/`DuplicateMaterial`/`UpdateMaterial(id)` 接口 |

---

## 7. 验收与测试

- **单元测试**（`gkNextUnitTests`，Catch2）：材质 CRUD 接口、字段写回、glTF 往返（编辑→存→读→字段相等，重点 emissive strength 缩放、IOR/IOR2、alpha→BLEND）。
- **视觉验证**：`gnb shot --target gkNextEditor --ui` 截图核对面板与材质预览球；`gnb shot --scene <带各类材质的场景>` 核对金属/玻璃/自发光/PBR 渲染正确。
- **性能**：拖动 slider 时确认走合批上传（不每帧全表 `UpdateAllMaterials`）。
- **预览验证**：切换材质、拖动颜色/roughness/metalness/opacity/emissive、撤销/重做时，预览球与主视口同步；关闭材质编辑器后不再调度 preview view；resize 时 sample slot 与 ImGui 纹理不失效。
- **高风险项用子 agent 复核**：`Material.hpp`/`BasicTypes.slang` 对齐与字段顺序的同步改动，建议独立 review。

---

## 8. 风险与未决问题

1. **结构体对齐同步**：`Material.hpp` 与 `BasicTypes.slang` 必须严格同序同对齐。按 D3 本期**不动结构体**，风险较低；除非未来启用 `Reserved2/3`，届时尤其小心 16 字节对齐。
2. **Emissive strength**：按 D3 沿用 `Diffuse.rgb` 反解约定，不改落盘格式；风险点仅在编辑器 color×strength 合成与 saver 反解（×50/÷50）保持一致，需往返单测兜底。
3. **删除材质的引用一致性（D4 重点风险）**：`RenderComponent` 用**索引**引用材质，删除后必须同时做两件事——(a) 指向被删材质的引用重映射到默认材质；(b) 所有大于被删索引的引用整体 −1 修正。建议集中在 `Scene::RemoveMaterial(id)` 内完成，UI 层不要各自实现。必须有覆盖"删除中间某个材质后其余物体仍指向正确材质"的单测。
4. **第三方目录清理**：材质编辑器不再构建、链接、引用 ImNodeFlow，且 `src/ThirdParty/ImNodeFlow` 已物理删除。未来如重新引入节点图能力，必须单开设计文档重新立项。
5. **Isotropic 模型**：体积散射参数当前无专属字段，编辑器可先按 Lambertian 类似处理，专属参数待引擎侧明确语义后再补。
6. **预览 RenderView 生命周期**：实时预览依赖独立 `Scene` + offscreen `RenderView`。风险点是 sample slot 生命周期、swapchain resize 后资源重建、窗口关闭后仍调度、以及材质切换时历史帧串味。预览服务必须集中管理 enable/disable、extent、sample slot 和 `InvalidateTemporalHistory()`，并复用现有 `RenderViewServices`/thumbnail 资源路径。

---

## 9. 给接手 AGENT 的执行提示

- 先读本文件 §2 与引用的源码行，再动手；改前用 `./gnb build gkNextEditor` 确认基线可编译。
- 严格按 Phase 顺序：Phase 1 不接预览/纹理，先把"字段全 + 写回对"做扎实，它是后续一切的地基。
- 任何动 `Material.hpp`/shader/saver 的改动，配套往返单测后再提交。
- UI 风格沿用 `NextUI::Theme`（参考 `PropertiesPanel.cpp`），不要引入新的视觉规范。
- 与[设置面板计划](gknexteditor-settings-panel-plan.md)的数据驱动思路保持一致：字段描述集中成表，少硬编码。
