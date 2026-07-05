# Gaussian Splat / SOG

gkNextEngine 支持 PlayCanvas SOG v2 高斯溅射场景，包括打包 `.sog` 和非打包
`meta.json + WebP`。主验证资产为 `assets/sog/Grape.sog`。

## 运行与验证

```powershell
.\gnb.bat build gkNextRenderer
.\gnb.bat shot --scene assets/sog/Grape.sog --frames 120
```

成功加载时日志包含：

```text
decoded SOG ...
uploaded ... Gaussian splats ...
uploaded scene [Grape.sog] to gpu
```

## 渲染路径

`GaussianSplatPass` 在 logic renderer 写完 `RT_DENOISED` 后执行：

1. GPU 直方图、前缀和、scatter，生成由远到近的有序索引和 indirect draw。
2. 实例化 billboard 光栅到 `RT_SPLAT_ACCUM`，使用场景深度测试，不写深度。
3. compute compose 将透明累积结果按 over 规则合成回 `RT_DENOISED`。
4. 引擎原有 DLSS / blit resolve 继续处理最终 HDR 结果。

节点挂载 `GaussianSplatComponent`。组件支持显隐、射线拾取开关和透明度缩放；
场景使用整个 SOG 的 3σ AABB 进行选择、聚焦和相机绕物旋转。

## CVar

- `show.gaussianSplats`：全局显示开关。
- `r.splat.bucketCount`：GPU 深度排序桶数，运行时限制为 16–4096。
- `r.splat.maxCount`：每帧最多处理的 splat 数，0 表示全部。
- `r.splat.sigma`：billboard 半径，单位为标准差，运行时限制为 1–4。

示例：

```powershell
.\gnb.bat run gkNextRenderer --scene assets/sog/Grape.sog --cvar "r.splat.bucketCount 1024"
```

降低桶数主要减少 prefix 阶段成本；降低 `maxCount` 是更强的性能/质量旋钮。
`sigma` 越小 overdraw 越低，但会裁掉更多高斯尾部。
