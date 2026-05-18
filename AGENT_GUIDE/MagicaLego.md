# MagicaLego 小游戏代码梳理

本文档梳理 `src/Application/Game/MagicaLego` 目录下乐高搭建小游戏的结构与关键流程，便于后续扩展与维护。

## 功能概览
- 玩法：基于网格的乐高方块搭建，支持放置/挖掘/选择、朝向旋转、基座尺寸切换。
- 视觉：基于引擎渲染与 GPU Raycast，提供预览框、缩略图刷子与场景重建。
- 辅助：存档读写、时间轴回放、截图与录屏、背景音乐。

## 目录与入口
- `src/Application/Game/MagicaLego/MagicaLegoGameInstance.hpp`
  - `MagicaLegoGameInstance`：核心运行逻辑。
  - 枚举：`ELegoMode`、`ECamMode`、`EBasePlane`、`EOrientation`。
  - 数据结构：`FBasicBlock`、`FPlacedBlock`、`FMagicaLegoSave`。
- `src/Application/Game/MagicaLego/MagicaLegoGameInstance.cpp`
  - 主要运行流程与核心行为实现。
  - `CreateGameInstance` 为游戏入口工厂。
- `src/Application/Game/MagicaLego/MagicaLegoUserInterface.hpp/.cpp`
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

## 脚本系统 (mlscript)

MagicaLego 支持脚本化建造，通过 `.mlscript` 文件或控制台命令执行。

### 核心文件
- `MagicaLegoCommands.hpp/.cpp` - 命令定义与解析
- `MagicaLegoScriptParser.hpp/.cpp` - 脚本解析器（支持变量、循环）

### 命令列表
| 命令 | 语法 | 说明 |
|-----|------|------|
| place | `place <Type>/<Color> <x> <y> <z> [orientation]` | 放置方块 |
| place | `place <Type>/<Color> here/ahead [n]` | 相对光标放置 |
| dig | `dig <x> <y> <z>` | 挖掘方块 |
| list | `list types` / `list colors [type]` | 列出可用类型/颜色 |
| move | `move <forward/backward/left/right/up/down> [n]` | 移动光标 |
| turn | `turn <left/right/around>` | 转向 |
| goto | `goto <x> <y> <z>` | 光标绝对定位 |
| face | `face <north/east/south/west>` | 设置光标朝向 |
| scan | `scan [radius]` | 扫描周围方块 |

### 脚本语法
```mlscript
# 注释
var height = 5

repeat $height as y
    place Block1x1/#0 0 $y 0
end
```

### Cursor 系统
光标（FCursor）是相对坐标系统的核心，类似 Turtle Graphics：
- `position`: 当前位置 (glm::i16vec3)
- `facing`: 朝向 (North=-Z, East=+X, South=+Z, West=-X)
- 用户手动放置方块时，光标自动同步到新位置

## AI 助手集成

### 核心文件
- `MagicaLegoAIService.hpp/.cpp` - Gemini API 集成
- 配置文件: `assets/configs/ai_config.json`

### 配置格式
```json
{
    "apiKey": "YOUR_GOOGLE_API_KEY",
    "model": "gemini-2.0-flash",
    "endpoint": "https://generativelanguage.googleapis.com/v1beta"
}
```

### 功能特性
- **脚本生成**: 用自然语言描述，AI 生成 mlscript
- **上下文感知**: "Build on existing" 模式包含当前场景状态
- **颜色词汇表**: 自动分析颜色生成语义描述（自然色、建筑色等）
- **脚本验证**: 自动修复缺失的 `end` 语句

### 提示词设计要点
1. 明确坐标系统: North=-Z, East=+X, South=+Z, West=-X
2. 列出所有可用方块类型和颜色代码
3. 提供示例脚本展示语法
4. 强调扁平方块（Plate/Flat）应先放置作为地基

## 代码组织

### 文件结构
```
src/Application/Game/MagicaLego/
├── MagicaLegoGameInstance.hpp/cpp   # 核心游戏逻辑 (~1200行)
├── MagicaLegoUserInterface.hpp/cpp  # UI 渲染 (~1700行)
├── MagicaLegoCommands.hpp/cpp       # 命令系统 (~700行)
├── MagicaLegoScriptParser.hpp/cpp   # 脚本解析 (~500行)
├── MagicaLegoAIService.hpp/cpp      # AI 集成 (~800行)
├── MagicaLegoConstants.hpp          # 集中常量定义
├── MagicaLegoUIHelpers.hpp          # UI 辅助函数
└── MagicaLegoStyle.hpp/cpp          # ImGui 样式
```

### 常量管理
所有魔法数字集中在 `MagicaLegoConstants.hpp`:
```cpp
namespace MagicaLego
{
    namespace Grid { constexpr float UnitX = 0.08f; /* ... */ }
    namespace UI { constexpr float TitleBarHeight = 40.0f; /* ... */ }
    namespace Anim { constexpr float Fast = 0.2f; /* ... */ }
    namespace AI { constexpr int MaxOutputTokens = 819200; /* ... */ }
}
```

### UI 文件组织
`MagicaLegoUserInterface.cpp` 使用区域注释组织：
- Constructor / Destructor / Initialization
- Title Bar and Opening Animation
- Main Render Loop
- Overlays and HUD
- Recording and Layout Management
- Main Tool Bar and Side Panels
- Timeline
- Console and AI Assistant

## 开发经验总结

### 最佳实践
1. **常量集中管理**: 避免散落的魔法数字，便于调整和维护
2. **辅助函数提取**: 可复用的 UI 代码放入 `UIHelpers.hpp`
3. **区域注释**: 大文件使用 `// ====` 风格的区域分隔符
4. **异步 AI 调用**: 使用回调避免阻塞 UI 线程
5. **脚本验证**: AI 生成的脚本需要验证和自动修复

### 调试技巧
- 控制台窗口可直接输入命令测试
- `list types` / `list colors` 查看可用资源
- `scan` 命令查看周围方块状态
- AI 生成的脚本会显示在 "Last Generated Script" 区域

## 关联文件速查
- `src/Application/Game/MagicaLego/MagicaLegoGameInstance.hpp`
- `src/Application/Game/MagicaLego/MagicaLegoGameInstance.cpp`
- `src/Application/Game/MagicaLego/MagicaLegoUserInterface.hpp`
- `src/Application/Game/MagicaLego/MagicaLegoUserInterface.cpp`
- `src/Application/Game/MagicaLego/MagicaLegoCommands.hpp`
- `src/Application/Game/MagicaLego/MagicaLegoCommands.cpp`
- `src/Application/Game/MagicaLego/MagicaLegoScriptParser.hpp`
- `src/Application/Game/MagicaLego/MagicaLegoScriptParser.cpp`
- `src/Application/Game/MagicaLego/MagicaLegoAIService.hpp`
- `src/Application/Game/MagicaLego/MagicaLegoAIService.cpp`
- `src/Application/Game/MagicaLego/MagicaLegoConstants.hpp`
- `src/Application/Game/MagicaLego/MagicaLegoUIHelpers.hpp`
- `assets/configs/ai_config.json`
- `assets/scripts/*.mlscript`
