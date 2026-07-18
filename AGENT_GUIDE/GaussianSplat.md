# Gaussian Splat / SOG

当前实现位于可选模块 `src/Modules/SplatLoader/`。`SplatModule::Install()` 注册 `.sog` loader、反射组件、运行时 CVar 和 external render pass；Engine 核心不直接拥有 SOG 解析器。

SOG v2 的反量化、坐标/covariance 转换、Scene 产物和 external-pass 契约见 [现行设计](../docs/designs/gaussian-splatting-sog-design.md)。修改 loader 时必须同时遵守该格式契约，不能只以某个示例资产“看起来能开”为准。

PlayCanvas SOG v2 可以是打包 `.sog`，也可以由 loader 内部读取 `meta.json + WebP` 条目。示例 `assets/sog/Grape.sog` 属于可选资产，不应假定 source tree 一定存在；缺少时先检查 `./gnb.sh paks list`。

## 运行与验证

```bash
./gnb.sh build gkNextRenderer
./gnb.sh shot --scene assets/sog/Grape.sog --frames 120
```

Windows 使用对应的 `gnb.bat`。成功日志包含 `decoded SOG`、`uploaded ... Gaussian splats` 和 `uploaded scene [Grape.sog] to gpu`。没有可选样例时，以当前可用 `.sog` 输入替代，不要把资产缺失写成 loader 平台限制。

## 当前渲染路径

`GaussianSplatPass` 是 primary view 后、debug overlay 前的 external content pass：

1. GPU 直方图、前缀和与 scatter 生成由远到近的索引和 indirect draw；静止相机/模型可复用 sort cache。
2. 实例化 billboard 写入 `RT_SPLAT_ACCUM`，使用场景深度测试但不写深度。
3. compute compose 把透明累积结果合成回 `RT_DENOISED`，随后继续走正常 DLSS/blit resolve。
4. `SplatProxyBuilder` 可为 splat 创建隐藏的体素化 proxy mesh，供阴影、ray occlusion、拾取/选择边界等非 billboard 路径使用。

节点挂载 `Runtime::GaussianSplatComponent`。组件拥有显隐、射线拾取、透明度、proxy 阴影/遮挡和密度阈值等可反射属性；不要把所有行为写成全局 CVar。

## CVar 分组

- 可见与排序：`show.gaussianSplats`、`r.splat.bucketCount`、`r.splat.maxCount`、`r.splat.sortCache`。
- billboard：`r.splat.sigma`、`r.splat.forceAA`、`r.splat.aaStrength`。
- proxy：`r.splat.proxy.enable/gridMax/brickSize/sigma/isoThreshold/simplifyRatio/debugVisible`。
- 交互与光照：`r.splat.shadow.enable`、`r.splat.rayOcclusion.enable`、`r.splat.receiveLighting`、`r.splat.lightingStrength`、`r.splat.proxy.debug`。

默认值和运行时钳制以 `SplatSettings.cpp`、`GaussianSplatPass.cpp` 与 `SplatProxyBuilder.cpp` 为准。尤其 `bucketCount` 是最小请求值，pass 还会根据 splat 数自动提高；不要把它描述成始终精确的桶数。

降低 `maxCount` 是直接的质量/性能旋钮；降低桶数主要影响排序，但会受自动下限约束。`sigma` 越小 overdraw 越低，也会裁掉更多高斯尾部。修改 proxy 参数后还要验证阴影、ray occlusion 与隐藏 proxy 是否同步，不能只看正面截图。
