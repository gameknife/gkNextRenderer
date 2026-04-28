# 1 小时任务开发计划 第三批 (2026-04, Editor 快捷键交互 + Properties 面板进阶)

## Context

第二批 8 项任务已合并到 commit `3d64fb87`(Editor UX 收尾 + Showcase 场景扩展)。本批延续「<1h 自包含」节奏,聚焦两个用户日常感受最强烈的方向:

1. **Editor 快捷键与交互**: 当前编辑器仅支持 `Esc`(取消选中)、`F`(聚焦)、`F2`(重命名,Outliner 内)、`W/E/R`(gizmo 切操作)。缺少 `Delete`、`Ctrl+D`(复制)、`Ctrl+S`(保存)、`Q`(World/Local 切换)等通用 3D/桌面编辑器标配。Gizmo Toolbar 也缺 hotkey 提示与 World/Local UI,新用户/agent 学习曲线陡。
2. **Properties 面板进阶**: 上批解决了空选 UX,本批让大场景/多组件物体的编辑效率有质变 — 加属性搜索框、每属性 reset-to-default 按钮。

A1 (Save Scene As 文件对话框) 继续作为 known issue 延后,等专门时间窗口处理。

## 使用方法

1. **挑任务**: 任选 1 项,逐项完成 TODO。
2. **遵循公共约束**: 命名/构建/平台规则全部沿用 [`AGENTS.md`](../AGENTS.md)。
3. **构建 preset**: 验证一律使用 `full-*` preset,与上两批一致。
4. **报告**: 完成后简述「改了哪些文件、测了什么、看到的输出」,**不**总结代码意图。

## 公共上下文(快速参考)

| 项 | 值 |
|---|---|
| 构建 | `./build.bat --preset full-windows` / `./build.sh --preset full-macos-arm64` |
| 运行编辑器 | `./run.bat --preset full-windows --target gkNextEditor.exe` |
| 运行渲染器 | `./run.bat --preset full-windows --target gkNextRenderer.exe` |
| 运行 visual test | `./out/build/full-windows/bin/gkNextVisualTest.exe` |
| 运行成功标志 | 日志出现 `uploaded scene [...] to gpu` |
| 命名规范 | 类型/函数 PascalCase;变量/参数 camelCase;私有成员 trailing `_`;详见 `.clang-tidy` |
| 头文件首选 | 新文件首先 `#include "Common/CoreMinimal.hpp"` |

> 提示:**禁止**修改 `src/ThirdParty/` 与 `external/`。

---

## 任务索引

