# NoAmbientDeferred TAA 抖动 + 过曝 —— 问题定位与修复计划（方案 A：实现真 TAA）

> 状态：已完成（2026-06-13）
> 方案：为该路径补齐时序累积（reproject + history），让 TAA 真正生效。**不采用"关掉 jitter"的回避方案。**
> 影响渲染器：`Vulkan::NoAmbientDeferred::Renderer`（枚举 `ERT_LegacyDeferredNoAmbient`，UI 名 `SoftModernNoAmbient`）
> 关联文件：
> - `src/Engine/Rendering/SoftwareModern/SwModernNoAmbientRenderer.{cpp,hpp}`
> - `assets/shaders/Core.SwModernNoAmbient.comp.slang`
> - `assets/shaders/Process.ComposeSimple.comp.slang`
> - 新增 `assets/shaders/Process.ReProjectSimple.comp.slang`
> - 参考：`SoftwareModernRenderer.cpp`、`Process.ReProject.comp.slang`、`Shading.slang`

## 1. 现象

`NoAmbientDeferred` 渲染器下开启 TAA（`r.taa`，默认 `true`）：

1. 画面持续抖动，没有任何 resolve（收敛）效果。
2. 输出颜色相比关闭 TAA 时偏亮 / 高光发灰发白（"有点过曝"）。

## 2. 根因分析

### 2.1 该渲染器根本没有 TAA resolve（抖动的直接原因）

`NoAmbientDeferred::Renderer::Render()` 的完整管线只有两个 pass（`SwModernNoAmbientRenderer.cpp:35-62`）：

```
Core.SwModernNoAmbient.comp   (shading → RT_SINGLE_DIFFUSE / RT_OBJEDCTID_0 / RT_PREV_DEPTHBUFFER)
Process.ComposeSimple.comp    (直接 tonemap RT_SINGLE_DIFFUSE → RT_DENOISED)
```

它**没有** reproject / accumulate pass，也**没有** history 拷贝。对比完整路径 `LegacyDeferred::SoftwareModernRenderer::Render()`（`SoftwareModernRenderer.cpp:53-126`），后者是完整的 `shading → Process.ReProject.comp（时序累积）→ compose → copy 到 RT_SINGLE_PREV_*` 四段。

而 TAA 的 jitter 是**全局**施加的，与具体渲染器无关。`Engine.CameraUbo.cpp:81-91`：

```cpp
if (config_.userSettings.TAA || config_.userSettings.DLSS) {
    glm::vec2 jitter = haltonSeq[...] - glm::vec2(0.5f, 0.5f);
    ubo.Projection[2][0] = jitter.x / extent.width  * 2.0f;   // 把 Halton 抖动写进投影矩阵
    ubo.Projection[2][1] = jitter.y / extent.height * 2.0f;
    ubo.Jitter = glm::vec4(jitter.x, jitter.y, 0, 0);
}
```

`Core.SwModernNoAmbient.comp.slang:49-52` 用的是被抖动过的 `Camera.ProjectionInverse` 重建主光线。

**结论**：每帧投影被亚像素抖动 → shading 在亚像素偏移处采样 → 因为没有任何时序累积把多帧抖动样本平均回去，抖动被原样显示，画面一直在抖。该路径是作为"低配 / no-ambient 轻量路径"加入的（commit `4e5ebcbe`），从未实现 resolve，而全局 jitter 默认开启，于是 `r.taa=true` 即暴露。

### 2.2 过曝原因（与缺 resolve 同源）

先排除"tonemap 常数不同"：`Process.ComposeSimple.comp.slang:111` 与完整路径 `Process.DenoiseJBF.comp.slang:228` 的 SDR tonemap 完全一致（`GT_Tonemapping(total * PaperWhiteNit / 40000.0)`）。

