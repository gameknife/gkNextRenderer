# 1 小时任务开发计划 (2026-04)

本文档面向 **Claude Code / Codex** 等编码代理,列出当前阶段可在 1 小时以内闭环的小块任务。每个任务自包含,可独立挑选执行。

## 使用方法

1. **挑任务**: 从下方任务清单中选 1 项,逐项完成 TODO。
2. **遵循公共约束**: 所有任务必须遵循 [`AGENTS.md`](../../AGENTS.md) 的命名/构建/平台规则。
3. **完成判定**: 必须满足任务卡的 "验收方法" 中的全部条目。
4. **构建 preset**: 验证一律使用 `full-*` preset (`full-windows` / `full-macos-arm64` / `full-linux`),与 `AGENTS.md` 一致。
5. **报告**: 完成后简述「改了哪些文件、测了什么、看到的输出」。**不要**总结代码意图,代码自己会说话。

## 公共上下文(快速参考)

| 项 | 值 |
|---|---|
| 构建 | `./build.bat --preset full-windows` / `./build.sh --preset full-macos-arm64` |
| 运行编辑器 | `./run.bat --preset full-windows --target gkNextEditor.exe` |
| 运行渲染器 | `./run.bat --preset full-windows --target gkNextRenderer.exe` |
| 运行单元测试 | `./out/build/<preset>/bin/gkNextUnitTests` |
| 运行成功标志 | 日志出现 `uploaded scene [...] to gpu` |
| 命名规范 | 类型/函数 PascalCase;变量/参数 camelCase;私有成员 trailing `_`;详见 `.clang-tidy` |
| 头文件首选 | 新文件首先 `#include "Common/CoreMinimal.hpp"` |

> 提示:**禁止**修改 `src/ThirdParty/` 与 `external/`。

---

## 任务索引

