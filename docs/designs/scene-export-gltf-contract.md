---
title: "场景导出 glTF/GLB 契约"
category: design
status: 现行
owner: SceneExport/GltfLoader
created: 2026-07-17
last_updated: 2026-07-17
---

# 场景导出 glTF/GLB 契约

场景保存实现位于可选模块 `src/Modules/SceneExport/FSceneSaver.*`，对应读回路径位于 `src/Modules/GltfLoader/FSceneLoader.*`。本文记录 round-trip 依赖的自定义 extras；删除这些字段或改名属于格式变更，不是普通代码清理。

## 入口与基本输出

`SceneExport::SaveScene(scene, path)` 仅按大小写不敏感的 `.glb` / `.gltf` 扩展分发。GLB 内嵌 buffer/image；GLTF 写 JSON 与外部资源。空 scene、未知扩展或序列化异常返回 false 并记录错误。

当前写出：

- 非 scene-reference-internal node 的本地 TRS、parent/children 层级、name/tag/layer；
- 有 retained CPU vertices 的 drawable model，按 material section 拆 glTF primitives；
- POSITION/NORMAL/TEXCOORD_0/TANGENT 与 uint32 indices；
- PBR base color/metallic/roughness、normal/emissive texture，部分 IOR/transmission/emissive-strength；
- retained CPU texture source；WebP 使用 required `EXT_texture_webp`；
- perspective camera 与 aperture/focal distance extras；
- EnvironmentComponent、SceneReferenceComponent 和 RenderComponent 的受支持属性。

## Node extras

这些 key 由 saver 与 loader 成对维护：

| Key | 语义 |
|---|---|
| `tag` / `layer` | Node 字符串分类 |
| `visible` | RenderComponent visible |
| `castShadows` | RenderComponent shadow participation |
| `receiveGI` | RenderComponent GI participation |
| `layerMask` | RenderComponent 32-bit layer mask |
| `gkEnvironment` | 环境设置 object |
| `gkSceneReference` | `{version: 1, asset: <path>}` 外部场景引用 |

loader 为兼容旧资产，也接受 `gkSceneReference` 直接为字符串，以及环境中的 `WithSun/CamSpeed/NoSky` 旧 key。saver 同时写新的 `HasSun/HasSky/ControlSpeed` 和这些兼容 key；删除旧读路径前必须先有资产迁移证据。

`gkEnvironment` 当前写出 `SkyIdx`、`SkyIntensity`、`SkyRotation`、`SunIntensity`、`SunRotation`、`HasSky`、`HasSun`、`ControlSpeed`、`GammaCorrection`，并附兼容字段。Scene reference node 只保存引用 root；展开出的 internal nodes 被跳过，读回时 reference node 的本地 mesh/camera/skin/children 会被忽略。

Material 的 `extras.ior2` 保存引擎第二折射率；标准 glTF 没有对应字段。摄像机使用 `F-Stop` 与 `FocalDistance` extras。

## 非完整快照

SceneExport 是可编辑场景的 glTF 子集，不是任意运行时状态序列化器。当前不保证写出：

- animation tracks、skeleton/skin、physics、任意反射 component 或游戏私有状态；
- Gaussian Splat/SOG 原始数据；
- GPU-only procedural geometry；没有 CPU vertices 的 model 会跳过；
- 没有 retained CPU source 的 texture；该纹理会 warning 并从 material 中省略；
- 精确复原所有引擎 material model 与 shader 参数。

因此“Save 成功”只表示生成了文档支持范围内的 glTF/GLB，不表示 Scene 的所有 component byte-for-byte round-trip。

## 修改与验证

- 新增可持久化 component 时，优先采用有版本的 namespaced extras，并同时实现 saver、loader、缺字段默认值和旧版本兼容。
- hierarchy 使用 local TRS；不要把 world transform 写入普通 node，否则 parented scene 会二次变换。
- material section 必须保持 node RenderComponent 的 material slot 映射；超过引擎上限的输入约束仍由 loader 决定。
- texture 导出依赖 `GlobalTexturePool` retained source，不应从 GPU image 做隐式同步 readback。

验证至少做一次 `scene → GLB → reload → 再导出`，比较 node 层级/TRS、section/material、环境、scene reference 和 render flags；再用独立 glTF viewer 检查标准字段。涉及自定义 extras 的测试不能只看外部 viewer，因为它不会解释引擎语义。