`GT_Tonemapping`（Gran Turismo 曲线，`Tonemap.slang:58-79`）高光段是**强压缩的凹曲线**。正确 TAA 必须在**线性空间**平均多帧辐射度、**然后只 tonemap 一次**。当前路径每帧对"被抖动的单样本"单独做非线性 tonemap 并直接显示；对亚像素细高光 / 高对比边缘，jitter 让覆盖率逐帧翻转，人眼对**已被 tonemap 压缩**的闪烁做时序积分，结果比"先平均再 tonemap"的正确帧更亮 → 表现为"有点过曝"。

**因此抖动与过曝是同一根因。** 实现"线性域累积 + 单次 tonemap"后两者一并消失——这正是方案 A 要做的。

### 2.3 关联（不在本次范围）

`VoxelTracing::VoxelTracingRenderer::Render()`（`SoftwareModernRenderer.cpp:148-162`）同样只有 shading、无 reproject，开启 TAA 会有相同抖动。本次只修 `NoAmbientDeferred`，此处仅记录。

## 3. 方案 A 总览

为 `NoAmbientDeferred` 补一条**轻量时序累积**，管线改为：

```
Core.SwModernNoAmbient.comp   (额外产出 RT_MOTIONVECTOR / RT_NORMAL)
Process.ReProjectSimple.comp  (新增：reproject + history blend → RT_ACCUMLATE_DIFFUSE)   ← 线性 HDR 域
Process.ComposeSimple.comp    (读 RT_ACCUMLATE_DIFFUSE，tonemap 不变)
copy RT_ACCUMLATE_DIFFUSE → RT_SINGLE_PREV_DIFFUSE  (供下一帧做 history)
```

设计要点：
- **累积在 tonemap 之前的线性 HDR 域**，compose 仍只 tonemap 一次（同时修抖动与过曝）。
- 本路径是单张全色 `RT_SINGLE_DIFFUSE`，**不直接复用** `Process.ReProject.comp`（它假设 diffuse 已 demodulate + 独立 albedo/specular 三张图），而是写一个单缓冲的极简版。
- 复用已有资源：`RT_ACCUMLATE_DIFFUSE` / `RT_SINGLE_PREV_DIFFUSE` / `RT_MOTIONVECTOR` / `RT_NORMAL` / `RT_OBJEDCTID_0/1` / `RT_MOTIONMOMENT` 均已在 `VulkanBaseRenderer.cpp:558-576` 创建；history `RT_OBJEDCTID_1` 由 base `PostRender::CopyObjectIdHistory`（`VulkanBaseRenderer.GpuDriven.cpp:632-655`）每帧维护，**无需改动**。

## 4. 修复计划（分步，可直接执行）

### Step A1 — shading 产出 motion vector 与 normal

文件：`assets/shaders/Core.SwModernNoAmbient.comp.slang`

命中分支（line 163-165 附近，写 `RT_SINGLE_DIFFUSE` / `RT_OBJEDCTID_0` / `RT_PREV_DEPTHBUFFER` 处）额外写：
- `RT_NORMAL` ← `normalize(hitVertex.Normal)`（世界法线，`float4(N,1)`）。
- `RT_MOTIONVECTOR` ← 复用 `Common.CalculateMotionVector(Camera, hitNode, hitVertex, pixel)`（`Shading.slang:78-100`，用 `ViewProjectionUnJit`/`PrevViewProjectionUnJit`/`node.combinedPrevTS`；内部会同时写 `RT_MOTIONMOMENT`）。
  - 若直接调用不便（该 helper 在某 struct 内），可内联其逻辑：当前/历史世界位置分别用 `ViewProjectionUnJit` 与 `PrevViewProjectionUnJit*combinedPrevTS` 投到屏幕，差值即 motion；并按 `distance(currPos, prevPos)>0.02` 写 `RT_MOTIONMOMENT = TemporalFrames`，否则递减。

miss（天空）分支（line 64-67 附近）补齐，避免读到陈旧值：
- `RT_MOTIONVECTOR` ← `float4(0)`；`RT_NORMAL` ← `float4(0)`；`RT_MOTIONMOMENT` ← `Camera.TemporalFrames`（强制天空丢弃 history）。

