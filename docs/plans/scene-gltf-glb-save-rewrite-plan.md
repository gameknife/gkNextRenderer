# Scene → glTF/GLB 保存流程重写开发计划

> 状态：待开发（设计 + 任务拆解，供后续 AGENT 接手实现）
> 创建日期：2026-06-24
> 范围：`src/Engine/Assets/Savers/`、`src/Engine/Assets/Loaders/FSceneLoader.cpp`、`Node`/`RenderComponent` 元数据、`GlobalTexturePool` 纹理回存、编辑器保存入口、单测/视觉验证
> 测试主场景：`assets/scad/office.scad`（纯过程场景，几何/层级/材质/layer-tag 往返）+ 一个带纹理的 glb（纹理 webp/raw 往返）

---

## 1. 背景与目标

当前 `FSceneSaver`（`src/Engine/Assets/Savers/FSceneSaver.cpp`）是第一版"能跑通最小路径"的实现，做得不完善，存在多处会导致**保存后再加载丢信息 / 渲染不一致**的问题。本次目标是**重写 save 流程**，达到以下能力：

1. **几何 + 层级 + 相机 + 材质**能够 1:1 往返（save → load 后场景视觉一致）。
2. **多 section（多材质）网格**正确拆分为多 primitive，材质归属不丢失（office.scad 的核心诉求）。
3. 通过 glTF 的 `extras`（userdata）存储 gkNextEditor 规划中的 **layer、tag** 以及节点可见性/阴影/GI 等运行时元数据，并能读回。
4. 支持把纹理**编码为 WebP 或 RAW**，写入 GLB（embed 到 buffer），并能被 `FSceneLoader` 成功读取还原。
5. 用 `assets/scad/office.scad` 验证整体存储流程；纹理往返用单独的带纹理资源验证。

**非目标（本期不做，仅预留接口）**：骨骼/蒙皮/动画的保存、Gaussian Splat 保存、Draco 几何压缩、KTX2/BasisU 压缩纹理写出（读取已支持，写出留作后续）。

---

## 2. 现状调研

### 2.1 调用链与文件

```
编辑器 File > Save Scene
  TitleBarOverlay.cpp:100        ctx.scene.Save("saved_scene.glb")
  Scene.cpp:912  Scene::Save     → 按扩展名分发
  Scene.cpp:930  SaveAsGLB       → FSceneSaver::SaveGLBScene
  FSceneSaver.cpp:28             SerializeScene → tinygltf WriteGltfSceneToFile

加载（对照）
  FSceneLoader.cpp:296 LoadGLTFScene  → tinygltf 解析 → ParseGltfNode / 材质 / mesh / 动画
  纹理：GlobalTexturePool::LoadTexture（Texture.cpp:52/75）→ RequestNewTextureMemAsync
```

### 2.2 现有 Saver 已实现部分

`FSceneSaver::SerializeScene`（`FSceneSaver.cpp:124`）依次写：
- `SerializeMeshes`：每个 `Assets::Model` → 1 个 mesh、**1 个 primitive**，写 POSITION/NORMAL/TEXCOORD_0/TANGENT + 索引（uint32）。
- `SerializeMaterials`：写 baseColor（线性=Diffuse²）、metallic、roughness、emissive、`KHR_materials_ior`、`KHR_materials_transmission`。
- `SerializeCameras`：写透视相机 yfov/near/far。
- `SerializeNodes`：写 TRS、父子关系、相机关联、mesh 关联（`renderComp->GetModelId()`）；建默认 scene 收集根节点。

### 2.3 缺陷清单（必须在重写中解决）

