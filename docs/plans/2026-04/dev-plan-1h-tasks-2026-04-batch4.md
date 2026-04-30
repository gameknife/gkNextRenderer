# 1 小时任务开发计划 第四批 (2026-04, Showcase + Outliner 进阶 + CVar + 零碎清理)

## Context

前三批共 24 项任务已完成(其中 A1 Save Scene As 文件对话框继续 deferred,batch 5+ 处理)。本批继续以「<1h 自包含」节奏,用户指定方向为:

1. **Showcase 继续扩展**: 已有 GIBootcamp / MaterialShowcase / LightingShowcase / CameraShowcase。SkinnedMesh + 动画轨道、Physics 组件这两条管线尚无 procedural 演示,补上后所有核心组件都有视觉测试基线。
2. **Outliner 进阶**: 第三批 K7 加了 ↑↓ 导航,←/→ 折叠展开是天然下一步。`Ctrl+A` 全选 + 右键「Hide/Show All Children」补齐多选/批量操作。
3. **CVar / Console**: 当前 console 有 `cvar.list` / `cvar.help`,缺前缀模糊补全。
4. **零碎代码清理**: 重复常量(`kTitleBarHeight` 等)与 ContentBrowser 残留的空 lambda 处理。

精选 7 项,规模介于第二批(8)与极简之间。

## 使用方法

