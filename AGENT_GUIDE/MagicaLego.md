# MagicaLego 小游戏代码梳理

本文档梳理 `src/Application/MagicaLego` 目录下乐高搭建小游戏的结构与关键流程，便于后续扩展与维护。

## 功能概览
- 玩法：基于网格的乐高方块搭建，支持放置/挖掘/选择、朝向旋转、基座尺寸切换。
- 视觉：基于引擎渲染与 GPU Raycast，提供预览框、缩略图刷子与场景重建。
- 辅助：存档读写、时间轴回放、截图与录屏、背景音乐。

## 目录与入口
- `src/Application/MagicaLego/MagicaLegoGameInstance.hpp`
  - `MagicaLegoGameInstance`：核心运行逻辑。
  - 枚举：`ELegoMode`、`ECamMode`、`EBasePlane`、`EOrientation`。
  - 数据结构：`FBasicBlock`、`FPlacedBlock`、`FMagicaLegoSave`。
- `src/Application/MagicaLego/MagicaLegoGameInstance.cpp`
  - 主要运行流程与核心行为实现。
  - `CreateGameInstance` 为游戏入口工厂。
- `src/Application/MagicaLego/MagicaLegoUserInterface.hpp/.cpp`
  - `MagicaLegoUserInterface`：ImGui UI 逻辑。

## 核心数据结构
- `FBasicBlock`
  - `brushId_`：刷子索引。
  - `modelId_`：引擎模型 ID。
  - `matType` / `color`：材质与颜色。
  - `name` / `type`：显示用名称与类型。
- `FPlacedBlock`
  - `location`：格子坐标（int16）。
  - `orientation`：朝向。
  - `modelId_`：刷子索引（< 0 表示删除）。
- `FMagicaLegoSave`
  - `Save` / `Load`：写入/读取 `.mls` 存档。
  - 保存内容为 `brushs` + `records`。

## 运行流程
- `OnInit`
  - 加载 BGM 列表并播放。
  - 请求加载场景 `assets/models/legobricks.glb`。
- `OnSceneLoaded`
  - 隐藏基础板节点，复制基座并生成 21x21 网格。
  - 扫描并注册基础方块类型（`AddBlockGroup`）。
  - 初始化预览块 `previewNode_`。
  - 初始化基座显示与清理记录。
- `OnTick`
  - 触发 CPU Raycast 与指示器更新。
  - 相机中心与指示框缓动。
  - 预览块朝向与位置更新。
- `OnRenderUI` / `OnInitUI`
  - 委托给 `MagicaLegoUserInterface`。

## 放置与交互逻辑
- Raycast
  - `CPURaycast` 通过 `ProjectScreenToWorld` 产生射线，再由引擎 `RayCastGPU` 返回结果。
  - 命中后走 `OnRayHitResponse`。
- 放置/挖掘/选择
  - `ELM_Place`：根据 `currentBlockIdx_` 和 `currentOrientation_` 放置。
  - `ELM_Dig`：将目标位置写入 `modelId_ = -1`，等价删除。
  - `ELM_Select`：更新选中节点，并在 AutoFocus 下对焦。
- 动态块管理
  - `BlocksDynamics`：位置 hash -> `FPlacedBlock`。
  - `BlockRecords`：操作记录序列（用于回放与存档）。
  - `RebuildScene`：清理动态实例并重建所有放置块。

## 相机与操作
- 相机模式
  - `ECM_Orbit`：围绕中心旋转。
  - `ECM_Pan`：平移相机中心。
  - `ECM_AutoFocus`：选择/放置后自动对焦。
- 鼠标/键盘
  - 键盘：`Q/W/E` 模式，`A/S/D` 相机，`1/2/3` 基座，`R` 旋转。
  - 鼠标：左键放置/选择，右键旋转视角，滚轮控制 FOV。

## UI 模块概览
- 标题栏 `DrawTitleBar`
  - 窗口控制、截图/录屏、BGM、质量切换、帮助入口。
- 左侧栏 `DrawLeftBar`
  - 模式、相机、基座、光照参数、存档管理。
- 右侧栏 `DrawRightBar`
  - 类型选择 + 方块缩略图刷子。
- 时间轴 `DrawTimeline`
  - 回放步数、播放/暂停、回滚到某一步。
- 引导与提示
  - `DrawOpening` / `DrawHelp` / `DrawNotify`。

## 资源与文件路径
- 场景模型：`assets/models/legobricks.glb`
- pak：`assets/paks/lego.pak`、`assets/paks/thumbs.pak`
- 缩略图：`assets/textures/thumb/thumb_<type>_<name>.jpg`
- 存档：`assets/legos/*.mls`
- 截图：`screenshots/`
- 录屏：`captures/` + `temps/`，通过 ffmpeg 拼接。

## 常见扩展点
- 新增方块类型
  - 在场景中添加对应命名的节点；`AddBlockGroup` 会自动收集。
  - 增加 `BasicNodeIndicatorMap` 尺寸配置。
  - 提供对应缩略图资源。
- 存档版本升级
  - `MAGICALEGO_SAVE_VERSION` 与 `FMagicaLegoSave::Load`。
  - 需要兼容旧版 `brushId_` 映射逻辑。
- 交互扩展
  - 在 `OnRayHitResponse` 中新增模式或额外处理。
  - 在 `MagicaLegoUserInterface` 添加工具/按钮入口。

## 已知注意点
- `RebuildScene` 会清空动态实例并重建，放置频繁时可能有性能压力。
- `BlocksDynamics` 以 hash 作为 key，需保证 `GetHashFromBlockLocation` 不冲突。
- 缩略图在非 Apple 平台按文件名动态加载。
- 录屏依赖系统 `ffmpeg`，缺失时会失败。

## 关联文件速查
- `src/Application/MagicaLego/MagicaLegoGameInstance.hpp`
- `src/Application/MagicaLego/MagicaLegoGameInstance.cpp`
- `src/Application/MagicaLego/MagicaLegoUserInterface.hpp`
- `src/Application/MagicaLego/MagicaLegoUserInterface.cpp`