| # | 缺陷 | 位置 | 影响 |
|---|------|------|------|
| D1 | **多 section/多材质丢失**：一个 Model 含多个 section（顶点 `MaterialIndex`），却被写成单 primitive 且 `primitive.material = materials[0]` | `FSceneSaver.cpp:368-381` | office.scad 等多色场景保存后**所有面同色**，材质全错 |
| D2 | **节点↔mesh 索引假设过强**：`gltfNode.mesh = GetModelId()`，直接拿 modelId 当 gltf mesh index | `FSceneSaver.cpp:190` | 多场景 append（modelIdx 偏移）后索引错位；与 loader 的 `meshId = node.mesh + modelIdx` 不对称 |
| D3 | **纹理完全没保存**：`CopyTextures` 是 TODO，材质纹理引用全部丢失 | `FSceneSaver.cpp:413,465` | 任何带贴图场景保存后贴图丢失 |
| D4 | **环境/相机 extras 不回写**：loader 读取 `SkyIdx/SkyIntensity/SkyRotation/SunIntensity/SunRotation/WithSun/NoSky/CamSpeed`（scene.extras）、`F-Stop/FocalDistance`（camera.extras）、`ior2`（material.extras）、`arealight`（node.extras），但 saver **一个都不写** | loader `FSceneLoader.cpp:800-846,1038-1045,647,197` | 天空/太阳/景深/面光源往返丢失 |
| D5 | **相机命名 hack 脆弱**：相机名是 `"<idx> <nodeName>"`，saver 靠 split 空格反查 node | `FSceneSaver.cpp:146-157` | 节点名含空格即错位 |
| D6 | **节点元数据不保存**：可见性、castShadows、receiveGI、layerMask、tag、layer 均无处落盘 | 全局 | 编辑器状态无法持久化 |
| D7 | **gamma 往返风险**：save 用 `Diffuse²` 反推 baseColor，load 用 `sqrt`；纯发光材质 load 把 emissive 写进 Diffuse，save 时判定分支与 load 不对称 | save `:402`, load `:598-611` | 发光体/高饱和色往返漂移 |
| D8 | **空场景/无 RenderComponent 兜底为 material 0**，可能越界或错绑 | `FSceneSaver.cpp:378` | 无材质 mesh 崩溃/错色 |
| D9 | GLTF（非 GLB）路径 `embedBuffers=false` 但没写出 `.bin`/贴图 sidecar 的落盘逻辑校验 | `FSceneSaver.cpp:97-101` | .gltf 导出可能产出无法重新加载的文件 |

### 2.4 layer/tag 现状：**只有 UI 占位，没有数据模型**

- `OutlinerPanel.cpp:369 DrawLayersPanel`：硬编码 5 个 layer（Default/Gameplay/Props/Colliders/Lighting），纯绘制，无数据绑定。
- `PropertiesPanel.cpp:249-257`：`static int tagIndex/layerIndex` + 两个 Combo（Tag: Untagged/Player/Environment/Interactable；Layer: 同上 5 个），**static 变量、不绑定任何 Node**。
- `Node`（`Node.h`）**没有 layer/tag 字段**；`RenderComponent` 有 `layerMask_`(0xFFFFFFFF) 但语义是渲染层掩码，不是编辑器 layer。
- `Node::RegisterReflection`（`Node.cpp:15`）只反射了 Name/InstanceId/TRS。

> 结论：本期需要**新增 layer/tag 数据模型**（见 §5），不是简单"读现有字段写盘"。

### 2.5 纹理系统现状（决定纹理回存方案）

- 加载：`RequestNewTextureMemAsync`（`Texture.cpp:288`）接收压缩字节（webp/ktx/png…），按 mime 解码：
  - `image/webp` → `WebPDecodeRGBA`（`Texture.cpp:412`，已链接 `libwebp`）。
  - `image/ktx*` → KTX2 transcode BC7（`WITH_KTX2`）。
  - 其它 → `stbi_load_from_memory` → **压成 KTX2/BC7 落 cache**（`Texture.cpp:454-509`）后上传。
  - **源压缩字节 `copiedData` 在上传后 `delete[]`**（`Texture.cpp:571`），GPU 上是 BC7 块，**没有保留 CPU 端 RGBA 或源字节**。
- `TextureImage`（`TextureImage.hpp`）**不暴露 width/height/format，也没有回读 API**。
- WebP **编码**（`webp/encode.h` / `WebPEncodeRGBA`）当前未使用，但 `libwebp` 已是依赖，编码能力可用。
- glTF 读取侧已支持 `EXT_texture_webp`（`FSceneLoader.cpp:386-392,438-444`）。

