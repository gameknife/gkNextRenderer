# Archive

## Pre-workflow（2026-05-14 之前）

从旧版根目录 `TODO.md` 迁移。无 ID、无 journal，仅保留原始描述。

- [x] 提交当前修改
- [x] 确认 AmbientCube 的改造，目前 Voxel 的更新和 GPU 读取是正确的，但 AmbientCube 感觉没有工作。但 hwlightbake 在执行
- [x] 提交目前的修改
- [x] 清理上下文
- [x] 恢复 wireframe 的工作，这里 wireframePipeline_，可考虑直接写在 imgui 绘制前，直接绘制在最终输出之上。不要像之前一样尝试写在 RT_DENOISED 上
- [x] Options 下的 bool HotReload{true} 已经废弃，移除整个选项以及相关无用的逻辑
- [x] wireframe 的修改有些问题，只有当 scale 为 native 的时候，绘制正常。当使用 quality 等缩放模式的时候，线框会位于画面的左上角。修复这个问题
- [x] 提交目前的修改
- [x] 把本地 feature/productive-ui-refactor 分支关于 ui 的重构以及前面的 Brotato3D Tweaks 的提交合并到本分支，并运行验证通过
- [x] 彻底解决 LDraw 的那个单元测试错误，如果是因为 Optional 资源问题，实在不行，干掉它
- [x] 目前 .\gnb.bat run xxxx 无法带上 target 本身支持的 cmdline，很不方便，请改造。比如 .\gnb.bat run gkNextRenderer --help，可以把 gkNextRenderer 本身的 help 打印出来。当然不一定是我说的这样，能够有办法带参数即可
- [x] Brotato3D 目前应该在游戏过程中一直在重建 BVH 和刷新 voxel 数据。我希望是尽量减少刷新，除了一开始的场景，后续主角，敌人的移动，动态碎块，prop 都不应该影响 BVH 和 voxel。这样 cpu 线程压力会小很多。请作这个调整。
