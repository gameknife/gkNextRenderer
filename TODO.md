## 下一步任务
- [x] 提交当前修改
- [x] 确认AmbientCube的改造，目前Voxel的更新和GPU读取是正确的，但AmbientCube感觉没有工作。但hwlightbake在执行
- [x] 提交目前的修改
- [x] 清理上下文
- [x] 恢复wireframe的工作，这里wireframePipeline_，可考虑直接写在imgui绘制前，直接绘制在最终输出之上。不要像之前一样尝试写在RT_DENOISED上
- [x] Options下的bool HotReload{true}已经废弃，移除整个选项以及相关无用的逻辑
- [x] wireframe的修改有些问题，只有当scale为native的时候，绘制正常。当使用quality等缩放模式的时候，线框会位于画面的左上角。修复这个问题
- [x] 提交目前的修改
- [x] 把本地feature/productive-ui-refactor分支关于ui的重构以及前面的Brotato3D Tweaks的提交合并到本分支，并运行验证通过
- [x] 彻底解决LDraw的那个单元测试错误，如果是因为Optional资源问题，实在不行，干掉它
- [x] 目前.\gnb.bat run xxxx无法带上target本身支持的cmdline，很不方便，请改造。比如.\gnb.bat run gkNextRenderer --help，可以把gkNextRenderer本身的help打印出来。当然不一定是我说的这样，能够有办法带参数即可
- [x] Brotato3D目前应该在游戏过程中一直在重建BVH和刷新voxel数据。我希望是尽量减少刷新，除了一开始的场景，后续主角，敌人的移动，动态碎块，prop都不应该影响BVH和voxel。这样cpu线程压力会小很多。请作这个调整。

## 待确认任务


## 里程碑状态
未完成