> 结论：要把纹理写回 glb，**不能依赖 GPU 回读 BC7**（需先解 BC7 再编码，重且有损）。推荐在**加载时保留每张纹理的 CPU 源数据**（原始压缩字节或解码后的 RGBA），保存时直接复用（见 §4.6）。

### 2.6 office.scad 适配性

`assets/scad/office.scad` 是**纯过程场景**：几何由 OpenSCAD 求值生成，材质**只有颜色**（`color([...])` → Lambertian / 半透明 Dielectric），**没有任何纹理**。SCAD loader 按颜色分桶生成**多 section Model**（见 `AGENT_GUIDE/SCADLoader.md`）。

因此 office.scad 能充分验证：**几何、节点层级、多 section→多材质、材质颜色/透明、相机、layer/tag extras** 往返。它**不能**验证纹理往返——纹理 webp/raw 往返需另选一个带贴图的 glb（见 §6.2）。

---

## 3. 关键约束与数据流

```
保存:  Scene(nodes/models/materials/cameras/env + 新增 layer/tag)
        → FSceneSaver 重建 tinygltf::Model
            · Model.section → 多 primitive，primitive.material = RenderComponent.materialIdx_[section]
            · 节点 extras: layer/tag/visible/castShadows/receiveGI/layerMask
            · scene.extras: Sky/Sun/Cam 环境
            · material.extras: ior2 等；扩展 ior/transmission
            · images: webp/raw 编码字节 → bufferView（GLB embed）
        → WriteGltfSceneToFile

加载:  既有 FSceneLoader（已支持 extras 读取 + EXT_texture_webp）
        → 需补：raw 纹理 mime 解析；新增节点 extras（layer/tag/...）读回
```

**对称性原则**：saver 写出的每一类信息，loader 必须有对应读回路径；新增 extras key 要同时改两侧，并写进往返单测。

---

## 4. 设计方案：重写 Saver

建议保留 `FSceneSaver` 类名与 `SaveGLBScene/SaveGLTFScene` 公共入口（`Scene::Save` 不变），内部重写。可拆出 `FSceneSaverContext`（持有 tinygltf::Model、name→index 映射、共享 buffer）。

### 4.1 节点与层级（修 D2/D5/D6）

- 遍历 `scene.Nodes()` 建立 `Node* → gltfNodeIndex` 映射（顺序即写出顺序）。
- TRS 直接写 translation / rotation(`[x,y,z,w]`) / scale（保持现状，已正确）。
- **mesh 关联**：不要直接用 `GetModelId()` 当 gltf index。建立 `modelId → gltfMeshIndex` 映射（§4.2 生成 mesh 时填充），节点写 `gltfNode.mesh = meshIndexMap[modelId]`。
- **相机关联**：不再依赖名字 split；在 §4.4 生成相机时建立 `Node* → cameraIndex`（或在节点上直接判定其 camera），写 `gltfNode.camera`。
- **节点 extras**（见 §5.3 schema）：visible、castShadows、receiveGI、layerMask、layer、tag、（可选）locked。
- 根节点（`GetParent()==nullptr`）进 `scene.nodes`。

### 4.2 网格与多 section（修 D1/D8）—— **本期最关键**

SCAD/glTF 的一个 `Assets::Model` 可能含多个 section（`Vertex::MaterialIndex` = section 序号，`Model::SectionCount()`）。loader 侧每个 gltf primitive → 一个 section。**保存必须反向：按 section 把顶点/索引切回多 primitive。**

实现要点：
1. 对每个 Model，遍历其 `CPUIndices()`，按三角形所属 section（取该三角形顶点的 `MaterialIndex`）分组到 `sectionIndices[section]`。
2. 每个 section 生成一个 primitive：
   - 复用同一份 POSITION/NORMAL/TEXCOORD_0/TANGENT accessor（整模型一份顶点 buffer 即可），primitive 只换 `indices` accessor。或按 section 重切顶点（实现更简单、文件略大）——**推荐复用顶点、仅分组索引**。
   - `primitive.material = renderComp->GetMaterials()[section]`（全局材质索引，需映射到该 glb 内的局部材质索引，见 §4.3）。
