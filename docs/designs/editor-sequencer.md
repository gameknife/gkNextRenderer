---
title: "Editor Sequencer 与动画轨道编辑"
category: design
status: 现行
owner: gkNextEditor/Scene
created: 2026-08-02
last_updated: 2026-08-02
---

# Editor Sequencer 与动画轨道编辑

gkNextEditor 的 Sequencer 是 `Assets::Scene::Tracks()` 的直接编辑视图，不维护第二份动画资产。面板位于 `Panels/SequencerPanel.cpp`，默认停靠在底部区域，也可从 Windows 或 Tools 菜单开关。

## 数据与预览

- 一个 `AnimationTrack` 是一个对象或 Environment target，面板下展开实际通道；Transform 有 translation/rotation/scale，Environment 有太阳与天空的角度、强度和颜色。
- 播放头单位为秒。拖标尺、修改关键帧、停止或逐关键帧跳转会调用 `Scene::EvaluateTracks(time)` 立即写回节点本地 TRS 或环境设置并标记场景 dirty。
- Play 使用 Scene 原有 track Tick 路径；Sequencer 只控制所有轨道的 Playing 状态并同步显示时间，不引入第二个动画时钟。
- 新关键帧取播放头时刻目标的当前值。关键帧始终保持时间有序，拖动不能越过相邻关键帧；开启 Snap 时按指定 FPS 量化。
- `+ Track` 作用于 Outliner/Viewport 当前选择的 Node：已有 Transform track 时直接选中，没有时创建并默认选中 Translation 通道，随后可立即 `+ Key`。下拉菜单保留 Environment track 入口，两种入口都避免重复创建同一 target 的轨道。

## 编辑事务

加、删、移动关键帧，新增轨道以及关键帧时间/数值修改都进入全局 `CommandHistory`。命令保存编辑前后的 track vector，并在 Undo/Redo 后重新求值当前播放头，确保视口与数据同步。连续拖拽只在释放时提交一次命令。

## 持久化边界

Transform 轨道使用标准 glTF animation 保存；Environment 轨道使用 scene extras 的 `gkEnvironmentTracks`。Editor 的 Save As 同时支持 `.glb` 和 `.gltf`，新建但尚无关键帧的空轨道不会产生无效的 glTF animation；至少加入一个关键帧后会随场景写出。格式细节见 [场景导出 glTF/GLB 契约](scene-export-gltf-contract.md)。增加新的 AnimationTrack target 或通道时，必须同时更新面板通道列表、定点求值、SceneExport 与 GltfLoader，不能只增加 UI。