> 注意 motion vector 用的是 **UnJit** 矩阵（去抖动），这样 reproject 定位的是真实像素位移、不把 jitter 当成运动；而 shading 主光线仍用 jitter 后的 `ProjectionInverse` 采样。两者职责不同，勿混用。

### Step A2 — 新增极简 resolve shader

新文件：`assets/shaders/Process.ReProjectSimple.comp.slang`（`[numthreads(8,8,1)]`）。需让构建系统收录该 `.slang`（参考其它 `Process.*.comp.slang` 的 glob/CMake 收录方式；新增文件可能需 `--reconfigure`）。

输入（全部 bindless storage image）：`RT_SINGLE_DIFFUSE`(当前全色)、`RT_MOTIONVECTOR`、`RT_OBJEDCTID_0`、`RT_OBJEDCTID_1`、`RT_MOTIONMOMENT`、`RT_NORMAL`、history color = `RT_SINGLE_PREV_DIFFUSE`。
输出：`RT_ACCUMLATE_DIFFUSE`。

逻辑（线性域，参考 `Process.ReProject.comp.slang` 但只处理单缓冲）：
1. `current = RT_SINGLE_DIFFUSE[ipos]`。
2. `motion = RT_MOTIONVECTOR[ipos].rg`；`previpos = floor(ipos + motion)`。
3. history 有效性：`previpos` 在界内 且 `FetchPrimitiveIndex(RT_OBJEDCTID_0[ipos]) == FetchPrimitiveIndex(RT_OBJEDCTID_1[previpos])` 且 `RT_MOTIONMOMENT[ipos]==0`，否则 `useHistory=false`。
4. `history = bilinearSample(RT_SINGLE_PREV_DIFFUSE, ipos+motion)`（用 `FilterHistoryColor` 同款 subpixel 双线性，可裁掉它的多 primitive id 校验、仅保留有效性判断）。
5. **邻域 YCoCg AABB clamp** 抑制 ghosting：把 `Process.ReProject.comp.slang:204-221` 注释掉的那段启用（对 3×3 或 5×5 邻域取 `rgb2ycocg` 的 min/max，`history = clamp` 回 AABB）。
6. `out = useHistory ? lerp(historyClamped, current.rgb, 1.0/max(1,TemporalFrames)) : current.rgb;`
7. 写 `RT_ACCUMLATE_DIFFUSE[ipos] = float4(out, 1)`。

push constant 用 `ZeroBindCustomPushConstantPipeline`（仿 `SoftwareModernRenderer.cpp:29`），至少传 `TemporalFrames` 与 history 的 bindless id（非 ReferenceMode 下即 `RT_SINGLE_PREV_DIFFUSE`）。

### Step A3 — renderer 接入管线

文件：`src/Engine/Rendering/SoftwareModern/SwModernNoAmbientRenderer.{hpp,cpp}`

`hpp`：新增成员 `std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> accumulatePipeline_;`（如用自定义 push）。

`cpp::CreateSwapChain`：
- 新建 `accumulatePipeline_`（指向 `Process.ReProjectSimple.comp.slang.spv`）。
- 非 ReferenceMode 下 history 用 `Assets::Bindless::RT_SINGLE_PREV_DIFFUSE`；ReferenceMode 用 `GetTemporalStorageImage`（仿 `SoftwareModernRenderer.cpp:32-43`，单张 diffuse 即可）。

`cpp::Render`（在 shading 之后、compose 之前插入）：
1. shading 后的 barrier 增加 `RT_MOTIONVECTOR`、`RT_NORMAL`（write→read），保留现有 `RT_SINGLE_DIFFUSE` / `RT_OBJEDCTID_0` / `RT_PREV_DEPTHBUFFER`。
2. `SCOPED_GPU_TIMER("reproject pass")`：bind `accumulatePipeline_`，dispatch `Math::GetSafeDispatchCount(w,8) × GetSafeDispatchCount(h,8)`；之后对 `RT_ACCUMLATE_DIFFUSE` 加 barrier（write→read）。
3. compose pass 不变（但其源已切到 accumulate，见 Step A4）。
4. `SCOPED_GPU_TIMER("copy pass")`：`vkCmdCopyImage` 把 `RT_ACCUMLATE_DIFFUSE` → `RT_SINGLE_PREV_DIFFUSE`（仿 `SoftwareModernRenderer.cpp:102-125`，只拷 diffuse 一张，注意前后 layout barrier）。