3. 找该 Model 对应的 `RenderComponent`：建 `modelId → RenderComponent*` 映射（现有 `FSceneSaver.cpp:231-243` 的思路，但要保留**整段 materialIdx_ 数组**而非只取 [0]）。
4. 无 RenderComponent / 无材质：写一个默认材质（在 §4.3 保证至少有一个 fallback material），不要硬编 index 0 越界。

> 验收：office.scad 保存后重新加载，颜色分块与原场景一致（gnb shot 对比）。

### 4.3 材质（修 D7）

- 全局 `scene.Materials()` 去重写出；建立 `globalMaterialIndex → gltfMaterialIndex` 映射（本期通常 1:1，但要为多场景 append 去重留位）。
- baseColor / metallic / roughness / emissive：保持现状，但**与 loader 严格对称**：
  - loader：`Diffuse = sqrt(baseColor)`；save：`baseColor = Diffuse * Diffuse`。保持。
  - 纯发光（`MaterialModel==DiffuseLight`）：loader 把 `emissiveColor * emissiveStrength` 塞进 `Diffuse`。save 时应写 `emissiveFactor` 并配 `KHR_materials_emissive_strength`，使 baseColor 不被发光值污染。需要明确：**发光材质的 Diffuse 语义在引擎内是"发光色×强度"**——保存时要还原 emissiveFactor + strength，loader 读回后再合成，保证往返一致（写一个发光材质单测固定该约定）。
- 扩展：`KHR_materials_ior`（≠1.46 时写）、`KHR_materials_transmission`（Dielectric 写 1.0）、`material.extras["ior2"]`（RefractionIndex2 ≠ RefractionIndex 时写）。
- 纹理引用：见 §4.6。
- `extensionsUsed` 需登记用到的扩展（ior/transmission/emissive_strength/EXT_texture_webp）。

### 4.4 相机与环境（修 D4）

- 相机：写 perspective yfov/znear/zfar（现状）；补 `camera.extras["F-Stop"] = 0.2/Aperture`（Aperture>0 时）、`camera.extras["FocalDistance"]`。
- 环境（写 `scene.extras`，对应 loader `FSceneLoader.cpp:800-846`）：
  `SkyIdx, SkyIntensity, SkyRotation, SunIntensity, SunRotation, WithSun(=HasSun?1:0), CamSpeed(=ControlSpeed)`；`HasSky==false` 时写 `NoSky=1`。
  来源：`scene.GetEnvironmentStrings()` / `EnvironmentSetting`（`Model.hpp:30`）。
- 面光源：对带 `arealight` 语义的节点写 `node.extras["arealight"]=1`（若引擎侧能识别哪些 Node 是面光源；当前 loader 从 extras 反推 LightObject，保存侧需要一个 Node→是否 arealight 的判定——本期可作为可选项，若无法判定则跳过并记 TODO）。

### 4.5 顶点属性

- 必写：POSITION（带 min/max）、NORMAL、TEXCOORD_0、TANGENT(vec4)、indices(uint32)。
- 跳过（本期）：JOINTS_0/WEIGHTS_0、morph、animation。`Model` 虽存 weights/joints，但骨骼往返非本期目标，**预留**但不写（写单测确认无骨骼场景不受影响）。

### 4.6 纹理：WebP / RAW 编码写入 GLB 并读回（修 D3）

#### 4.6.1 CPU 源数据保留（前置改造，必需）

在 `GlobalTexturePool` 增加"保存所需的 CPU 源"留存。两种粒度，**推荐 (A)**：

