---
title: "ReProject 历史钳制后续计划"
category: plan
status: 部分完成
owner: rendering
created: 2026-06-18
last_updated: 2026-07-17
---

# ReProject 历史钳制后续计划

本计划只保留当前仍未落地的 Phase B/C。历史黑点问题的第一阶段已经实现；不要重复加入另一套同义 clamp，也不要根据已删除的 ReLAX/NoAmbient 计划恢复旧管线。

## 已落地：Phase A

`assets/shaders/Process.ReProject.comp.slang` 当前通过 `ClampHistorySoft` 对 reprojected history 做邻域软钳制，并在邻域样本不足时使用空间 fallback。运行时参数为：

- `r.reproject.clampGammaHi`
- `r.reproject.clampGammaLo`
- `r.reproject.clampFloor`

参数注册与默认值以当前 renderer CVar 代码为准。该路径只调节 history 接受范围，不应被描述成 motion-vector 开关或完整 denoiser。

## Phase B：仅在动态证据充分时引入 temporal moments

曾有过 moments 实验，但已回退，当前代码没有可依赖的 moments history。只有在固定 Phase A 后仍能用移动相机/动态物体稳定复现闪烁或黑点，并且截图/trace 能证明单帧邻域 clamp 不足时，才执行：

1. 为 history 增加 luminance first/second moment，明确格式、初始化、resize 和 reset 生命周期。
2. 用 variance 适配 clamp 半径或 history confidence，不改变 current-frame 空间样本。
3. 为 disocclusion、无 motion vector、边缘低样本和 NaN/Inf 输入写最小测试/验证场景。
4. 比较 GPU 时间、显存和静态噪声；收益不足则不合并。

不要先实现再寻找问题。Phase B 的进入条件是可重复动态 baseline，而不是本文存在。

## Phase C：量化与收尾

- 使用相同场景、相同帧和相同 renderer 配置保存 before/after 图。
- 至少覆盖静态细线/高亮、相机平移、遮挡显露和低帧 history reset。
- 记录黑点数量或图像 diff、GPU 时间与额外 history 内存。
- 最终只保留有明确收益的 CVars；稳定默认值写进配置/代码，实验开关删除。

## 验证命令

```bash
./gnb.sh build gkNextRenderer gkNextUnitTests
./gnb.sh shot --scene assets/models/playground.glb --frames 60
./out/build/<preset>/bin/gkNextUnitTests
```

动态问题应补 `gnb validate` 脚本或视觉测试，单张静态截图不能证明 temporal 稳定性。