### Step A4 — compose 读累积结果

文件：`assets/shaders/Process.ComposeSimple.comp.slang:58`
- `LightingImage` 源从 `Bindless.RT_SINGLE_DIFFUSE` 改为 `Bindless.RT_ACCUMLATE_DIFFUSE`。
- tonemap / edge 高光段（line 77-112）保持不变。

## 5. 验证计划

1. **构建**：`./gnb build gkNextRenderer gkNextUnitTests`；新增 shader 文件确认 `.spv` 生成（必要时 `--reconfigure`）。
2. **静态机位对比**（`gnb shot`，免手动开窗）：
   ```
   gnb shot --scene assets/models/playground.glb
   ```
   分别截：TAA 关 / 修复前 TAA 开 / 修复后 TAA 开。
3. **抖动判定**：连续多帧静止机位截图，修复后应收敛、无亚像素抖动，边缘更平滑。
4. **过曝判定**：对比三图亮度 / 高光区直方图，确认修复后亮度回到 TAA 关闭水平。
5. **动态 ghosting**：移动相机 / 移动物体，确认无明显拖影（YCoCg clamp 生效）；disocclusion 区域应回退到当前帧、无残影。
6. **回归**：切到 `SoftModern` 等其它渲染器，确认本次改动（仅限 NoAmbient 文件 + 新 shader）未影响它们。
7. 跑 `gkNextUnitTests` 确认 engine API 无破坏。

## 6. 风险与注意事项

- **motion vector 必须用 UnJit 矩阵**（见 Step A1），否则 reproject 会把 jitter 误判为运动，导致永远丢弃 history、抖动依旧。
- `RT_MOTIONMOMENT` 由 `CalculateMotionVector` 写、被 resolve 读，确保两端都接入，避免读陈旧值导致 history 永久失效。
- jitter 除数用 `OutputExtent`（`Engine.CameraUbo.cpp` 传入 `OutputExtent`），shading/dispatch 用 `RenderExtent`；若启用 SuperResolution 两者不等，jitter 尺度会偏。修复后若在缩放分辨率下仍抖，按此排查（与本修复正交）。
- 动态 / 蒙皮物体的 motion 依赖 `node.combinedPrevTS` 每帧正确更新（完整 SwModern 路径已依赖，风险低）。
- 首帧 / 切换渲染器首帧无有效 history，resolve 需回退到当前帧（`useHistory=false`），避免读到未初始化的 `RT_SINGLE_PREV_DIFFUSE`。
- 新增 shader 文件后若未被自动收录，构建会找不到 `.spv`，记得 `--reconfigure`。

## 7. 关键代码位置速查

| 作用 | 文件:行 |
| --- | --- |
| NoAmbient 渲染管线（缺 resolve，需改） | `SwModernNoAmbientRenderer.cpp:35-62` |
| NoAmbient shading（需补 motion/normal） | `Core.SwModernNoAmbient.comp.slang:37-166` |
| NoAmbient compose（需改源为 accumulate） | `Process.ComposeSimple.comp.slang:58,101-112` |
| 全局 jitter 施加（保持开启） | `Engine.CameraUbo.cpp:81-95` |
| 参考实现：完整 TAA 路径 | `SoftwareModernRenderer.cpp:53-126` |
| 参考实现：reproject shader（含 YCoCg clamp 注释段） | `Process.ReProject.comp.slang:53-234` |
| motion vector 计算 helper | `Shading.slang:78-100` |
| object id history 拷贝（通用，无需改） | `VulkanBaseRenderer.GpuDriven.cpp:632-655` |
| 时序资源创建（accumulate/prev/motion/normal） | `VulkanBaseRenderer.cpp:558-576` |
| tonemap 曲线 | `Tonemap.slang:58-84` |