| # | 标题 | 工时 | 优先级 |
|---|---|---|---|
| [A1](#a1-save-scene-as-接入-sdl-文件对话框) | Save Scene As 接入 SDL 文件对话框 | ~45m | P0 |
| [A2](#a2-编辑器-hdri-双击切换环境贴图) | 编辑器 HDRI 双击切换环境贴图 | ~45m | P0 |
| [A3](#a3-layout--reset-菜单接入-dockbuilder-重置) | Layout > Reset 菜单接入 DockBuilder 重置 | ~30m | P1 |
| [B1](#b1-outliner-节点过滤搜索框) | Outliner 节点过滤搜索框 | ~45m | P1 |
| [B2](#b2-outliner-节点可见性切换) | Outliner 节点可见性切换 (眼睛图标) | ~1h | P1 |
| [B3](#b3-内容浏览器扩展更多文件类型图标) | 内容浏览器扩展更多文件类型图标 | ~30m | P2 |
| [B4](#b4-内容浏览器目录条目缓存--手动刷新按钮) | 内容浏览器目录条目缓存 + 手动刷新按钮 | ~1h | P2 |
| [C1](#c1-新增-procedural-material-showcase-场景) | 新增 procedural Material Showcase 场景 | ~45m | P2 |
| [D1](#d1-visualtest-输出与基线像素差异统计-stretch) | VisualTest 输出与基线像素差异统计 (stretch) | ~1.5h | P3 |

---

## A1. Save Scene As 接入 SDL 文件对话框

**优先级**: P0  **工时**: ~45m  **风险**: 低

### 背景
编辑器 `File > Save Scene As...` 菜单项目前把保存路径硬编码为 `"saved_scene.glb"`,无法选择目录或文件名。SDL3 提供了原生 `SDL_ShowSaveFileDialog`,且项目内已有 `SDL_ShowOpenFileDialog` 调用样板可参考。

### TODO

- [ ] 阅读 [`BrickPlayerGameInstance.cpp:2264`](../../src/Application/BrickPlayer/BrickPlayerGameInstance.cpp) 的 `OpenFileDialog` 写法,了解项目内 SDL 对话框 callback 模式
- [ ] 修改 [`TitleBarOverlay.cpp:51-65`](../../src/Editor/Overlays/TitleBarOverlay.cpp),把硬编码替换为 `SDL_ShowSaveFileDialog`
- [ ] Filter 至少包含 `.glb`(主要)与 `*`(全部)两项
- [ ] callback 中调用 `ctx.scene.Save(filelist[0])`,沿用现有的 `SPDLOG_INFO/SPDLOG_ERROR` 日志
- [ ] 由于 `TitleBarOverlay` 是 namespace 函数,无法捕获 `this`;考虑通过 `userdata` 把 `Scene*` 传过去,或者把 SDL 调用包装成 `Engine` 上的方法暴露
- [ ] 删除原 TODO 注释 `// TODO: Add file dialog for save path selection`

### 涉及文件
- `src/Editor/Overlays/TitleBarOverlay.cpp` (主改)
- `src/Editor/EditorContext.hpp` 或 `src/Runtime/Engine.hpp` (可能新增辅助方法,见上)

### 验收方法
1. 构建: `./build.bat --preset full-windows --reconfigure`,无新增警告/错误
2. 运行 `gkNextEditor`,点击 `File > Save Scene As...`,弹出原生保存对话框
3. 选择路径并确认,日志出现 `Scene saved successfully: <你选的路径>`
4. 用文件管理器确认 `.glb` 文件已生成在所选位置
5. 取消对话框时,不输出错误日志,无崩溃

### 注意
- SDL 文件对话框为**异步**回调,UI 线程不会阻塞,callback 在主循环驱动下被回调
- 不要引入新依赖;SDL3 已是项目核心依赖

---

## A2. 编辑器 HDRI 双击切换环境贴图

**优先级**: P0  **工时**: ~45m  **风险**: 低

### 背景
内容浏览器双击 `.hdr` 文件已经派发 `EEditorAction::IO_LoadHDRI` ([`ContentBrowserPanel.cpp:394`](../../src/Editor/Panels/ContentBrowserPanel.cpp)),但 action 在 [`EditorMain.cpp:84-89`](../../src/Editor/EditorMain.cpp) 里是空 lambda,标着 TODO。引擎层的拖入处理 [`Engine.cpp:1434-1439`](../../src/Runtime/Engine.cpp) 已经实现完整逻辑,直接复用即可。

### TODO

- [ ] 在 [`EditorMain.cpp:84-89`](../../src/Editor/EditorMain.cpp) 的 `IO_LoadHDRI` lambda 里:
  - [ ] 调用 `Assets::GlobalTexturePool::GetInstance()->LoadHDRTexture(std::string(args))` 获取 textureId
  - [ ] 写入 `ctx.scene.GetEnvSettings().SkyIdx = textureId`
  - [ ] 用 `SPDLOG_INFO` 输出加载结果
- [ ] 删除该处 TODO 注释
- [ ] 验证当 `SkyIdx` 改变时渲染管线确实采样新的天空贴图(参考 `gkNextRenderer.cpp:668` 已暴露的 `SkyIdx` slider)

### 涉及文件
- `src/Editor/EditorMain.cpp`

### 验收方法
1. 构建通过
2. 运行 `gkNextEditor`,在内容浏览器找一个 `.hdr` 文件(如 `assets/textures/std_env.hdr`)
3. 双击,日志出现 HDRI 加载信息
4. 视口立即可见环境光/天空变化
5. 重复双击不同 HDRI,环境每次都跟着切换;无内存泄漏报错

### 注意
- `SkyIdx` 必须是 `LoadHDRTexture` 返回的最新 id,**不要**写常量
- 若所选 `.hdr` 加载失败,应记录 `SPDLOG_ERROR` 并保持原 `SkyIdx` 不变

---

## A3. Layout > Reset 菜单接入 DockBuilder 重置

**优先级**: P1  **工时**: ~30m  **风险**: 低

### 背景
[`TitleBarOverlay.cpp:101-105`](../../src/Editor/Overlays/TitleBarOverlay.cpp) `Layout > Reset` 菜单项是空操作。DockBuilder 默认布局已在 [`EditorInterface.cpp:251-267`](../../src/Editor/EditorInterface.cpp) 的 `firstRun_` 块内构造好,把它抽出为函数 + 加一个重置触发即可。

### TODO

- [ ] 在 `EditorInterface` 增加 `void RebuildDefaultDockLayout(ImGuiID id)`,把现有 `firstRun_` 块的 `DockBuilderSplitNode/DockWindow/Finish` 调用搬进去
- [ ] `firstRun_` 路径改为调用该函数
- [ ] 在 `EditorUiState` 增加 `bool dockResetRequested = false;` 标志
- [ ] `Render` 中:若 `dockResetRequested` 为真,调用 `RebuildDefaultDockLayout(id)` 后清零标志
- [ ] [`TitleBarOverlay.cpp:103`](../../src/Editor/Overlays/TitleBarOverlay.cpp) `Layout > Reset` 菜单项:`ui.dockResetRequested = true;`

### 涉及文件
- `src/Editor/EditorInterface.cpp` / `.hpp`
- `src/Editor/Core/EditorUiState.hpp`
- `src/Editor/Overlays/TitleBarOverlay.cpp`

### 验收方法
1. 构建通过
2. 运行 `gkNextEditor`,手动拖动若干面板打乱布局
3. 点 `Edit > Layout > Reset`,所有面板回到默认位置(Outliner 在左,Properties 在右,内容/日志在下)
4. 重复 3-5 次,稳定无崩溃,无 ImGui 错误

### 注意
- `DockBuilderRemoveNode` 后再 `DockBuilderAddNode`,然后再 split,顺序与 ImGui docking 示例一致
- 不要在 `Render` 主流程外调用 DockBuilder API;它必须在 BeginFrame/EndFrame 之间

---

## B1. Outliner 节点过滤搜索框

**优先级**: P1  **工时**: ~45m  **风险**: 低

### 背景
[`OutlinerPanel.cpp:218-233`](../../src/Editor/Panels/OutlinerPanel.cpp) 用 `limit = 1000` 做硬截断,场景节点超过 1000 时无法选中后面的节点。加一个 `ImGuiTextFilter` 后,既能搜索特定节点,也能避开硬截断。

### TODO

- [ ] 在 `DrawOutlinerPanel` 内增加一个 `static ImGuiTextFilter` 过滤器(或挂在 `EditorUiState`)
- [ ] 在 Outliner 顶部 (Auto Scroll 按钮旁) 渲染 `filter.Draw(ICON_FA_MAGNIFYING_GLASS, 200.0f)`
- [ ] 修改 `DrawNode` 递归:若 `filter.IsActive()` 且 `!filter.PassFilter(node.GetName().c_str())` 且**所有后代**都不通过,则跳过该节点
- [ ] 当过滤激活时,移除 1000 节点硬截断(保持原硬截断仅用于无过滤场景)
- [ ] 父节点匹配时,展示其所有匹配的后代;后代匹配时,自动展开父节点路径

### 涉及文件
- `src/Editor/Panels/OutlinerPanel.cpp`
- (可选) `src/Editor/Core/EditorUiState.hpp` 用于持久化过滤字符串

### 验收方法
1. 构建通过
2. 加载有大量节点的场景(如 `playground.glb` / `kitchen.glb`)
3. 在过滤框输入字符,Outliner 仅显示匹配节点(及其所需的父级路径)
4. 清空过滤,Outliner 恢复完整层级
5. 过滤激活时,可定位到原本被 1000 截断的节点

### 注意
- `ImGuiTextFilter::PassFilter` 不区分大小写,符合期望
- 过滤激活时不要破坏 `pendingScrollTargetId` 的自动滚动逻辑

---

## B2. Outliner 节点可见性切换

**优先级**: P1  **工时**: ~1h  **风险**: 低

### 背景
`RenderComponent::SetVisible(bool)` / `GetVisible()` 在 [`RenderComponent.h:27`](../../src/Runtime/Components/RenderComponent.h) 已就绪,反射也已暴露 `"Visible"` 属性。Outliner 行内增加一个眼睛图标按钮,即可一键切换。

### TODO

- [ ] 修改 [`OutlinerPanel.cpp:69-72`](../../src/Editor/Panels/OutlinerPanel.cpp) 的 label 构造:
  - [ ] 在节点名称右侧或左侧追加一个 `ICON_FA_EYE` / `ICON_FA_EYE_SLASH` 小按钮
  - [ ] 点击切换 `RenderComponent::SetVisible`
  - [ ] 该点击不应触发节点选中/取消选中逻辑
- [ ] 没有 `RenderComponent` 的节点,该按钮**不显示**
- [ ] 隐藏的节点在 Outliner 中文字使用 `ImGuiCol_TextDisabled` 颜色
- [ ] 切换写入 `Scene::MarkDirty()` 触发渲染脏标记

### 涉及文件
- `src/Editor/Panels/OutlinerPanel.cpp`

### 验收方法
1. 构建通过
2. 加载任意带物件的场景
3. 选一个节点,点击眼睛图标 → 视口该物件消失,Outliner 文字变灰
4. 再次点击 → 物件出现,文字恢复
5. 撤销/重做(若挂到命令系统)正常工作 (**可选 stretch**: 接入 `CommandHistory`)

### 注意
- 优先实现直接切换 (不入命令历史) 以保持工时;接入 `CommandHistory` 留作 stretch
- `ImGui::SmallButton` 配合 `ImGui::SameLine` 即可实现紧凑布局
- 注意按钮的 `PushID`,避免多个节点的眼睛按钮 ID 冲突

---

## B3. 内容浏览器扩展更多文件类型图标

**优先级**: P2  **工时**: ~30m  **风险**: 极低

### 背景
[`ContentBrowserPanel.cpp:89-91`](../../src/Editor/Panels/ContentBrowserPanel.cpp) 的 `kVisuals` 数组只识别 `.hdr`。其他常见资源(`.js` 脚本、`.png/.jpg` 纹理、`.ldr/.mpd` LDraw、`.glb` 已通过 SceneList 走 Scene 通道) 都被归为 Unsupported 而**不显示**。

### TODO

- [ ] 在 `kVisuals` 数组追加(选合适的 fontawesome 图标和颜色):
  - [ ] `.js` → `ICON_FA_CODE`(脚本)
  - [ ] `.png` / `.jpg` / `.jpeg` / `.tga` → `ICON_FA_IMAGE`(纹理)
  - [ ] `.ldr` / `.mpd` → `ICON_FA_CUBES`(LDraw)
  - [ ] `.json` → `ICON_FA_FILE_LINES`(配置)
- [ ] 给每类新增对应的 `EContentAssetKind` 枚举值;暂时双击行为可留空(只显示),避免膨胀工时
- [ ] 确保现有 Scene/Hdri 双击逻辑不受影响

### 涉及文件
- `src/Editor/Panels/ContentBrowserPanel.cpp`

### 验收方法
1. 构建通过
2. 内容浏览器中,定位到 `assets/typescript/`、`assets/textures/`、`assets/configs/` 等目录
3. 看到新增类型的文件以**对应图标和颜色**显示,而不是被隐藏
4. Scene/HDRI 双击行为照旧

### 注意
- `IconsFontAwesome6.h` 中的图标名以 `ICON_FA_` 为前缀
- 不要给文件类型分配重复的颜色,便于一眼区分

---

## B4. 内容浏览器目录条目缓存 + 手动刷新按钮

**优先级**: P2  **工时**: ~1h  **风险**: 中(IO/缓存逻辑)

### 背景
[`ContentBrowserPanel.cpp:366-368`](../../src/Editor/Panels/ContentBrowserPanel.cpp) 每帧都做 `std::filesystem::directory_iterator(currentPath)`。在大目录(如 `assets/textures/` 几百张)上每帧 IO,既慢又会卡 UI。

### TODO

- [ ] 引入一个静态(或挂在 `EditorUiState`)缓存:`std::unordered_map<std::filesystem::path, std::vector<directory_entry>>`
- [ ] 进入新目录或缓存 miss 时填充
- [ ] 在 Outliner 顶部 `DrawContentBrowserNavigation` 旁加一个 `ICON_FA_ROTATE` 刷新按钮:点击清空当前目录缓存
- [ ] 切换 currentPath 时**不**自动清空所有缓存(只在用户点刷新时清)
- [ ] (可选 stretch) 自动按 `last_write_time` 检测 mtime 变化

### 涉及文件
- `src/Editor/Panels/ContentBrowserPanel.cpp`

### 验收方法
1. 构建通过
2. 进入 `assets/textures/`,UI 不再每帧卡顿(若不易体感,可临时在循环里加 `SPDLOG_DEBUG` 计数器对比)
3. 在外部往该目录新增/删除文件,内容浏览器**不会**立即反映
4. 点击刷新按钮后,新文件出现/旧文件消失
5. 来回切换不同目录,行为稳定无 use-after-free

### 注意
- `directory_entry` 内部持有路径,可以安全缓存
- 不要在 ctor/dtor 抛异常的目录上崩溃(如权限不足):`std::error_code` 重载捕获
- 若实现自动 mtime 检测,要节流(例如每秒最多 check 一次)

---

## C1. 新增 procedural Material Showcase 场景

**优先级**: P2  **工时**: ~45m  **风险**: 极低

### 背景
[`SceneList.cpp:336`](../../src/Runtime/Scene/SceneList.cpp) 的 `GIBootcamp` 已经是一个完整的 procedural 场景模板(几何 + 材质 + 灯光 + 相机)。新增一个 `MaterialShowcase`,用于材质回归测试与视觉对比。

### TODO

- [ ] 在 [`SceneList.cpp`](../../src/Runtime/Scene/SceneList.cpp) 仿 `GIBootcamp` 的写法新增 `MaterialShowcase` 函数:
  - [ ] 1 个地面 (Lambertian gray)
  - [ ] 5x3 球阵列,行=材质类型(Lambertian / Metallic / Mixture / Dielectric / DiffuseLight),列=粗糙度(0.0 / 0.3 / 0.8)
  - [ ] 1 盏面光源 (DiffuseLight)
  - [ ] 中性 HDRI(可让 `HasSky = true; SkyIdx = 0`)
- [ ] 在 [`SceneList.cpp:1173`](../../src/Runtime/Scene/SceneList.cpp) `AllScenes.push_back("MaterialShowcase.proc");`
- [ ] 在 [`SceneList.cpp:1245`](../../src/Runtime/Scene/SceneList.cpp) 附近的 if 分支中加 `MaterialShowcase` 调度
- [ ] 在 `assets/configs/visual_test.json` 加一项,把 `MaterialShowcase.proc` 纳入视觉测试

### 涉及文件
- `src/Runtime/Scene/SceneList.cpp`
- `assets/configs/visual_test.json`

### 验收方法
1. 构建通过
2. `./run.bat --preset full-windows --target gkNextRenderer.exe`,在场景下拉中看到 `MaterialShowcase.proc`
3. 选中后,视口出现 5x3 球阵列,材质区分明显
4. 日志出现 `uploaded scene [MaterialShowcase.proc] to gpu`
5. `./out/build/full-windows/bin/gkNextVisualTest.exe` 跑过,生成 `MaterialShowcase` 截图

### 注意
- 不要新增贴图依赖,所有材质都用 procedural 颜色
- 球的间距至少 2.5 倍球半径,避免遮挡
- 灯光强度参考 `GIBootcamp` 数值范围(数百~上千)

---

## D1. VisualTest 输出与基线像素差异统计 (stretch)

**优先级**: P3  **工时**: ~1.5h(略超 1h,作为 stretch)  **风险**: 中

### 背景
[`gkNextVisualTest.cpp`](../../src/Application/gkNextVisualTest/gkNextVisualTest.cpp) 当前生成截图与 HTML 报告,但没有与基线对比。加上像素级 RMSE / 最大差异统计,能让回归更可发现。

### TODO

- [ ] 设计基线目录结构:`assets/visual_test_baselines/<scene_name>.png`(或 `.avif`)
- [ ] 截图后,如果同名基线存在,加载基线并计算:
  - [ ] 总像素 RMSE
  - [ ] 最大单像素差(R/G/B 通道分别)
  - [ ] 差异 > 阈值的像素百分比(阈值可配,默认 5/255)
- [ ] 把统计写入 HTML 报告(每行 baseline / current / diff 三栏 + 数值)
- [ ] 加一个 `--update-baseline` 命令行开关:跑完直接把 current 覆盖到基线目录

### 涉及文件
- `src/Application/gkNextVisualTest/gkNextVisualTest.cpp`
- `assets/configs/visual_test.json`(若需要新字段)
- 新增 `assets/visual_test_baselines/` 目录(只放 `.gitkeep` 占位即可,基线由开发者本地生成)

### 验收方法
1. 构建通过
2. 第一次运行:基线缺失,只生成 current 截图,提示 "no baseline"
3. `--update-baseline` 跑一次后,基线目录出现 PNG
4. 修改某个 procedural 场景的颜色,再次运行,HTML 报告显示 RMSE 非零、diff 百分比可读
5. 用 `--update-baseline` 重置后,差异回 0

### 注意
- 不要把基线 PNG 提交进 git(用 `.gitignore` 排除,或放到 optional pak 流程)
- 截图分辨率必须与基线一致,否则报错跳过(不要 resize 比对)
- 像素比较使用 `uint8` 减法,RMSE 计算用 `double`

---

## 完成后的常规收尾

- 不要在代码里留 `// TODO` 或 `// FIXME`,除非显式标注「下一项任务」
- 不要新增 `.md` 文档,除非用户/计划文档明确要求
- `SPDLOG_INFO` 给最终成功路径,`SPDLOG_WARN` 给可恢复异常,`SPDLOG_ERROR` 给真正失败
- 提交前检查:`git status` 不应有未跟踪的中间文件 (`out/`、临时截图等)
- 严格遵循 `AGENTS.md` 的「执行前确认」原则:**不要**做任务卡范围之外的「顺手清理」

---

## 任务挑选建议

按时间块从 P0 开始顺序消化:
- **第 1 块(45m)**: A1 (Save Scene As)
- **第 2 块(45m)**: A2 (HDRI 切换)
- **第 3 块(30m)**: A3 (Layout Reset)
- 之后视精力挑 B / C 中任意一项

P3 的 D1 留到有较大空闲时再启动。

