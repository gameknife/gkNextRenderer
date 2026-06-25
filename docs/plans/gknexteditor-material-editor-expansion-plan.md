---
title: "gkNextEditor MaterialEditor 扩展设计与开发计划"
category: plan
status: 待实现
owner: editor
created: 2026-06-25
last_updated: 2026-06-25
---

# gkNextEditor MaterialEditor 扩展设计与开发计划

> 状态：⚪ 待实现（设计已定，交由后续 AGENT 分阶段实现）
> 范围：`src/Application/Editor/gkNextEditor/{Panels,Nodes}`、`src/Engine/Assets/Data/Material.hpp`、`src/Engine/Assets/Core/Scene*`、`assets/shaders/common/BasicTypes.slang`、`src/Engine/Assets/{Savers,Loaders}`
> 目标读者：接手实现的 AI coding agent / 工程师
> 前置阅读：[gkNextEditor 设置面板开发计划](gknexteditor-settings-panel-plan.md)（数据驱动 UI 与 CommandHistory 复用思路一致）

---

## 1. 背景与目标

gkNextEditor 内已经有一个**基于节点（ImNodeFlow）的材质编辑器** `Material Editor` 窗口，基础框架和"打开 → 改几个值 → Apply"的最简流程已经跑通。但它目前更像是"披着节点皮的属性面板"：节点是固定生成的、字段只暴露了一小部分、改动要手动点 Apply、纹理只能看不能改、一次只能编辑一个材质，也没有预览。

本计划的目标是把它扩展成一个**功能齐全、好用**的材质编辑器，能够全功能、方便地编辑场景里的各种材质：

1. **属性完整** —— `Material` 结构体里所有有意义的字段都能在编辑器里编辑（含当前漏掉的 ShadingMode/Emissive/Opacity/IOR2/NormalScale/各贴图槽）。
2. **所见即所得** —— 改动实时（或近实时）反映到视口；带材质预览球。
3. **资产管理完整** —— 新建 / 复制 / 重命名 / 删除材质，纹理槽可指派 / 替换 / 清除。
4. **可撤销** —— 所有编辑接入 `CommandHistory`（与 PropertiesPanel 一致）。
5. **持久化一致** —— 编辑结果通过现有 glTF/GLB 往返正确保存与读回。
6. **节点图可演进** —— 先把"参数完整 + 易用"做扎实，再把节点图从"扁平参数"演进为真正可组合的程序化材质图（分阶段，后期）。

### 1.1 已拍板决策（Locked Decisions, 2026-06-25）

> 以下为已与负责人确认、**实现时必须遵守**的口径。后续 agent 不要再自行偏离；如需变更需重新确认。

| # | 决策项 | 结论 | 影响章节 |
| --- | --- | --- | --- |
| D1 | 节点图定位 | **参数视图优先**；程序化 shader graph（§4.6 Step2 / 原 Phase 5）**暂不考虑**，本计划不含该工作。 | §4.6、§5 |
| D2 | 材质预览球 | **暂不做预览渲染**；但 UI 上**预留预览窗体位置**（占位区块 + "Preview (TBD)" 提示），便于后续补。 | §4.4、§5 Phase 2/4 |
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
| `Reserverd2` / `Reserverd3` | `float` | **空闲** | 可用于新增标量参数，无需扩容结构体 |

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

### 2.3 当前节点编辑器结构

文件：`src/Application/Editor/gkNextEditor/Panels/MaterialEditorPanel.cpp`、`Nodes/Node{Material,SetFloat,SetInt}.{hpp,cpp}`。

- 全局单例图：`gNodeFlow`（`ImFlow::ImNodeFlow`）、`gMatNode`（汇聚节点弱引用）（`MaterialEditorPanel.cpp:17`）。
- `OpenMaterialEditor`（`:40`）：**每次 reset 整张图**，按 `selected_material` 当前值固定摆放：`NodeSetFloat(IOR)`、`NodeSetInt(ShadingMode)`、`NodeSetColor(Albedo)`、`NodeSetFloat(Roughness)`、`NodeSetFloat(Metalness)`，连到汇聚 `NodeMaterial`；仅当对应贴图 id≠-1 时，额外摆一个**只读** `NodeSetTexture`（Albedo/Normal/MRA）。
- `ApplyMaterial`（`:21`）：点 "Apply Material" 按钮才从 `gMatNode` 的 in-pin 读回 Albedo/Roughness/Metalness/IOR 写入材质，然后 `UpdateAllMaterials()`。
- 入口：PropertiesPanel 的 Edit 按钮（`PropertiesPanel.cpp:393-400`）与 ContentBrowser 材质双击（`ContentBrowserPanel.cpp:753-755`），均设 `ui.selected_material` + `ui.ed_material=true` 并调 `OpenMaterialEditor`。
- 渲染挂载：`EditorInterface.cpp:316` 根据 `ui.ed_material` 调 `DrawMaterialEditorPanel`。