- **(A) 保留原始压缩源字节 + mime**（最省、无损、最简单）：`RequestNewTextureMemAsync` 在 `delete[] copiedData` 前，把 `(texname → {bytes, mime, srgb})` 存入一个 `std::unordered_map`（仅在编辑器/需要保存的构建里开启，受开关控制以免常驻内存翻倍）。保存时若纹理要求保持原格式即可直接 embed；要求转 webp/raw 时先解码再编码。
- **(B) 保留解码后的 RGBA + w/h**：占内存更多，但编辑器若会运行时改像素则需要它。

新增接口建议：
```cpp
// GlobalTexturePool 内
struct FTextureCpuSource { std::vector<uint8_t> bytes; std::string mime; int w=0,h=0; bool srgb=false; bool isRawRGBA=false; };
const FTextureCpuSource* GetCpuSource(uint32_t textureIdx) const;   // 没有则 nullptr
void SetCpuSourceRetention(bool enable);                            // 默认编辑器开、运行时关
```

> 明确风险：**过程生成 / 运行时新建且无源数据的纹理**无法用 (A) 导出；需要 (B) 或 GPU 回读。本期以 (A) 为主，(B)/回读列为后续。office.scad 无纹理，不受影响。

#### 4.6.2 编码与写入

保存时对每张被材质引用的纹理：
1. 取 `FTextureCpuSource`（先解码为 RGBA：webp→`WebPDecodeRGBA`，png/jpg→stbi，已是 RGBA 则直接用）。
2. 按导出选项编码：
   - **WebP**：`WebPEncodeRGBA`（有损 quality 可配）或 `WebPEncodeLosslessRGBA`（无损）。生成 `tinygltf::Image`，`mimeType="image/webp"`，写入 bufferView（GLB embed）。`texture.extensions["EXT_texture_webp"].source = imageIndex`，并登记 `extensionsUsed += "EXT_texture_webp"`（按 glTF 规范，若没有 PNG/JPG fallback 还需登记到 `extensionsRequired`）。
   - **RAW**：把 RGBA8 原始像素写入 bufferView，`tinygltf::Image{width,height,component=4,bits=8,pixel_type=UBYTE}`，`mimeType="image/raw"`（**自定义**，非标准 glTF）。通过自定义 extension 或 `image.extras` 记录 `{width,height,format:"RGBA8",srgb}` 以便读回。
3. `material.pbrMetallicRoughness.baseColorTexture.index` 等指向新建 texture。
4. 维护 `engineTextureIdx → gltfTextureIndex` 映射，材质各通道（baseColor/MRA/normal/emissive）按 `FMaterial` 里的 `DiffuseTextureId/MRATextureId/NormalTextureId/EmissiveTextureId` 写引用。

> 通道映射注意：loader 把 metallicRoughness 纹理读进 `MRATextureId`，occlusion 期望打包进 MRA 的 R 通道（`FSceneLoader.cpp:567-578`）。保存时按此约定写 `metallicRoughnessTexture`，不要单独写 occlusionTexture。

#### 4.6.3 读回（改 FSceneLoader）

- WebP：已支持（`EXT_texture_webp` + mime `image/webp` 解码路径已在 `Texture.cpp:412`）。**确认 embed（bufferView）路径**也走通（现 loader `:424` 用 `model.buffers[0]` + bufferView，多 buffer 时需用 `bufferViews[].buffer`，顺手修正）。
- RAW：在 `RequestNewTextureMemAsync` 增加 `image/raw` 分支：直接当作 RGBA8 上传（读 `image.extras` 的 w/h/srgb，或自定义 extension）。loader 侧把 raw 字节 + 尺寸传入。

#### 4.6.4 导出选项

`SaveGLBScene` 增加参数（或 `FSceneSaveOptions` 结构）：
```cpp
struct FSceneSaveOptions {
    enum class ETextureExport { KeepOriginal, WebPLossy, WebPLossless, Raw } textureExport = ETextureExport::WebPLossless;
    int  webpQuality = 90;          // 仅 WebPLossy
    bool embedTextures = true;      // GLB 必 true
    bool writeEditorMetadata = true;// layer/tag/visible 等 extras
};
```

---

