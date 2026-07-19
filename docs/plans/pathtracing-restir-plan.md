---
title: "PathTracing ReSTIR 开发计划"
category: plan
status: 未开始（本计划不构成自动授权，需用户任务或活动 spec 显式触发）
owner: engine/rendering
created: 2026-07-19
last_updated: 2026-07-19
---

# PathTracing ReSTIR 开发计划

设计边界、数据契约与取舍见 [ReSTIR 设计方案](../designs/pathtracing-restir-design.md)；本文只记录执行顺序与验收口径。里程碑必须按序执行，每个里程碑独立可验证、可回退（`r.restir.enable=false` 全程保证旧路径逐位不变）。

## 里程碑总览

| 里程碑 | 内容 | 交付判据 |
|---|---|---|
| M0 | 资源与管道地基 | debug 热力图可见，SHARC 零回归 |
| M1 | RIS-only 初始候选 | 多灯场景等 spp 噪声下降，progressive 收敛与旧路径一致 |
| M2 | 时域蓄水池复用 | 静态场景噪声再降一档，运动无 lag/ghost 回归 |
| M3 | 空间复用 + 独立 shade pass | 边缘无可见 bias，perf 达预算 |
| M4 | 收尾集成 | 全量 visual test 通过，默认值决策，文档转正 |
| M5（可选探索） | ReSTIR GI 预研 | 单独授权，不随 M1–M4 自动执行 |

---

## M0 资源与管道地基

**改动：**

- `assets/shaders/common/BasicTypes.slang` + `src/Engine/Assets/` 对应 C++ 侧：`SharcResources` 扩展为 `FPathTracingExtras`（追加 `RestirReservoirPing/Pong/Parameters` 三个地址），`static_assert` 尺寸；`SharcIsAvailable()` 改为判 `HashEntries != 0` 字段，新增 `RestirIsAvailable()` 判 `RestirParameters != 0`。
- `src/Engine/Rendering/PathTracing/PathTracingRenderer.{hpp,cpp}`：仿照 `FSharcState` 增加 `FRestirState`（extent 变化重建、双缓冲 ping/pong 翻转、`vkCmdFillBuffer` 清零、barrier 辅助函数）；无论 SHARC 开关，只要任一特性启用就构建 extras 表并挂 `ReservedAddress0`。
- `src/Engine/Runtime/Config/UserSettings.hpp` + `EngineCVars.cpp`：`r.restir.*` 全套 CVar（见设计 §3.7）。
- 新增 `assets/shaders/common/Restir.slang`：`FRestirReservoir` 结构、pack/unpack、流式 RIS 合并原语（`Update`/`Merge`/`FinalizeW`）、`FRestirRuntimeParameters` 读取。
- 主 dispatch 尾部接 debugMode 1/2（写一个常量 reservoir 再读回渲染热力图，验证双缓冲与 barrier 正确）。

**验证：**

- `./gnb.sh build gkNextRenderer gkNextUnitTests` 通过。
- `gnb shot --scene CornellBox.proc` + `r.restir.debugMode=1`：热力图铺满屏。
- SHARC 回归：`r.sharc` 开启下 `gnb shot` 与改动前截图肉眼一致（`SharcIsAvailable` 语义改动是本里程碑最大风险点）。
- `r.restir.enable=false` 下 `gkNextVisualTest` baseline 无 diff。

**风险：** extras 表尺寸/对齐改动会让旧 SPIR-V 与新 C++ 结构错位——shader 与 C++ 必须同一 commit 落地。

## M1 RIS-only 初始候选（无偏基线）

**改动：**

- `assets/shaders/common/Restir.slang`：候选生成（CDF 选灯 + 灯面 uv，源 pdf 换算）、目标函数 p̂（复用 `DirectIlluminate` 面光几何项，提炼为共享函数避免两处漂移）、初始 RIS 循环 + 胜者可见性 + 内联 shading。
- `PathTracingRenderer.slang`：`Render` 尾部 primary 直接光改为"太阳项照旧 + 面光项按 `RestirIsAvailable()` 分流"；`Core.PathTracing.comp` 与 `Core.SharcQuery.comp` 两个入口同时生效。
- `src/Application/Common/DemoScenes.cpp`：新增 `ManyLightsShowcase.proc`（8×8 灯阵 + 遮挡体），注册为验证场景。

**验证：**

- 等 spp 单帧对比：`ManyLightsShowcase.proc`、`CornellBox.proc`、`conf_room.glb` 各截 `r.restir.enable` on/off 两张，多灯场景噪声肉眼显著下降，Cornell 单灯场景不劣化。
- **无偏性（本里程碑核心验收）**：offline progressive 1024 帧，RIS-only vs 旧 NEE 收敛图 diff < visual test 阈值 5。不过此关不得进 M2。
- 帧耗时：`SCOPED_GPU_TIMER` 记录候选生成开销，1024 灯 CDF 线性扫描若超 0.2ms 换二分查找。

**风险：** p̂ 与 shading 的 f 若各写一份，将来改面光模型时不同步 → 必须共享同一函数；W 的 NaN/Inf 防护（p̂=0 像素）。

## M2 时域蓄水池复用

**改动：**