ImNodeFlow 能力（`src/ThirdParty/ImNodeFlow/include/ImNodeFlow.h`）：`placeNodeAt<T>/addNode<T>`、`addIN/addOUT`+`behaviour` lambda、`getInVal`、`ConnectionFilter::SameType`、`getNodes()/getLinks()`、`on_selected_node()`、`rightClickPopUpContent/droppedLinkPopUpContent`。**没有内建序列化**（需自建）。

### 2.4 能力缺口清单（Gap Analysis）

| 缺口 | 现状 | 影响 |
| --- | --- | --- |
| 字段不全 | 只编辑 Albedo/Roughness/Metalness/IOR | ShadingMode、Emissive、Opacity(alpha)、IOR2、NormalScale、Emissive 贴图、AO 全部改不了 |
| ShadingMode 不回写 | 节点存在但 `ApplyMaterial` 没读它 | 改了着色模型不生效 |
| Emissive 不回写 | `NodeMaterial` 有 Emissive in-pin，但从未连线/回写 | 自发光强度无法编辑 |
| 纹理只读 | `NodeSetTexture` 只 `ImGui::Image` 显示 | 不能指派/替换/清除贴图，不能新建贴图槽 |
| 非实时 | 必须手点 Apply | 编辑体验差，调参低效 |
| 单材质 | `gNodeFlow` 全局单例、每次 reset | 不能并排比较/批量编辑 |
| 无撤销 | 直接写 `gpuMaterial_` | 与 PropertiesPanel 的 `CommandHistory` 体验不一致，误操作不可回退 |
| 无预览 | `NodeMaterial::draw` 只 `Text` 打印数值 | 看不到材质球，调参靠盲猜 |
| 无资产管理 | 没有新建/复制/重命名/删除 | 只能编辑已存在材质 |
| 右键菜单占位 | "Add Node" 只加一个名叫 "Test" 的浮点节点 | 节点目录形同虚设 |
| 图不可序列化 | 每次按材质值重建 | 程序化节点图（如有）无法保存 |

---

## 3. 设计原则

1. **材质数据是唯一真相（Material-as-source-of-truth）**。节点图是"材质字段的可视化视图"，编辑结果始终落到 `FMaterial`；不做图序列化。程序化节点图按 D1 本计划不做，故不存在"图即真相"的场景。
2. **复用既有设施**：`CommandHistory`（撤销）、`GlobalTexturePool`（纹理）、glTF saver/loader（持久化）、PropertiesPanel 的 `NextUI::Theme` 控件风格。不要另起炉灶。
3. **数据驱动**：字段 → 控件 的映射尽量集中成一张表（C++ 静态描述或 manifest），新增字段时少改 UI 代码，与设置面板计划的思路保持一致。
4. **条件可见**：按 `MaterialModel` 显示相关字段（如 Dielectric 才显示 IOR2/透射，DiffuseLight 才显示 Emissive 强度），减少噪音。
5. **GPU 同步纪律**：任何改 `Material.hpp` 字段语义/顺序的改动，必须同步 `BasicTypes.slang` 与 saver/loader，并在验收里跑往返测试。

---

## 4. 详细设计

### 4.1 材质属性完整暴露（字段 → 控件映射）

下面是编辑器应当暴露的完整字段表（**复用现有字段，不需要扩容结构体**；空闲 `Reserverd2/3` 留作后续 specular tint / anisotropy 等扩展）：

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

> Emissive 注（按 D3）：**沿用现有 `Diffuse.rgb` 承载辐射亮度的约定，不新增字段、不改落盘格式**。编辑器提供 "Emissive Color × Strength" 两个控件，内部合成 `rgb = color × strength` 写入 `Diffuse.rgb`，并保持与 saver 反解逻辑（`FSceneSaver.cpp:252-262` 的 ÷strength / ×50 约定）一致。**不**启用 `Reserverd` 显式 strength 字段。