## 5. layer / tag 数据模型与 extras schema

### 5.1 数据模型（新增）

在 `Assets::Node` 增加编辑器元数据字段（轻量，直接挂 Node，避免新建 component 的反射成本；也可考虑独立 `MetadataComponent`——**推荐挂 Node**，因 layer/tag 是节点级通用属性）：

```cpp
// Node.h 私有成员 + 访问器
std::string tag_   = "Untagged";   // 单值标签
std::string layer_ = "Default";     // 所属 layer 名
// 访问器：GetTag/SetTag/GetLayer/SetLayer
```

- layer/tag 用**字符串**而非枚举：编辑器目前是固定列表，但字符串更利于扩展、且 extras 天然是字符串/数字；编辑器 Combo 仍可用固定列表选择，自定义值允许透传。
- 可见性等已有载体：`RenderComponent`（visible_/castShadows_/receiveGI_/layerMask_），保存时从 component 取。

### 5.2 反射 + 编辑器绑定

- `Node::RegisterReflection`（`Node.cpp:15`）补 `.data<&Node::SetTag,&Node::GetTag>("Tag")` 与 `Layer`，加 `PropertyMeta`（Editable，分类 "Metadata"），使 PropertyPanel 自动出 UI（参考 `AGENT_GUIDE/ReflectionSystem.md`）。
- 把 `PropertiesPanel.cpp:249-257` 的 static Combo 改为读写选中 Node 的 `tag_/layer_`（通过反射或直接访问器 + CommandHistory 以支持 undo）。
- `OutlinerPanel.cpp:369` 的 layers 列表改为可显隐/筛选（与 Node.layer 联动）——本期可只做"显示真实 layer 列表 + 按 layer 过滤大纲"，显隐做成可选增强。

### 5.3 glTF extras schema（保存约定，写文档锚定）

| 载体 | key | 类型 | 来源 | loader 读回 |
|------|-----|------|------|-------------|
| node.extras | `tag` | string | `Node::GetTag()` | 新增 |
| node.extras | `layer` | string | `Node::GetLayer()` | 新增 |
| node.extras | `visible` | bool/int | `RenderComponent::GetVisible()` | 新增 |
| node.extras | `castShadows` | bool/int | `RenderComponent::GetCastShadows()` | 新增 |
| node.extras | `receiveGI` | bool/int | `RenderComponent::GetReceiveGI()` | 新增 |
| node.extras | `layerMask` | int | `RenderComponent::GetLayerMask()` | 新增 |
| node.extras | `arealight` | int | （可选）面光源标记 | 已有读 |
| scene.extras | `SkyIdx/SkyIntensity/SkyRotation/SunIntensity/SunRotation/WithSun/CamSpeed/NoSky` | num/int | `EnvironmentSetting` | 已有读 |
| material.extras | `ior2` | num | `RefractionIndex2` | 已有读 |
| camera.extras | `F-Stop/FocalDistance` | num | `Camera` | 已有读 |

> tinygltf 的 `extras` 是 `tinygltf::Value`，写出/读回均支持 Object/数值/字符串/bool。新增 key 必须**同时**改 saver（写）与 `FSceneLoader`（读）并加进往返单测。

---

## 6. 测试与验证方案

### 6.1 office.scad 往返（主验收，无纹理）

1. `gnb run gkNextRenderer --load-scene assets/scad/office.scad`，或在编辑器加载。
2. 设置若干节点的 tag/layer/visible，触发 Save 到 `out/.../office_roundtrip.glb`。
3. 重新加载该 glb，断言：
   - 节点数 / 层级 / 每节点 TRS 一致（容差 1e-4）。
   - 材质数一致、每 section→材质映射一致（颜色/透明）。
   - tag/layer/visible/castShadows/receiveGI 读回一致。
   - 相机与环境（Sky/Sun）一致。
4. 视觉：`gnb shot --target ScadStudio --scene assets/scad/office.scad` 与加载 roundtrip.glb 的 `gnb shot` 截图肉眼/像素对比（参考 AGENTS.md "Agent Visual Validation"，截图落 `out/build/<preset>/screenshots/agent_validation.jpg`）。