- `Restir.slang`：时域合并——motion vector 重投影读 pong、`ObjectId`/屏内/`MOTIONMOMENT==0` 拒绝、当前像素重算 p̂、M clamp（`r.restir.mClamp`）。
- `PathTracingRenderer.cpp`：ping/pong 逐帧翻转；`TemporalResolve::IsHistoryValidForFrame()` 为假或非 primary view 时向 `FRestirRuntimeParameters` 写 `TemporalValid=0`。
- `Scene::UpdateLights`（`Scene.Update.cpp`）：灯集合 generation 计数（数量或 lightMatIdx 序列变化即 ++），经 UBO 或 RestirParameters 下发，变化帧时域 M 归零。
- debugMode 4：时域复用命中率视图。

**验证：**

- 静态收敛：`CornellBox.proc` 静止 60 帧，对比 M1 噪声再降一档（等效 M≈mClamp）。
- 运动：`gnb validate` 脚本驱动相机平移/急转 + 动体场景（`AnimationShowcase.proc`），盯灯影 lag、ghost、disocclusion 爆点；`RT_MOTIONMOMENT` 像素必须无历史污染。
- 失效路径：切 renderer 往返、切场景、camera cut，各自下一帧无脏历史（黑块/亮块）。
- 灯增删（编辑器里删灯/改材质）后无残影。

**风险:** 帧间相关噪声与 `ReProject` history clamp 的互动（设计 §3.5）——若出现绺状低频噪声被 clamp 放大，优先调 `r.restir.mClamp` 而非动 clamp 参数。

## M3 空间复用 + 独立 shade pass

**改动：**

- 新增 `assets/shaders/Core.RestirSpatialShade.comp.slang`：G-buffer 重建 primary 表面（`RT_PREV_DEPTHBUFFER` + 逆矩阵），空间邻居合并（几何测试：法线点积 > 0.9、深度相对差 < 10%、ObjectId 一致），最终可见性 + shading，`RT_SINGLE_DIFFUSE` 读改写。
- `PathTracingRenderer.cpp`：注册第 4 条 pipeline（`ZeroBindWithTLASPipeline`），dispatch 排在主 pass 后、`temporalPostChain_.Run` 前，补 reservoir 与 `RT_SINGLE_DIFFUSE` barrier；主 dispatch 内联 shading 改为只写 reservoir（shading 移交新 pass）。
- 空间合并的低差异邻居序列（per-frame 旋转的 golden-angle 螺旋）。

**验证：**

- bias 目测：progressive 参考 vs 完整 ReSTIR 收敛图，几何接缝/轮廓边缘变暗需肉眼不可辨；`gnb shot` 近距离盯 `conf_room.glb` 墙角与桌沿。
- 深度重建精度：riverland 或 1km 级 SCAD 地图远景无 shadow-acne/漏光；不达标则启用设计 §3.2 的世界坐标 RT 备选。
- perf：两段合计 < 0.6ms @1080p（`SCOPED_GPU_TIMER`），超了先降 `spatialSamples`。
- 断开空间复用（`r.restir.spatial=false`）回归 M2 行为。

**风险：** `RT_SINGLE_DIFFUSE` 读改写与 SHARC query pass 的写序；G-buffer 重建的表面与主 pass 内 `Vertex` 不完全一致（法线贴图后的 shading normal vs G-buffer normal）导致 p̂ 漂移——G-buffer 存的即 shading normal（`FetchGBuffer`），需在实现时确认取同一来源。

## M4 收尾集成

**内容：**

- offline progressive 强制 RIS-only 降级（`IsOfflineProgressiveRenderActive()` 查询挂钩）。
- 非 primary / Transient view 走旧路径的分流确认（PiP、缩略图、Remote 各过一遍）。
- DLSS 开启下过一遍（render-res 语义确认即可）。
- 可选实验：太阳并入候选池（独立 CVar，默认关，仅记录结论）。
- 决策 `r.restir.enable` 默认值；若默认开，重录 `gkNextVisualTest` baseline 并在 PR 里注明。
- 文档：设计文档状态"提案"→"现行"，本计划按 `docs/README.md` 生命周期退场（耐久知识提炼进设计文档），`docs/README.md` 索引同步。

**验证：** `gkNextVisualTest` 全量 + `temporal-history-and-denoising.md` 护栏清单全项（静止收敛、平移、快速旋转、物体边缘、renderer 切换、双 RenderView）。

## M5（可选探索，单独授权）ReSTIR GI 预研

不随 M1–M4 自动执行。方向备忘：蓄水池存首个间接 bounce 的重连点（位置/法线/出射辐射），reconnection shift + Jacobian 做时空复用；**SHARC 缓存作为重连点辐射的廉价目标函数评估**是本项目特有的协同点（候选评估不打光线，最终 shading 才验证）。预研以"单场景 demo + 等时噪声对比报告"为交付，不进主线开关。

## 全局风险与守则

- 每个里程碑收尾必须验证 `r.restir.enable=false` 逐位回归（visual test 无 diff），这是全程回退保险。
- shader 与 C++ 的结构体布局改动（M0）必须同 commit，`static_assert` 双侧尺寸。
- 不修改 `ThirdParty/sharc` 官方头；SHARC 侧只动 `Sharc.slang` 的可用性判定。
- 构建按 AGENTS.md targeted build：`./gnb.sh build gkNextRenderer gkNextUnitTests`；新增 shader 文件后需 `--reconfigure` 一次让 glob 收录。