实现建议：把上表做成一个 `struct MaterialFieldDesc { const char* group; const char* label; EWidget widget; float min,max; /*getter/setter on Material*/ }` 的静态数组，UI 遍历渲染。这样 Phase 1 之后新增字段只需加一行。

### 4.2 着色模型切换与条件 UI

- `MaterialModel` 用 Combo 暴露 6 个枚举名。切换时按上表 `条件可见` 重算可见字段集合。
- 切模型时给出**合理默认迁移**：例如切到 Dielectric 时若 IOR2 仍为初值，自动同步 `RefractionIndex2 = RefractionIndex`；切到 DiffuseLight 时把 Roughness/Metalness 控件隐藏。
- 这是修复 2.4 中"ShadingMode 不回写"的根因——把 `MaterialModel` 纳入统一写回路径。

### 4.3 纹理槽编辑

把只读的 `NodeSetTexture` 升级为可交互"贴图槽"组件（节点内 + 属性视图两处复用）：

- 缩略图：`ctx.ui.RequestImTextureId(textureId)`（`UserInterface.cpp:444`）渲染 128² 预览，沿用现有逻辑。
- 指派：
  - 从 Texture Browser 拖拽（已有 `EEditorDragPayloadType`，参考 Material 拖拽 `ContentBrowserPanel.cpp` 的 payload 模式）。
  - 弹出纹理选择器：遍历 `GlobalTexturePool::GetInstance()->TotalTextureMap()`（name→`GlobalIdx_`，`Texture.hpp`）做下拉/网格选择。
  - 从磁盘导入：`GlobalTexturePool::RequestNewTextureFileAsync(filename, hdr, srgb)`（`Texture.hpp:62`），注意 **sRGB 标记**（Albedo/Emissive=sRGB，MRA/Normal=linear）。
- 清除：设回 -1。
- MRA 通道提示：在槽位旁标注 "R=AO · G=Roughness · B=Metalness"，避免误用。

### 4.4 实时预览与即时应用

- **即时应用**：去掉"必须手点 Apply"。控件 `IsItemDeactivatedAfterEdit()` 触发一次写回 + 置 `materialDirty_`（走 `Scene` 的合批路径，而非每帧 `UpdateAllMaterials`），由 `UpdateNodes()` 在帧末统一上传，避免拖动 slider 时每帧重打包整张材质表。保留一个手动 "Apply" 仅作兜底。
- **材质预览球（按 D2：本期不做渲染，仅预留窗体位置）**：`NodeMaterial::draw` 当前只打印数值（`NodeMaterial.cpp:35-38`）。本期**不实现**离屏/CPU 预览渲染；只在材质编辑器布局里**预留一块固定尺寸的预览区**（如 128² 占位框 + "Preview (TBD)" 文案），保证后续补预览时不需要重排 UI。
  - 预留实现：在 `NodeMaterial::draw` 顶部或编辑器面板顶部画一个固定尺寸 `ImGui::Dummy/BeginChild` 占位框，边框 + 居中提示文字即可。
  - 后续（不排期）补全方向参考：方案 A 离屏渲染真实材质球（复用 `gnb shot` 离屏路径 + 固定 HDRI）、方案 B CPU 解析近似（N·L+Fresnel+roughness）。**本计划不实现这两者**，仅作存档。

### 4.5 材质资产管理

在 Material Browser（`ContentBrowserPanel.cpp:734`）与材质编辑器工具条提供：

- **新建**：`Scene::AddMaterial(FMaterial{ Material::Lambertian(...), "Material_N" })`，新建后自动选中并打开。
- **复制**：深拷贝 `FMaterial`，名字加 `_copy`。
- **重命名**：编辑 `name_`（saver 用它写 `gltfMat.name`）。
- **删除（按 D4：删除并重映射到默认材质）**：删除前扫描所有 `RenderComponent` 的材质引用（`RenderComponent::GetMaterials()`，参考 `PropertiesPanel.cpp:367`），把指向被删材质的引用**重映射到默认/兜底材质**（若场景无默认材质则先 `AddMaterial` 一个 Lambertian 兜底）；同时修正"删除后索引位移"——删除会使后续材质索引整体前移，必须同步更新所有 `RenderComponent` 中大于被删索引的引用（减 1）。建议在 `Scene` 侧实现 `RemoveMaterial(id)` 统一处理重映射 + 索引修正 + 置 `materialDirty_`，避免 UI 层手抖。删除前可弹确认提示并列出受影响物体（可选）。
- 右键菜单（`MaterialEditorPanel.cpp:123` 的 `rightClickPopUpContent`）：把占位的 "Add Node / Test" 替换成真实的节点目录（见 4.6）。