### 6.2 纹理往返（webp / raw，单独资源）

office.scad 无纹理，**另选带贴图的 glb**（如 `gkNextVisualTest` 用过的资源或 Khronos sample，经 `Packager`/optional.pak）：
- 加载 → 以 `WebPLossless` 保存 → 重载，比较纹理像素（解码后 RGBA 逐像素，允许有损模式下 PSNR 阈值）。
- 同资源以 `Raw` 保存 → 重载，**应逐像素无损**。
- 断言材质各通道（baseColor/MRA/normal/emissive）纹理引用正确、srgb 标记正确。

### 6.3 单元测试（Catch2，`gkNextUnitTests`）

- 新增 `src/Tests/Test_SceneSaver.cpp`，tag `[Unit][SceneSaver]`：
  - 构造内存 Scene（含 2 section 的 Model + 2 材质）→ save 到临时 glb（内存或 temp 文件）→ 用 `FSceneLoader` 重载 → 断言上述各项。
  - 发光材质往返一致性（锚定 §4.3 约定）。
  - extras（tag/layer）往返。
  - 纹理：合成一张小 RGBA（如 4×4）→ raw 往返逐像素相等；webp 无损往返相等。
- 复用/参考既有 `src/Tests/Test_GltfSkinning.cpp`、`Test_SceneList.cpp` 的 fixture 风格；需真实 GPU 的部分用 `EngineTestFixture`（`--hidden-window`）。

### 6.4 构建与回归

- 改 Engine 层：`./gnb build gkNextRenderer gkNextUnitTests`。
- 改编辑器：`./gnb build gkNextEditor`。
- 运行：日志出现 `uploaded scene [...] to gpu` 视为加载成功。

---

## 7. 分阶段任务拆解（建议里程碑）

**M1 — 几何/材质/层级正确往返（无纹理、无 extras）**
- [ ] 重写 `FSceneSaver`：节点/相机映射表（修 D2/D5），多 section→多 primitive + 正确材质（修 D1/D8）。
- [ ] 材质往返对称（修 D7），扩展 ior/transmission/ior2。
- [ ] office.scad 往返单测（几何+材质+层级+相机）通过；gnb shot 视觉一致。

**M2 — extras：环境 + 节点运行时元数据**
- [ ] saver 写 scene.extras（Sky/Sun/Cam，修 D4）、camera.extras、node.extras（visible/castShadows/receiveGI/layerMask）。
- [ ] loader 补节点 extras 读回。
- [ ] 往返单测覆盖 extras。

**M3 — layer/tag 数据模型 + 编辑器**
- [ ] `Node` 增 tag_/layer_ + 访问器 + 反射（§5.1/5.2）。
- [ ] `PropertiesPanel` Combo 绑定选中 Node（接 CommandHistory 支持 undo）。
- [ ] saver/loader 读写 `node.extras.tag/layer`；office.scad 往返断言含 tag/layer。
- [ ] （可选）OutlinerPanel layer 列表真实化 + 过滤。

**M4 — 纹理 webp/raw 写入与读回**
- [ ] `GlobalTexturePool` CPU 源留存（§4.6.1）+ 开关。
- [ ] saver 纹理编码 webp/raw → bufferView + texture/扩展登记（§4.6.2）。
- [ ] loader 补 `image/raw` 分支；校正 embed webp 的多 buffer 引用（§4.6.3）。
- [ ] `FSceneSaveOptions` 导出选项接入 `Scene::Save`/编辑器菜单。
- [ ] 带贴图资源 webp/raw 往返单测（§6.2）通过。

**M5 — 收尾**
- [ ] .gltf（非 GLB）导出落盘校验（修 D9）或显式只支持 GLB 并在 UI 限制。
- [ ] 文档：更新 `AGENT_GUIDE/`（新增 `SceneSaver.md`）+ 本计划勾选。
- [ ] 全量回归：`gnb build`（仅在改动面广时）+ `gkNextUnitTests`。