1. **挑任务**: 任选 1 项,逐项完成 TODO。
2. **遵循公共约束**: 命名/构建/平台规则全部沿用 [`AGENTS.md`](../../AGENTS.md)。
3. **构建 preset**: 验证一律使用 `full-*` preset。
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
| [SC1](#sc1-animationshowcase-procedural-场景) | AnimationShowcase procedural 场景 | ~1h | P1 |
| [SC2](#sc2-physicsshowcase-procedural-场景) | PhysicsShowcase procedural 场景 | ~45m | P1 |
| [OL1](#ol1-outliner---键盘折叠展开当前节点) | Outliner ←/→ 键盘折叠/展开当前节点 | ~30m | P1 |
| [OL2](#ol2-outliner-ctrla-全选filter-激活时仅全选可见) | Outliner Ctrl+A 全选(filter 激活时仅全选可见) | ~30m | P2 |
| [OL3](#ol3-outliner-右键菜单-hideshow-all-children) | Outliner 右键菜单 Hide/Show All Children | ~30m | P2 |
| [CV1](#cv1-console-cvar-模糊补全) | Console CVar 模糊补全 | ~45m | P1 |
| [CL1](#cl1-editor-零碎清理常量去重--contentbrowser-空-lambda) | Editor 零碎清理(常量去重 + ContentBrowser 空 lambda) | ~30m | P2 |
| [Known Issue: A1 carry-over](#known-issue-a1-save-scene-as-文件对话框) | Save Scene As 文件对话框(deferred 第四次) | ~30m | (deferred) |

---

## SC1. AnimationShowcase procedural 场景

**优先级**: P1  **工时**: ~1h  **风险**: 中(动画轨道接入)

### 背景
前两批补了 Material / Lighting / Camera 三个 procedural showcase,SkinnedMesh + 动画轨道这条管线还没有专门演示场景。`SceneList.cpp` 的 procedural builder 已经支持 `tracks` 参数(参见 `GIBootcamp` / `MaterialShowcase` 签名),只需要按既有模式装填。

### TODO

- [ ] 在 `src/Runtime/Scene/SceneList.cpp` 仿 `MaterialShowcase` 新增 `AnimationShowcase` 函数:
  - [ ] 1 个地面 (Lambertian gray)
  - [ ] 3 个并排几何体演示三种动画类型:
    - [ ] 旋转 cube(Y 轴持续旋转,周期约 4s)
    - [ ] 上下振荡 sphere(Position.y 正弦,幅度 ±0.5,周期 ~2s)
    - [ ] 缩放脉冲 cube(Scale 在 0.7~1.3 之间正弦,周期 ~3s)
  - [ ] 中性 HDRI(`HasSky = true; SkyIdx = 0`)
  - [ ] 相机正面对着三组,稍俯视
- [ ] 用现有 `tracks` 容器(参考 `GIBootcamp` 是否有动画轨道注入示范;若没有,沿用最简的 keyframe-based track)
- [ ] 在 `AllScenes.push_back("AnimationShowcase.proc");` + 加载分支添加,与上批 `LightingShowcase` 同一处
- [ ] 在 `assets/configs/visual_test.json` 加入新场景

### 涉及文件
- `src/Runtime/Scene/SceneList.cpp`(主改)
- `assets/configs/visual_test.json`

### 验收方法
1. 构建通过
2. 渲染器场景下拉看到 `AnimationShowcase.proc`
3. 加载后 3 个几何持续动画(旋转 / 振荡 / 脉冲)
4. 日志出现 `uploaded scene [AnimationShowcase.proc] to gpu`
5. VisualTest 截图能稳定生成(若动画时间不固定,加一个固定 captureTime 或在 `visual_test.json` 设 `defaultFramesToWait` 让某帧确定性)

### 注意
- 动画**确定性**对 visual baseline 是关键。优先方案:在 visual test 模式下用「固定时间种子」或「指定第 N 帧」的策略,**不要**强行让动画静止
- 不引入新贴图,完全 procedural
- 如果 `tracks` 接口对程序化场景不够友好,可降级方案:写一个只有几何 + 在 `OnTick` / 渲染前钩子中改 transform 的「半 procedural」演示。本任务**允许**这种简化,但要在报告中说明取舍
- 不要改动 SkinnedMeshComponent 注册流程 — 那超范围

---

## SC2. PhysicsShowcase procedural 场景

**优先级**: P1  **工时**: ~45m  **风险**: 中(物理决定性)

### 背景
PhysicsComponent 目前只在 CornellBox 出现一处。procedural 场景示例中没有动力学演示。一个简单 PhysicsShowcase:几个 rigid cubes 从一定高度落到地面,演示重力 + 碰撞。

### TODO

- [ ] 在 `src/Runtime/Scene/SceneList.cpp` 新增 `PhysicsShowcase` 函数:
  - [ ] 1 个静态地面 plane (Lambertian gray, 物理 type = Static)
  - [ ] 4-5 个不同尺寸的 cube,初始 y 高度 5~10,**带 PhysicsComponent**(motion type = Dynamic)
  - [ ] 1 个倾斜的 ramp(可选,让 cube 滚下;如果增加复杂度太高可省略)
  - [ ] 中性 HDRI + 1 盏方向光
  - [ ] 相机框住整个落体过程
- [ ] 在 `AllScenes` + 加载分支添加
- [ ] 在 `visual_test.json` 加入,**单独配置一个较长的 `sceneTimeouts` 值**(如 30s),并加一段注释说明物理非确定性

### 涉及文件
- `src/Runtime/Scene/SceneList.cpp`
- `assets/configs/visual_test.json`

### 验收方法
1. 构建通过
2. 加载场景,cubes 自由下落、堆叠、静止
3. 日志正常 `uploaded scene [...] to gpu`
4. 反复 reload 几次,基本物理表现一致(允许小抖动)
5. VisualTest 跑过(因为非决定性,**不强制要求** RMSE 低于阈值;只要能截图、不超时即可)

### 注意
- 物理决定性受 step count、PCG 等影响。本任务的 visual baseline **可标注为 informational**(报告不阻断 CI)
- 不要修改 NextPhysics 子系统的内部行为
- cube 尺寸不要过小(<0.3),容易因为接触阈值导致跳动
- 不要用 IES 灯或多面光源,影响调试关注点

---

## OL1. Outliner ←/→ 键盘折叠/展开当前节点

**优先级**: P1  **工时**: ~30m  **风险**: 低

### 背景
第三批 K7 添加了 Outliner ↑↓ 节点导航。完整的键盘 tree 操作还差 ←/→ 折叠/展开。ImGui Tree Node 的展开状态一般通过 `SetNextItemOpen(true/false, ImGuiCond_Always)` 控制,需要一个「这个节点本帧要切换 open 状态」的临时信号。

### TODO

- [ ] 在 `EditorUiState` 增加 `uint32_t pendingExpandTargetId = InvalidId;` 与 `uint32_t pendingCollapseTargetId = InvalidId;`(也可合并为一个 `enum class { None, Expand, Collapse } pendingExpandAction`,任选)
- [ ] 在 OutlinerPanel 的键盘处理段(K7 ↑↓ 旁边)增加:
  - [ ] `IsKeyPressed(ImGuiKey_RightArrow)`:取当前选中,写入 `pendingExpandTargetId = selectedId;`
  - [ ] `IsKeyPressed(ImGuiKey_LeftArrow)`:写入 `pendingCollapseTargetId = selectedId;`
  - [ ] 仅当 Outliner focused 且 `!WantTextInput` 时响应
- [ ] 在 `DrawNode` 渲染处:若当前 `node.GetInstanceId() == pendingExpand…` → `ImGui::SetNextItemOpen(true, ImGuiCond_Always);` 并清零 pending;collapse 同理 `false`
- [ ] 叶子节点(无子)按 → 应**无视**,不要报错

### 涉及文件
- `src/Editor/Core/EditorUiState.hpp`
- `src/Editor/Panels/OutlinerPanel.cpp`

### 验收方法
1. 构建通过
2. 加载有层级的场景(`kitchen.glb`),选中根节点
3. 按 → 节点展开;再按 → 进入下一层(实际是配合 K7 ↑↓ 一起测试整套导航)
4. 按 ← 折叠
5. 在叶子节点按 → / ←,无错误日志、无崩溃
6. 文本输入态按 ←/→,**不**触发折叠/展开

### 注意
- 与 K7 的 `pendingScrollTargetId` 是不同字段 — **不要**合并
- 不要试图在 `DrawNode` 里递归读 ImGui 内部 storage 判断 expanded — 用 SetNextItemOpen 的「下一帧强制」语义即可
- ImGui 自身的 Tab/Space 导航不要禁用

---

## OL2. Outliner Ctrl+A 全选(filter 激活时仅全选可见)

**优先级**: P2  **工时**: ~30m  **风险**: 低

### 背景
批量 Delete / 批量移动 / 批量复制等场景都需要先一键全选。配合 K1 (Delete) / K2 (Ctrl+D) 价值更大。filter 激活时只全选可见 — 这是用户期望的 mental model。

### TODO

- [ ] 在 OutlinerPanel 键盘处理段增加 `Ctrl/Cmd + A` 检测(`io.KeyCtrl || io.KeySuper` + `IsKeyPressed(ImGuiKey_A, false)`)
- [ ] 仅当 Outliner focused 且 `!WantTextInput` 时响应
- [ ] 行为:
  - [ ] filter 未激活:遍历 `scene.Nodes()`,收集所有 `instanceId`,`scene.SetSelectedIds(ids)`(若 API 名不一致用现有等价)
  - [ ] filter 激活:递归收集**通过 filter** 的节点 id(可复用 OL1 之前已有的 `PassesNodeFilter` 工具)
- [ ] 多选后,Properties 面板进入 multi-select 模式(已有逻辑,不需改)

### 涉及文件
- `src/Editor/Panels/OutlinerPanel.cpp`

### 验收方法
1. 构建通过
2. 加载场景,无 filter,Ctrl+A → Outliner 所有节点高亮选中,Properties 显示 "N Objects Selected"
3. filter 输入 "Cube" 后 Ctrl+A → 只选中匹配项
4. 在 filter 输入框中按 Ctrl+A → 触发 ImGui 默认行为(选中输入文字),**不**触发节点全选
5. Esc 取消全选恢复

### 注意
- 不要把全选写到命令历史 — 选中变化通常不入 Undo(参考现有点击选中的实现)
- 性能:大场景(>5000 节点)Ctrl+A 一次性 SetSelectedIds 应足够,不需要分块
- macOS 上 `io.KeySuper` 是 Cmd

---

## OL3. Outliner 右键菜单 Hide/Show All Children

**优先级**: P2  **工时**: ~30m  **风险**: 低

### 背景
第二批 B2 加了单节点眼睛图标切可见性。子树批量隐藏/显示是高频复合操作:把整棵建筑藏起来调相机、把所有 NPC 藏起来看 LOD,等等。挂在 Outliner 右键菜单(批次 2 E5 已加过 Copy Node Path,popup 是同一处)。

### TODO

- [ ] 在 OutlinerPanel `DrawNode` 的右键 popup 中(Copy Node Name / Copy Node Path 之后,加分隔)追加两项:
  - [ ] `Hide All Children`:递归该节点的所有 descendants,凡有 RenderComponent 的设 `SetVisible(false)`
  - [ ] `Show All Children`:同上,设 `true`
- [ ] 实现一个本地 `void SetSubtreeVisibility(Assets::Node& root, bool visible, Assets::Scene& scene)`,DFS 子树
- [ ] 操作完成后 `scene.MarkDirty()`(批次 2 B2 同款)
- [ ] **不**改自身节点的可见性(用户用眼睛图标切自身;子树批量是另一个动作)
- [ ] 不入命令历史(本任务保持简单),报告中可注明「stretch:接入命令系统」

### 涉及文件
- `src/Editor/Panels/OutlinerPanel.cpp`

### 验收方法
1. 构建通过
2. 加载有层级的场景,在父节点右键 → `Hide All Children` → 视口该节点的所有 descendants 消失,父节点本身仍可见
3. `Show All Children` → 全部恢复
4. 没有 RenderComponent 的子节点(纯 group),操作不影响,无错误
5. 叶子节点上点该项,无效果(无 children),无崩溃

### 注意
- 不要用「ImGui 的拷贝当前选中状态再批量改」的间接路径 — 直接走 component API 最干净
- DFS 时小心循环引用(理论上场景树是 DAG / Tree,但加个 visited set 防御一下也无妨;若 GetChildren 返回的是 owning 引用,可省略)
- 不要在 popup 关闭那一帧之外的时机调用 — 与 K1 `Delete` 保持同步语义

---

## CV1. Console CVar 模糊补全

**优先级**: P1  **工时**: ~45m  **风险**: 低

### 背景
项目内有 100+ CVar(根据探索结果,具体数字不重要)。现有 console 命令有 `cvar.list` / `cvar.help`,但**没有前缀/模糊补全**。用户输入 `bloom` 不知道有哪些相关 CVar — 必须 `cvar.list` 全部刷一遍肉眼搜。加一个 `cvar.complete <prefix>` 命令是最低成本。Stretch:在 console 输入框 inline 补全,但成本明显更高,留作后续。

### TODO

- [ ] 在 CVar 系统的命令注册处(`src/Runtime/Config/CVarSystem.cpp` 或同级文件)新增一个命令 `cvar.complete`:
  - [ ] 接收一个参数 `prefix`(可空)
  - [ ] 收集所有已注册 CVar 名
  - [ ] 优先返回**前缀匹配**(case-insensitive),按字典序排序
  - [ ] 若前缀匹配为零,降级为**子串匹配**
  - [ ] 输出最多 20 条(防刷屏),超过则末尾追加 `... (N more, refine prefix)`
- [ ] 复用 `cvar.list` 的输出 sink(SPDLOG_INFO 或 console echo,**不要**新建输出通道)
- [ ] 在 `cvar.help` 的描述里追加一行 "Use cvar.complete <prefix> for fuzzy lookup"

### 涉及文件
- `src/Runtime/Config/CVarSystem.cpp`(主改;若命令注册在别处,定位实际文件)
- `src/Runtime/Config/CVarSystem.hpp`(若需要 declaration)

### 验收方法
1. 构建通过
2. 启动编辑器,在 footer console 输入 `cvar.complete` → 列出前 20 个 CVar
3. `cvar.complete bloom` → 仅列含 bloom 的(若有)
4. `cvar.complete xyzqwerty`(不存在)→ 输出 `(no matches)` 或类似
5. 大写小写不敏感:`cvar.complete BLOOM` 与 `bloom` 等价

### 注意
- 不要引入 fzf/levenshtein 之类的重补全算法 — 前缀+子串足够
- 不要把补全逻辑塞进 `cvar.list` 内部加分支(参数解析复杂化);用独立命令更清晰
- macOS 终端宽字符不要花哨格式化;纯 plain text + 换行

---

## CL1. Editor 零碎清理(常量去重 + ContentBrowser 空 lambda)

**优先级**: P2  **工时**: ~30m  **风险**: 极低

### 背景
两件小事打包:

1. **重复常量**:`kTitleBarHeight = 55.0f` 至少在 `TitleBarOverlay.cpp` 与 `EditorInterface.cpp` 各定义一次,数值未来若需调整存在双重维护。
2. **ContentBrowser 空 lambda**:Model browser 与 Texture browser 注册了 `onDoubleClick = []() {}` 的空 lambda(material browser 有真实 handler)。当前等价于「点击后什么也不做但消耗一次双击事件」。要么删除赋值让回调真正为空(被 caller 检查 `if (cb)`),要么显式说明 / 接入最小行为(选中 + 输出 SPDLOG_INFO)。

### TODO

- [ ] 新建 `src/Editor/Core/EditorLayoutConstants.hpp`(纯 header,首行 `#pragma once` + 包含 `Common/CoreMinimal.hpp`)
  - [ ] `constexpr float kTitleBarHeight = 55.0f;`
  - [ ] `constexpr float kFooterHeight = 40.0f;`
  - [ ] (其他探索时发现的双定义常量也一并搬过来,但**不要**为了清理而扩散到无关代码)
- [ ] `TitleBarOverlay.cpp` 与 `EditorInterface.cpp` 删除本地 `constexpr float kTitleBarHeight = 55.0f` 等定义,改 include 新 header
- [ ] `ContentBrowserPanel.cpp` 模型/纹理浏览器:
  - [ ] **首选方案**:把 `onDoubleClick = []() {}` 改成 **不赋值**(让默认 `std::function` 为空),caller 已经在 `if (cb.onDoubleClick)` 才调用 → 行为不变,但代码语义清楚
  - [ ] 若现有 caller 没有空检查,**降级方案**:接入「选中该资产 + SPDLOG_INFO("Selected model: {}", name)」,作为占位行为
  - [ ] 报告中说明选了哪种

### 涉及文件
- `src/Editor/Core/EditorLayoutConstants.hpp`(新增)
- `src/Editor/Overlays/TitleBarOverlay.cpp`
- `src/Editor/EditorInterface.cpp`
- `src/Editor/Panels/ContentBrowserPanel.cpp`

### 验收方法
1. 构建通过,无新增警告
2. TitleBar 高度、Footer 高度视觉**完全无变化**(纯重构)
3. 内容浏览器双击 model / texture 资产,行为不变(或新增日志)
4. `git grep "constexpr float kTitleBarHeight"` 仅命中 `EditorLayoutConstants.hpp` 一处

### 注意
- **不要**把 EditorLayoutConstants.hpp 弄成「所有常量大杂烩」 — 只放与 Editor layout 强相关的几何尺寸
- 不要把 ContentBrowser 的双击行为改成会触发场景加载 / 重定向到 viewport 的复杂行为(超范围)
- 这种 PR 应该是 90% 重构 + 10% 行为微调,review 应能秒过

---

## Known Issue: A1 Save Scene As 文件对话框

**第四次 carry-over**(自第一批起)。工时 ~30m。

`TitleBarOverlay.cpp` `Save Scene As` 仍硬编码 `"saved_scene.glb"`。批次 3 K6 完成后 Ctrl+S 已能覆盖最常用路径。下一批考虑做「**场景 IO 完整化迷你专题**」,把 A1 + Save As Copy + Export GLTF/GLB 选择 + 拖入文件加载 + 脏标记保存提示 一次打包做掉(预计 4-5 项小任务)。

---

## 完成后的常规收尾

- 不要在代码里留 `// TODO` / `// FIXME`,除非明确标注下一项任务
- 不要新增 `.md` 文档,除非用户/计划文档明确要求
- `SPDLOG_INFO` 给最终成功路径,`SPDLOG_WARN` 给可恢复异常,`SPDLOG_ERROR` 给真正失败
- 提交前 `git status` 不应有未跟踪的中间文件(`out/`、临时截图等)
- 严格遵循 `AGENTS.md` 的「执行前确认」原则:**不要**做任务卡范围之外的「顺手清理」

## 任务挑选建议

按时间块从 P1 开始:
- **第 1 块(45m)**: CV1 (CVar 补全) — 与渲染/Editor 完全解耦,review 风险最低
- **第 2 块(30m)**: CL1 (零碎清理) — 纯重构,搭车顺手
- **第 3 块(30m)**: OL1 (←/→) — 与上批 K7 ↑↓ 强配套
- **第 4 块(30m+30m)**: OL2 + OL3 (全选 + Hide/Show Children) — 同一文件邻近代码区
- **第 5 块(45m+1h)**: SC2 (Physics) → SC1 (Animation) — 大块时间留给 procedural 场景

OL1 / OL2 / OL3 之间可以同一 PR 合并,也可以分。SC1 / SC2 完全独立。