### 4.6 节点图增强（分两步走）

**Step 1（Phase 1-3，实用优先）**：节点图作为材质字段的结构化视图。
- 汇聚 `NodeMaterial` 扩成完整 in-pin 集合（补 Opacity、EmissiveStrength、IOR2、NormalScale、Emissive/MRA/Normal/Albedo 贴图全槽）。
- 输入节点类型补齐：`NodeSetColor`（已存在）、`NodeSetFloat`（已存在）、`NodeSetInt`（已存在）、新增 `NodeSetTexture`（可编辑版）、`NodeEnumShadingMode`（带名字的下拉，替代裸 int）。
- 右键节点目录：按"Inputs / Textures / Material"分类列出可添加节点。

**Step 2（程序化材质图，按 D1：本计划不含，暂不考虑）**：演进为真正可组合的材质图。
- 方向存档：运算节点（`Multiply`/`Add`/`Mix`/`Fresnel`/`UV`/`TexSample`/`Noise`）、图序列化（自定义 JSON）、图 bake 成 `Material` 字段或编译成 shader 变体。
- **本计划不实现，也不排期**；若未来要做，须单开设计文档重新立项与确认。

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
  - 扩 `NodeMaterial` in-pin 至完整集合（`NodeMaterial.cpp:12`）。
  - 重写 `ApplyMaterial`（`MaterialEditorPanel.cpp:21`）覆盖所有字段，含 `MaterialModel`、`Diffuse.a`、`RefractionIndex2`、`NormalTextureScale`、Emissive。
  - 建立 §4.1 字段描述表，做条件可见（§4.2）。
- 交付物：能完整编辑一个材质所有标量/颜色字段并即时生效。
- 验收：逐字段改值 → 视口变化正确；`gnb shot` 截图对比金属/玻璃/自发光三种模型。

### Phase 2 — 即时应用 + 撤销 + 预览位预留
- 工作：即时写回走 `materialDirty_` 合批（§4.4）；接入 `CommandHistory`（§4.7）；**预留预览窗体位置**（占位框 + "Preview (TBD)"，按 D2，不做渲染）。
- 交付物：拖 slider 实时见效、Ctrl+Z 可回退、UI 上有预览占位区。
- 验收：性能（拖动不卡）、撤销/重做正确、预览占位区布局稳定。

### Phase 3 — 纹理槽编辑 + 资产管理
- 工作：可编辑贴图槽（指派/替换/清除/导入，sRGB 正确，§4.3）；新建/复制/重命名/删除材质（§4.5）；删除按 D4 走 `Scene::RemoveMaterial` 重映射到默认材质 + 索引修正；右键节点目录（§4.6 Step1）。
- 交付物：完整的材质 CRUD + 贴图管理。
- 验收：贴图增删改在视口生效；删除被引用材质后引用正确重映射到默认材质、其余引用索引不错位；新建材质可被物体引用。

### Phase 4 — 体验打磨
- 工作：多材质编辑（去全局单例，按 materialId 维护图实例）；预设库（金属/塑料/玻璃/自发光快速套用）；属性视图与节点视图风格统一。
- 交付物：可并排编辑/比较材质。
- 验收：切换/并排多材质不串味；预设套用正确。
- 注：真实预览渲染（原方案 A/B）按 D2 **不在本期范围**；预览占位区已在 Phase 2 预留。

### Phase 5 —（不做）程序化材质节点图
- 按 D1，本计划**不含**程序化 shader graph（运算节点 + 图序列化 + bake/编译）。如未来要做须单开设计文档重新立项。

---

## 6. 受影响文件清单