---

## 8. 风险与未决问题

1. **无源纹理无法导出**（过程/运行时纹理）：本期 (A) 方案不覆盖；需 (B) RGBA 留存或 GPU 回读（解 BC7），列后续。
2. **RAW 非标准 glTF**：`image/raw` + 自定义 extras 仅本引擎可读，第三方工具（Blender 等）无法识别；需在文档标注，并保证 `extensionsUsed/Required` 不污染标准查看器对几何的读取。
3. **发光材质往返语义**：Diffuse 同时承载发光色×强度，必须用单测锚定 save/load 约定，否则反复保存会漂移（D7）。
4. **多场景 append 的索引偏移**：loader 用 `modelIdx/materialOffset` 累加；saver 是整 Scene 导出，需保证导出的是"合并后的单场景"，索引从 0 重排。
5. **CPU 源留存内存翻倍**：编辑器开、运行时关；大场景注意峰值内存。
6. **面光源（arealight）反向标记**：能否从 Node 判定是面光源待确认，否则面光源往返本期不保证。
7. **office.scad 节点名作为锚点**（OfficeMap 解析，见 `office.scad` 头注释/StudioSim plan）：保存时**不得改写节点名**，否则破坏 OfficeMap。

---

## 9. 验收标准

- office.scad：save → load 后，节点层级/TRS/多 section 材质颜色/相机/环境/tag/layer 全部往返一致；gnb shot 视觉无可见差异。
- 带贴图资源：raw 往返逐像素无损；webp 无损往返逐像素一致；纹理通道引用与 srgb 正确。
- `gkNextUnitTests "[Unit][SceneSaver]"` 全绿。
- `./gnb build gkNextRenderer gkNextUnitTests gkNextEditor` 通过；运行加载 roundtrip.glb 出现 `uploaded scene [...] to gpu`。
- 不引入对 `ThirdParty/` 的修改；新增依赖（无）/扩展登记规范。

---

## 10. 涉及文件清单（实现参考）

**重写/主改**
- `src/Engine/Assets/Savers/FSceneSaver.h` / `FSceneSaver.cpp` —— 核心重写。
- `src/Engine/Assets/Loaders/FSceneLoader.cpp` —— 补 node extras 读回、`image/raw` 解析、embed webp 多 buffer 修正。
- `src/Engine/Assets/GPU/Texture.hpp` / `Texture.cpp` —— CPU 源留存 + `GetCpuSource` + raw 上传分支。
- `src/Engine/Assets/Core/Node.h` / `Node.cpp` —— tag_/layer_ + 访问器 + 反射。
- `src/Engine/Assets/Core/Scene.hpp` / `Scene.cpp` —— `Save` 增 `FSceneSaveOptions` 重载（保持旧签名兼容）。

**编辑器**
- `src/Application/Editor/gkNextEditor/Panels/PropertiesPanel.cpp` —— tag/layer Combo 绑定 Node。
- `src/Application/Editor/gkNextEditor/Panels/OutlinerPanel.cpp` —— （可选）真实 layer 列表/过滤。
- `src/Application/Editor/gkNextEditor/Overlays/TitleBarOverlay.cpp` —— Save 菜单接导出选项（webp/raw）。

**测试 / 文档**
- `src/Tests/Test_SceneSaver.cpp`（新增）+ `src/CMakeLists.txt` 收录。
- `assets/configs/visual_test.json` —— （可选）加 roundtrip 场景。
- `AGENT_GUIDE/SceneSaver.md`（新增，落实 extras schema + 纹理导出约定）。

**构建注意**
- `FSceneSaver.cpp` 已在 `src/CMakeLists.txt:462` 标记 `SKIP_UNITY_BUILD_INCLUSION`（因 tinygltf STB 实现冲突），新增/拆分文件若引入 tinygltf/STB/webp 实现宏，需同样处理 unity 排除。
- WebP 编码用 `#include <webp/encode.h>`（`libwebp` 已是 `vcpkg.json` 依赖）。
