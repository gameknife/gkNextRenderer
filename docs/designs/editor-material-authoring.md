---
title: "Editor 材质创作架构"
category: design
status: 现行
owner: editor/assets/rendering
created: 2026-07-17
last_updated: 2026-07-17
---

# Editor 材质创作架构

gkNextEditor 的 Material Editor 是 `Assets::FMaterial` 的参数式编辑器，不是 shader graph。旧 ImNodeFlow 材质节点路线已经移除；除非产品重新提出可序列化程序化材质图的明确需求，否则不要恢复节点、图 ABI 或第二份材质数据模型。

## 数据与格式事实

- 场景中的唯一真相是 `Assets::Scene::Materials()` 所持有的 `FMaterial{name_, gpuMaterial_}`。
- CPU `Assets::Material` 与 `assets/shaders/common/BasicTypes.slang` 的 GPU `Material` 必须保持字段顺序、类型和对齐一致。增加或复用字段时要同步两端并检查 loader/saver。
- `Diffuse.a` 是 opacity；`DiffuseLight` 模式下 `Diffuse.rgb` 直接保存辐射亮度。编辑器把最大 RGB 分量解释为 emissive strength，再以归一化颜色 × strength 写回同一字段，没有独立的强度字段。
- MRA 贴图约定为 R=AO、G=roughness、B=metalness。Albedo/Emissive 按 sRGB 载入，MRA/Normal 按 linear 载入。
- 当前六种模型是 Lambertian、Metallic、Dielectric、Isotropic、DiffuseLight、Mixture；精确枚举和值以 `Material.hpp` 为准。

## 编辑与撤销路径

`Panels/MaterialEditorPanel.cpp` 直接编辑选中的 `FMaterial`，并调用 `Scene::MarkMaterialsDirty()`；`Scene::UpdateNodes()` 在后续更新中合批上传全部材质。编辑同时停止 progressive accumulation，避免继续显示旧收敛结果。不要在 slider 每次变化时同步重建场景或直接绕过 scene dirty 路径。

连续控件编辑在 item 激活时保存 before snapshot，在结束编辑时提交 `MaterialEditCommand`。命令按 scene、material id 和字段名合并，Execute/Undo 都重新写入 `FMaterial` 并标记 dirty。参数、名称、纹理槽和 preset 走这条路径。

当前 New、Duplicate、Delete 直接调用 Scene API，不在 `CommandHistory` 中；不要声称所有材质资产操作都可撤销。若要补 CRUD undo，命令必须同时保存材质表、所有 `RenderComponent` 引用和选择状态，不能只恢复被删元素。

## 资产操作不变量

- 新建使用 Lambertian 默认材质；复制由 `Scene::DuplicateMaterial()` 完成并追加 `_copy`。
- 删除必须经过 `Scene::RemoveMaterial()`，不能由 UI 直接 `erase`。它会把指向被删项的 `RenderComponent` 引用改到兜底项，并修正被删除索引之后的所有引用。
- 场景只剩一个材质时，删除会把该 slot 原位重置为 `DefaultMaterial`，从而保证引用仍有效。
- 材质索引是场景局部索引，不是稳定资产 ID。保存跨帧选择时要在 vector 变化后刷新 id/pointer；不要长期缓存元素地址。

## 实时预览

实时材质球和 Content Browser 缩略图共同由 `EditorPreview::AssetThumbnails()` 注册的 `AssetThumbnailRenderer` provider 管理，但生命周期不同：材质球是面板可见期间持续更新的 preview，缩略图是按需 transient 工作。

材质球使用独立小 Scene、独立 RenderView/RT bank、固定环境和 sampled output。材质或相机变化后，provider 更新 preview Scene、失效相关 view 状态并重新调度。默认使用 `SoftwareModernNoAmbient`，因为它是当前允许 `SceneOverride` 而不依赖主场景 TLAS/Ambient 准备的 renderer；不要改成依赖 scene-global 资源的路径后仍假定 override 安全。

preview 的 post-render callback 立即把 `RT_DENOISED` 复制到 sampled image。该 callback 在主 command buffer 录制线程执行，不是后台渲染任务。RenderView 的通用约束见 [多视图架构](multi-viewport-renderview-design.md)。

## 保存边界

Material Editor 只修改场景数据，不自行写 glTF。保存由 SceneExport 模块完成，读取由 GltfLoader 完成，具体契约见 [场景导出 glTF/GLB 契约](scene-export-gltf-contract.md)。loader 会根据 glTF 属性推断部分 `MaterialModel`，且并非所有运行时语义都有无损标准字段；修改 emissive、transmission、IOR2、alpha 或纹理通道时必须做实际保存/重载验证，不能只看编辑器内预览。

## 修改检查表

修改材质系统时至少核对：CPU/GPU layout、默认构造值、所有 renderer 的读取、编辑器条件显示、CommandHistory、scene dirty/upload、preview SceneOverride、纹理色彩空间、SceneExport/GltfLoader 往返，以及删除后所有组件引用。