| 文件 | 改动 |
| --- | --- |
| `src/Application/Editor/gkNextEditor/Panels/MaterialEditorPanel.cpp` | 重写 Apply/Open，统一写回、即时应用、右键目录、多材质 |
| `src/Application/Editor/gkNextEditor/Nodes/NodeMaterial.{hpp,cpp}` | 扩 in-pin、画预览球 |
| `src/Application/Editor/gkNextEditor/Nodes/NodeSetInt.{hpp,cpp}` | `NodeSetTexture` 升级为可编辑 + 新增 ShadingMode 枚举节点 |
| `src/Application/Editor/gkNextEditor/Nodes/NodeSetFloat.{hpp,cpp}` | 可能补 slider/range 变体 |
| `src/Application/Editor/gkNextEditor/Panels/ContentBrowserPanel.cpp` | Material Browser 增 CRUD 入口（`:734`、`:806`） |
| `src/Application/Editor/gkNextEditor/Panels/PropertiesPanel.cpp` | 编辑入口/引用展示（`:363-410`） |
| `src/Application/Editor/gkNextEditor/Core/EditorUiState.hpp` | 可能从单 `selected_material*` 扩为多材质会话状态 |
| `src/Engine/Assets/Data/Material.hpp` + `assets/shaders/common/BasicTypes.slang` | 按 D3 本期**不改**（不启用 `Reserverd`）；列此仅为提醒：若未来要动则必须成对改 |
| `src/Engine/Assets/{Savers/FSceneSaver,Loaders/FSceneLoader}.cpp` | 按 D3 本期**不改落盘格式**；仅在往返测试中作为验证对象 |
| `src/Engine/Assets/Core/Scene*.{cpp,hpp}` | 新增 `RemoveMaterial(id)`（含 D4 重映射+索引修正）/`DuplicateMaterial`/`UpdateMaterial(id)` 接口 |

---

## 7. 验收与测试

- **单元测试**（`gkNextUnitTests`，Catch2）：材质 CRUD 接口、字段写回、glTF 往返（编辑→存→读→字段相等，重点 emissive strength 缩放、IOR/IOR2、alpha→BLEND）。
- **视觉验证**：`gnb shot --target gkNextEditor --ui` 截图核对面板；`gnb shot --scene <带各类材质的场景>` 核对金属/玻璃/自发光/PBR 渲染正确。
- **性能**：拖动 slider 时确认走合批上传（不每帧全表 `UpdateAllMaterials`）。
- **高风险项用子 agent 复核**：`Material.hpp`/`BasicTypes.slang` 对齐与字段顺序的同步改动，建议独立 review。

---

## 8. 风险与未决问题

1. **结构体对齐同步**：`Material.hpp` 与 `BasicTypes.slang` 必须严格同序同对齐。按 D3 本期**不动结构体**，风险较低；除非未来启用 `Reserverd2/3`，届时尤其小心 16 字节对齐。
2. **Emissive strength**：按 D3 沿用 `Diffuse.rgb` 反解约定，不改落盘格式；风险点仅在编辑器 color×strength 合成与 saver 反解（×50/÷50）保持一致，需往返单测兜底。
3. **删除材质的引用一致性（D4 重点风险）**：`RenderComponent` 用**索引**引用材质，删除后必须同时做两件事——(a) 指向被删材质的引用重映射到默认材质；(b) 所有大于被删索引的引用整体 −1 修正。建议集中在 `Scene::RemoveMaterial(id)` 内完成，UI 层不要各自实现。必须有覆盖"删除中间某个材质后其余物体仍指向正确材质"的单测。
4. **多材质会话**：去全局 `gNodeFlow` 单例（`MaterialEditorPanel.cpp:17`）会触及窗口/焦点逻辑，放 Phase 4。
5. **Isotropic 模型**：体积散射参数当前无专属字段，编辑器可先按 Lambertian 类似处理，专属参数待引擎侧明确语义后再补。
6. **预览**：按 D2 本期不做预览渲染，仅预留窗体位；无相关渲染风险。后续补预览时再评估离屏管线接入成本。

---

## 9. 给接手 AGENT 的执行提示

- 先读本文件 §2 与引用的源码行，再动手；改前用 `./gnb build gkNextEditor` 确认基线可编译。
- 严格按 Phase 顺序：Phase 1 不接预览/纹理，先把"字段全 + 写回对"做扎实，它是后续一切的地基。
- 任何动 `Material.hpp`/shader/saver 的改动，配套往返单测后再提交。
- UI 风格沿用 `NextUI::Theme`（参考 `PropertiesPanel.cpp`），不要引入新的视觉规范。
- 与[设置面板计划](gknexteditor-settings-panel-plan.md)的数据驱动思路保持一致：字段描述集中成表，少硬编码。