| # | 标题 | 工时 | 优先级 |
|---|---|---|---|
| [K1](#k1-delete-键删除选中节点) | Delete 键删除选中节点 | ~30m | P0 |
| [K2](#k2-ctrld-复制选中节点) | Ctrl+D 复制选中节点 | ~30m | P0 |
| [K3](#k3-q-切换-gizmo-worldlocal-空间) | Q 切换 Gizmo World/Local 空间 | ~30m | P0 |
| [K4](#k4-gizmo-toolbar-hotkey-tooltip) | Gizmo Toolbar hotkey tooltip | ~15m | P1 |
| [K5](#k5-viewportoverlay-显示-gizmo-状态mode--space) | ViewportOverlay 显示 Gizmo 状态(Mode + Space) | ~20m | P1 |
| [K6](#k6-ctrls-保存当前场景) | Ctrl+S 保存当前场景 | ~45m | P1 |
| [K7](#k7-outliner-方向键-节点导航) | Outliner 方向键 ↑↓ 节点导航 | ~45m | P2 |
| [P1](#p1-properties-属性搜索过滤框) | Properties 属性搜索过滤框 | ~30m | P1 |
| [P2](#p2-properties-每属性-reset-to-default-按钮) | Properties 每属性 reset-to-default 按钮 | ~45m | P2 |
| [Known Issue: A1 carry-over](#known-issue-a1-save-scene-as-文件对话框) | Save Scene As 文件对话框(deferred 第三次) | ~30m | (deferred) |

---

## K1. Delete 键删除选中节点

**优先级**: P0  **工时**: ~30m  **风险**: 低

### 背景
`DeleteNodesCommand` ([`src/Runtime/Command/DeleteNodesCommand.hpp`](../src/Runtime/Command/DeleteNodesCommand.hpp))已实现且具备 Undo/Redo,目前仅在 AI/JS 脚本路径([`EditorScriptExecutor.cpp:492`](../src/Editor/AI/EditorScriptExecutor.cpp), [`:1002`](../src/Editor/AI/EditorScriptExecutor.cpp))被调用。UI 层 [`EditorMain.cpp:150-178`](../src/Editor/EditorMain.cpp) `OnKey` 已处理 `Esc` 与 `F`,模式清晰,只缺 `Delete`。

### TODO

- [ ] 在 [`EditorMain.cpp:155`](../src/Editor/EditorMain.cpp) `switch(event.key.key)` 中追加 `case SDLK_DELETE:` 分支
- [ ] 取出当前选中:`std::vector<uint32_t> ids = GetEngine().GetScene().GetSelectedIds();`
- [ ] 空选中或 `ids.empty()` → 直接 `break;`(不报错)
- [ ] 否则:`auto cmd = std::make_unique<DeleteNodesCommand>(GetEngine().GetScene(), std::move(ids)); GetEngine().ExecuteCommand(std::move(cmd));`
- [ ] include `Runtime/Command/DeleteNodesCommand.hpp`
- [ ] **不要**绕开 CommandHistory 直接 `Scene::Remove*` — 必须保留 Undo
- [ ] 可选:macOS 上同时支持 `SDLK_BACKSPACE`(Mac 习惯),通过 `case SDLK_BACKSPACE:` fall-through 到 Delete 分支

### 涉及文件
- `src/Editor/EditorMain.cpp` (主改)

### 验收方法
1. 构建: `./build.bat --preset full-windows`,无新增警告
2. 启动 `gkNextEditor`,加载任意场景
3. Outliner 选中 1 个节点 → 按 `Delete` → 节点消失,视口同步更新
4. 多选 (Ctrl+点击) 2-3 个节点 → 按 `Delete` → 全部删除
5. `Ctrl+Z` → 全部还原,选中关系恢复
6. 空选中状态按 `Delete`,无错误日志、无崩溃
7. CommandHistory 面板里能看到对应的 "Delete N nodes" 项

### 注意
- 输入框聚焦时(例如 Properties 文本字段),`event.key.key` 仍会触发 — `OnKey` 是全局派发。检查 [`EditorMain.cpp:153`](../src/Editor/EditorMain.cpp) `modelViewController_.OnKey` 之后是否已在文本输入态返回。如果没有,**不**在本任务里改全局过滤逻辑;先靠 `ImGui::GetIO().WantTextInput` 在 Delete 分支内提前 `break;`
- 不要把 Delete 写成全局 ImGui shortcut(`ImGui::Shortcut`)— 与 `OnKey` 路径分两套会乱
- 删除指令对子树/锁定节点的处理已在 `DeleteNodesCommand` 内部 — 本任务**不需要**额外校验

---

## K2. Ctrl+D 复制选中节点

**优先级**: P0  **工时**: ~30m  **风险**: 低

### 背景
`DuplicateNodesCommand` ([`src/Runtime/Command/DuplicateNodesCommand.hpp`](../src/Runtime/Command/DuplicateNodesCommand.hpp)) 已实现并能 Undo,目前同样只在 AI/JS 路径 ([`EditorScriptExecutor.cpp:510`](../src/Editor/AI/EditorScriptExecutor.cpp), [`:1023`](../src/Editor/AI/EditorScriptExecutor.cpp)) 被使用。`Ctrl+D` 是绝大多数编辑器/IDE 的复制选中物习惯。

### TODO

- [ ] 在 [`EditorMain.cpp:155`](../src/Editor/EditorMain.cpp) `switch(event.key.key)` 中追加 `case SDLK_D:` 分支
- [ ] 检查 `event.key.mod & SDL_KMOD_CTRL`(Windows/Linux)或 `SDL_KMOD_GUI`(macOS Cmd)— 都满足才进入逻辑
- [ ] 不满足修饰键则 `break;`(避免影响 W/A/S/D 相机移动)
- [ ] 同 K1 模式:取选中,空则 `break;`,否则构造 `DuplicateNodesCommand` 并 `ExecuteCommand`
- [ ] include `Runtime/Command/DuplicateNodesCommand.hpp`
- [ ] 复制完成后,新节点应自动成为选中(命令内部已处理;**不要**在本任务里手动 SelectId,避免双重选中)

### 涉及文件
- `src/Editor/EditorMain.cpp`

### 验收方法
1. 构建通过
2. 选中一个 cube → `Ctrl+D` → 视口出现重叠的副本节点;Outliner 多一项,选中切到副本
3. 拖动 gizmo 移动副本,原 cube 不动
4. `Ctrl+Z` → 副本消失,选中回到原节点
5. 多选 2 个节点 `Ctrl+D` → 一次产生两个副本,均被选中
6. `Ctrl+D` 在文本输入态(Properties 编辑文本字段)**不应**触发复制

### 注意
- 同 K1,在文本输入态(`io.WantTextInput`)提前 `break;`
- macOS 用 `SDL_KMOD_GUI`(Cmd 键) — 用 `(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))` 一起判断即可
- `WASDQE` 相机移动仅在右键按下时激活 ([`GizmoController.cpp:138`](../src/Runtime/Editor/GizmoController.cpp)),与 `Ctrl+D` 不会冲突

---

## K3. Q 切换 Gizmo World/Local 空间

**优先级**: P0  **工时**: ~30m  **风险**: 低

### 背景
[`GizmoController::HandleShortcuts`](../src/Runtime/Editor/GizmoController.cpp) (L130-155) 已绑定 W/E/R 切换 Translate/Rotate/Scale,**`mode_` 字段(`ImGuizmo::LOCAL` / `WORLD`)初始化在 L124-127 后再无 UI 控制**。`DrawToolbar` (L170-218) 也只有 Move/Rotate/Scale + Pivot/Bounds 两组,缺 World/Local。Q 是行业标准切换键(Maya/Blender)。

### TODO

- [ ] 在 [`GizmoController.cpp:155`](../src/Runtime/Editor/GizmoController.cpp) `HandleShortcuts` 末尾追加:
  ```cpp
  if (ImGui::IsKeyPressed(ImGuiKey_Q))
  {
      mode_ = (mode_ == static_cast<int>(ImGuizmo::LOCAL))
                  ? static_cast<int>(ImGuizmo::WORLD)
                  : static_cast<int>(ImGuizmo::LOCAL);
  }
  ```
- [ ] 在 [`DrawToolbar`](../src/Runtime/Editor/GizmoController.cpp) 的 Pivot/Bounds 一组之后,加 `ImGui::TextUnformatted("|"); ImGui::SameLine();` 分隔,然后两个 RadioButton:
  - [ ] `Local`(`mode_ == ImGuizmo::LOCAL`)
  - [ ] `World`(`mode_ == ImGuizmo::WORLD`)
- [ ] 旋转 gizmo 在 Local/World 切换下视觉应有可见差异(把一个非轴对齐的物体先旋转 45°,再切 Local↔World 验证)

### 涉及文件
- `src/Runtime/Editor/GizmoController.cpp` (主改)
- `src/Runtime/Editor/GizmoController.hpp` (无需新接口,字段已有)

### 验收方法
1. 构建通过
2. 启动编辑器,选一物体并旋转 45°(让 Local/World 视觉差异明显)
3. 按 `W` 切平移,Toolbar 看到 Move 高亮
4. 按 `Q` → Toolbar 中 Local/World 切换,gizmo 轴朝向变化
5. 通过 Toolbar 直接点 `World` / `Local` 也能切,与 Q 完全等价
6. 文本输入态按 Q,**不**应改变 gizmo(`HandleShortcuts` 头部已 `WantTextInput` 拦截)

### 注意
- `ImGuizmo::LOCAL` / `ImGuizmo::WORLD` 是 ImGuizmo 枚举,直接用,**不要**自定义新枚举
- Scale 操作在 ImGuizmo 中**只支持 Local 模式** — World+Scale 会被 ImGuizmo 内部退回 Local。本任务**不需要**给用户额外提示,这是 ImGuizmo 已知行为
- 不要把 mode_ 改成 enum class,会牵动既有的 `static_cast<int>` 调用面;保持现状

---

## K4. Gizmo Toolbar hotkey tooltip

**优先级**: P1  **工时**: ~15m  **风险**: 极低

### 背景
[`DrawToolbar`](../src/Runtime/Editor/GizmoController.cpp) (L170-218) 渲染 Move/Rotate/Scale/Pivot/Bounds RadioButton,但**任何按钮都没有 tooltip**,新用户不知道有 W/E/R 快捷键。配合 K3 的 World/Local 与 Q 键,信息密度需要拉起来。

### TODO

- [ ] 给 6 个 RadioButton 各加一段 `if (ImGui::IsItemHovered()) { ImGui::SetTooltip("..."); }`(K3 完成后是 6 个,K3 前是 5 个 — 与 K3 顺序无关)
- [ ] tooltip 内容:
  - Move:`"Translate (W)"`
  - Rotate:`"Rotate (E)"`
  - Scale:`"Scale (R)"`
  - Pivot:`"Use individual pivot for each selected node"`
  - Bounds:`"Use combined selection bounds as pivot"`
  - Local:`"Local axes (Q)"`
  - World:`"World axes (Q)"`
- [ ] **不要**改 RadioButton label 文本本身

### 涉及文件
- `src/Runtime/Editor/GizmoController.cpp`

### 验收方法
1. 构建通过
2. 鼠标悬停每个 Toolbar 按钮 ≥0.5s,显示对应 tooltip
3. 按住按钮拖动鼠标,不会乱出 tooltip(`IsItemHovered` 默认行为正确)
4. 与 K3 的 Local/World 配合,悬停时也能看到 `(Q)` 提示

### 注意
- 优先用 `ImGui::SetItemTooltip("...")`(IMGUI 1.89+ 简写),若版本不支持回退到 `IsItemHovered + SetTooltip`
- tooltip 文本短,无需 `BeginTooltip/EndTooltip` 块

---

## K5. ViewportOverlay 显示 Gizmo 状态(Mode + Space)

**优先级**: P1  **工时**: ~20m  **风险**: 低

### 背景
当前 ViewportOverlay 通常只显示 FPS / 鼠标状态。用户切了 W/E/R 或 Q 之后,**没有视觉确认**当前是哪个 gizmo 操作 + 哪个空间。在视口角上加一行 `Translate · Local` 这种短状态,响应即时,极低成本极高 ROI。

### TODO

- [ ] 在 [`src/Editor/Panels/ViewportOverlay.cpp`](../src/Editor/Panels/ViewportOverlay.cpp) 找到现有的角标绘制处(FPS/状态文字附近)
- [ ] 通过 `ctx.engine.GetGizmoController()`(若 getter 不存在则添加;**或**通过现有的 GizmoController 引用路径,具体看 ViewportOverlay 已经能访问什么)取到 `operation_` 与 `mode_`
- [ ] 输出文字格式:`<OperationName> · <SpaceName>`
  - operation:`Translate` / `Rotate` / `Scale`
  - space:`Local` / `World`
- [ ] 用 `ImGuiCol_TextDisabled` 颜色,与 FPS 同一行或邻行均可,**不要**自创新窗口
- [ ] 选中为空时(`isShowing_ == false`)**不显示**这行(避免无操作对象时占位)

### 涉及文件
- `src/Editor/Panels/ViewportOverlay.cpp` (主改)
- `src/Runtime/Editor/GizmoController.hpp` (可能新增 const getter `Operation()` / `Mode()`)

### 验收方法
1. 构建通过
2. 选中节点,角标出现 `Translate · Local`
3. 按 `E` → 角标切到 `Rotate · Local`
4. 按 `Q` → 角标切到 `Rotate · World`
5. 取消选中(Esc) → 角标消失
6. 文字使用 disabled 颜色,不喧宾夺主

### 注意
- **不要**在角标里再加一组按钮 — 这是状态展示,不是控制面板。控制面板就是 K3 的 RadioButton
- 若现有 ViewportOverlay 没有 GizmoController 引用,采用「在 EditorContext 中暴露 GizmoController*」是干净选项;若 EditorContext 已有,直接用
- 不要把字符串映射写成 `if/else if` 长链 — 用一个静态数组或 `constexpr const char*` 数组

---

## K6. Ctrl+S 保存当前场景

**优先级**: P1  **工时**: ~45m  **风险**: 中(状态跟踪)

### 背景
当前唯一的保存路径是 `File > Save Scene As...`(还在 deferred,见 known issue),且**硬编码**保存到 `saved_scene.glb`。`Ctrl+S` 是基本习惯。要支持「保存到当前已打开的场景路径」需要新增**已加载场景路径**状态跟踪。

[`Engine.cpp:1467`](../src/Runtime/Engine.cpp) `RequestLoadScene` 是统一入口;[`Scene::Save(filename)`](../src/Assets/Core/Scene.cpp) 已存在。

### TODO

- [ ] 在 `EditorUiState` ([`EditorUiState.hpp`](../src/Editor/Core/EditorUiState.hpp)) 增加 `std::string currentScenePath;`(默认空)
- [ ] 在 [`EditorMain.cpp`](../src/Editor/EditorMain.cpp) 的 `IO_LoadScene` action 入口、`PushRecentScene` 之后,把 `args` 写入 `ui.currentScenePath = std::string(args);`(注意此处需要拿到 `EditorUiState&` — 通过 `EditorContext.ui`)
- [ ] 在 `OnKey` 加 `case SDLK_S:` 分支,要求 `Ctrl/Cmd` 修饰键,且 `WantTextInput` 不命中
- [ ] 行为:
  - [ ] 如果 `ui.currentScenePath` 非空且文件路径有效 → 直接 `ctx.scene.Save(ui.currentScenePath)`,记 `SPDLOG_INFO("Scene saved: {}", path)`
  - [ ] 否则 → `SPDLOG_INFO("No current scene path; use File > Save Scene As...");`(短期方案;A1 完成后改为弹出 Save As 对话框)
- [ ] 别忘了在 Recent Scenes 点击加载分支也写 `currentScenePath`(已统一走 IO_LoadScene 的话就免了)

### 涉及文件
- `src/Editor/Core/EditorUiState.hpp` (字段)
- `src/Editor/EditorMain.cpp` (action + OnKey)

### 验收方法
1. 构建通过
2. 启动后无场景路径,按 `Ctrl+S` → 日志 `No current scene path; use File > Save Scene As...`
3. 通过 `File > Open Scene...` 加载 `assets/models/kitchen.glb`
4. 在 Outliner 改名某节点(产生脏)
5. 按 `Ctrl+S` → 日志 `Scene saved: <绝对路径 to kitchen.glb>`,文件被覆写
6. 重启 → 用 Recent Scenes 加载该 glb,改名仍在
7. 在文本输入态按 `Ctrl+S` → 不触发保存

### 注意
- **不要**在没有 `currentScenePath` 时静默失败;必须 `SPDLOG_INFO` 提示
- 覆盖式保存有数据丢失风险 — `Scene::Save` 内部如果本身有备份 / 原子写,**不要**重复;若没有,本任务**也不**引入备份(超范围,单独立项)
- macOS 上 `SDL_KMOD_GUI` 是 Cmd;一并接受

---

## K7. Outliner 方向键 ↑↓ 节点导航

**优先级**: P2  **工时**: ~45m  **风险**: 中

### 背景
Outliner 当前只能鼠标点选;键盘只支持 F2 重命名 ([`OutlinerPanel.cpp:350`](../src/Editor/Panels/OutlinerPanel.cpp))。逐节点用键盘扫一遍场景树是常见调试需求。本任务只做 ↑↓(选上一项 / 下一项),不动展开/折叠(那是 ←→,留给后续批次)。

### TODO

- [ ] 在 [`OutlinerPanel.cpp`](../src/Editor/Panels/OutlinerPanel.cpp) `DrawOutlinerPanel` 主体内,在 `IsKeyPressed(F2)` 附近加 `↑/↓` 处理
- [ ] 仅当 Outliner 窗口被聚焦(`ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)`)时响应
- [ ] 实现一个本地静态 `std::vector<uint32_t> FlattenVisibleNodes(...)`,**只**收集当前 filter+expanded 状态下能看到的节点 ID(若实现复杂可简化为「按 Tree 顺序、忽略折叠」 — 接受 stretch 简化)
- [ ] 当前选中 → 在 flatten 列表里找索引,`↑` 取 idx-1,`↓` 取 idx+1,clamp 到 [0, size-1]
- [ ] 选中切换走 `ctx.scene.SetSelected(newId)`(单选 — Shift/Ctrl 多选留作 stretch)
- [ ] 同时设置 `pendingScrollTargetId = newId`,确保新选中能滚到可见

### 涉及文件
- `src/Editor/Panels/OutlinerPanel.cpp`

### 验收方法
1. 构建通过
2. 加载 `kitchen.glb`,在 Outliner 点选第一个节点
3. 按 ↓ 5 次 → 选中沿树向下移动 5 项,Properties 同步
4. 按 ↑ 3 次 → 回退 3 项
5. 在 filter 输入框输入字符 → 隐藏的节点不参与 ↑↓ 导航
6. 选中接近底部时按 ↓,**不**越界
7. 文本输入态按 ↑↓,**不**改变选中

### 注意
- 不要试图自己实现 Tree 折叠状态判定;若拿不到 expanded state,简化为「忽略折叠扁平化」(可接受)
- 不要在每帧都重建 flatten 列表 — 仅在 ↑↓ 触发那一帧重建即可
- ImGui 自身的 Tree 焦点导航(Tab 等)**不要**关闭;它们与 ↑↓ 不冲突

---

## P1. Properties 属性搜索过滤框

**优先级**: P1  **工时**: ~30m  **风险**: 低

### 背景
Outliner 已有 `ImGuiTextFilter` 节点过滤(上批 B1 完成)。对带很多组件/属性的节点(例如 Skinned Mesh 一长串骨骼参数),Properties 面板里找特定属性也得滚很远。同款 filter 可一并解决。

### TODO

- [ ] 在 [`PropertiesPanel.cpp`](../src/Editor/Panels/PropertiesPanel.cpp) 函数顶部、`ImGui::Begin("Properties", nullptr);` 之后加一个 `static ImGuiTextFilter propertyFilter;`(或挂到 `EditorUiState`,与 Outliner 风格一致)
- [ ] `propertyFilter.Draw(ICON_FA_MAGNIFYING_GLASS " Filter", 220.0f);`
- [ ] 在 `PropertyWidgets` 渲染入口(每个属性行)前,如果 `propertyFilter.IsActive() && !propertyFilter.PassFilter(propertyName)`,则跳过该属性
- [ ] 如果整个 component 的属性全被过滤掉,**也**跳过 component 标题(避免显示空 collapsing header)

### 涉及文件
- `src/Editor/Panels/PropertiesPanel.cpp`(主改)
- `src/Editor/Panels/PropertyWidgets.cpp`(可能要在迭代属性的循环里读 filter)
- (可选)`src/Editor/Core/EditorUiState.hpp` 持久化 filter

### 验收方法
1. 构建通过
2. 加载场景,选一个有多组件的节点(SkinnedMesh + Render + Physics)
3. Properties 顶部输入 `pos` → 只剩 `Position` 等字段;输入 `mat` → 只剩 Material/Metallic 等
4. 清空 filter → 完整属性树恢复
5. 当 filter 为空时,本任务**完全等价**于现有渲染(无回归)
6. 切到不同节点,filter 字符串保持(static 行为)

### 注意
- `ImGuiTextFilter::PassFilter` 不区分大小写,符合期望
- 不要破坏「点击属性,光标定位输入」的 UX
- 复合属性(`Transform → Position → x/y/z`)的 filter 粒度:能匹配「整组属性名」即可,**不要**细到每个分量,过度复杂化

---

## P2. Properties 每属性 reset-to-default 按钮

**优先级**: P2  **工时**: ~45m  **风险**: 中(reflection 集成)

### 背景
当前用户改了某属性(例如 metallic 从 0 改到 0.7),想恢复默认只能 Ctrl+Z 回退或手动改回 — 没有「这一项还原默认」的最小动作。Reflection 系统(`entt::meta`)已能产生默认值;给每行属性加一个小 `↺` 按钮即可。

### TODO

- [ ] 在 [`PropertyWidgets.cpp`](../src/Editor/Panels/PropertyWidgets.cpp) 渲染单个属性行的位置:
  - [ ] 在属性 widget 之后 `ImGui::SameLine();`
  - [ ] 用 `ImGui::SmallButton(ICON_FA_ROTATE_LEFT)` 或类似紧凑图标按钮
  - [ ] 鼠标悬停 tooltip:`"Reset to default"`
  - [ ] 点击时:从 reflection 拿到默认值(参考 entt::meta 工厂或新建一个临时实例的对应字段),通过命令系统 `SetPropertyCommand`(或现有等价命令)写回
- [ ] 若该属性当前值已等于默认,按钮置 disabled (或灰色),不接受点击
- [ ] **不要**绕开 CommandHistory — 必须可 Undo
- [ ] 复合类型(`glm::vec3` 等)整体 reset(一次写回 (0,0,0) 或类型默认),**不**做逐分量

### 涉及文件
- `src/Editor/Panels/PropertyWidgets.cpp`(主改)
- 可能 `src/Editor/Panels/PropertyWidgets.h`
- `src/Runtime/Reflection/`(只读;若没有公开 default-value getter,可能需要补一个小 helper,**注意不要扩散到无关组件的 reflection 注册**)

### 验收方法
1. 构建通过
2. 选一节点,把 RenderComponent 的 `Visible` 取消(改为 false)
3. Properties 行尾出现 ↺ 按钮(高亮可点)
4. 点击 → 恢复 true,视口立即更新
5. Ctrl+Z 还能回到 false → 命令历史有记录
6. 没改过的属性,按钮 disabled
7. Transform 的 Position 从 (5,0,0) 改回 (0,0,0) 一次性完成,不会逐分量产生 3 条 undo

### 注意
- 默认值来源:**优先**用 `entt::meta` 中已注册的默认 ctor 创建临时实例,读对应字段。如果 reflection 无此能力,**降级**为「类型零值」(`{}` 默认初始化),并在任务报告中**显式说明**该简化
- 不要给所有属性都加按钮 — 嵌套在 collapsing header 内的 sub-property 也要加,但 collapsing header **本身**不加
- 若 `SetPropertyCommand` 不存在,使用「读旧值 → 写新值 → 包成 lambda command」的现有模式;参考其他 PropertyWidgets 修改路径

---

## Known Issue: A1 Save Scene As 文件对话框

**第三次 carry-over**(自第一批起)。工时 ~30m。

[`TitleBarOverlay.cpp:54-67`](../src/Editor/Overlays/TitleBarOverlay.cpp) 仍硬编码 `"saved_scene.glb"`。本批仍 deferred — K6 完成后,Ctrl+S 已可覆盖保存到「当前路径」,Save Scene As 的紧迫性反而下降。下一批可以将 A1 与「场景另存为副本」「导出 GLTF/GLB 选项」一起作为「场景 IO 完整化」迷你专题处理。

---

## 完成后的常规收尾

- 不要在代码里留 `// TODO` / `// FIXME`,除非明确标注下一项任务
- 不要新增 `.md` 文档,除非用户/计划文档明确要求
- `SPDLOG_INFO` 给最终成功路径,`SPDLOG_WARN` 给可恢复异常,`SPDLOG_ERROR` 给真正失败
- 提交前 `git status` 不应有未跟踪的中间文件(`out/`、临时截图等)
- 严格遵循 `AGENTS.md` 的「执行前确认」原则:**不要**做任务卡范围之外的「顺手清理」

## 任务挑选建议

按时间块从 P0 开始(快捷键 P0 三连最容易拿到「立刻好用」的反馈):
- **第 1 块(30m)**: K1 (Delete)
- **第 2 块(30m)**: K2 (Ctrl+D)
- **第 3 块(30m)**: K3 (Q + World/Local Toolbar)
- **第 4 块(15m)**: K4 (hotkey tooltip,搭在 K3 后面顺手)
- **第 5 块(20m)**: K5 (ViewportOverlay Gizmo 状态)
- 之后视精力挑 K6 / K7 / P1 / P2

K1 / K2 / K6 之间没有顺序依赖。K3 / K4 / K5 强烈建议**按顺序**做(同一块代码区域)。P1 / P2 完全独立,可单独承包。
