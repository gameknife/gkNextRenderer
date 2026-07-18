---
title: "Editor Settings 与 CVar 架构"
category: design
status: 现行
owner: editor/runtime-config
created: 2026-07-17
last_updated: 2026-07-17
---

# Editor Settings 与 CVar 架构

gkNextEditor 的 Settings / Preferences 是 CVar 的数据驱动视图。`settings_panel.json` 只描述“显示哪些项以及用什么控件”，实际值、默认值、校验、回调与持久化都由 `FCVarSystem` 拥有。不要在 panel 内再维护一份设置对象或直接修改 `UserSettings` 来绕过 CVar。

## 初始化顺序与所有权

引擎启动顺序是：

1. `RegisterEngineCVars()` 注册 `r.*`、`sys.*` 等引擎项。
2. 加载 `assets/configs/cvar_default.json`。
3. 调用当前 `GameInstance::ConfigureCVars()`，让应用调整默认值、注册自身 CVar 和持久化 channel。
4. `LoadUserFiles()` 加载共享及应用 channel 的用户覆盖。
5. 最后应用命令行 CVar override。

`EditorGameInstance::ConfigureCVars()` 把 `ed.*` 绑定到生命周期稳定的 `EditorSettings settings_` 成员，并注册 `ed.` → `assets/configs/cvar_user.editor.json`。此时 Editor UI 尚未创建，因此 CVar target 不能指向 `EditorUiState` 或 panel 局部变量。

当前 `ed.*` 包括 hover highlight、Outliner auto-scroll、gizmo snap、translation snap distance 和默认 gizmo mode。行为代码通过 `EditorContext::settings` 或 `EditorGameInstance::settings_` 读取这些字段；临时窗口开关、搜索词和 selected category 才属于 `EditorUiState`。

## 三类配置文件

| 文件 | 角色 |
| --- | --- |
| `assets/configs/ui/settings_panel.json` | 只读视图 manifest：分类、分组、widget、范围、options、tooltip、advanced 标记 |
| `assets/configs/cvar_default.json` | 项目的默认值覆盖 |
| `assets/configs/cvar_user.json` / `cvar_user.editor.json` | 仅保存 Archive 且不同于默认值的用户覆盖 |

manifest 不是值文件，也不是 CVar 注册源。新增设置通常需要先在 Engine 或 `ConfigureCVars()` 注册 CVar，再把名字加入 manifest。只加 manifest 会因未知 CVar 被跳过；只注册 CVar 则仍可通过 console/All CVars 使用，但不会自动进入策划过的 Settings 页面。

## Manifest 与面板行为

`Panels/SettingsPanel.cpp` 当前支持 checkbox、combo、integer slider/drag、float slider/drag 和只读文本 fallback。加载时会调用 `TryGetInfo()` 验证名称和类型；未知项或 widget/type 不匹配会记录 warning 并跳过。文件缺失、无效或没有可用分类时使用内置最小 manifest，保证面板仍可打开。

控件修改统一走 `SetValueFromString(..., ECVarSetBy::Console)`，因此范围、StartupOnly/ReadOnly 规则和 onChanged callback 仍由 CVar 层执行。改值立即生效；`Apply & Save` 只调用 `SaveUserFiles()` 落盘。Reset Category 会对该分类中的每项调用 `ResetToDefault()`。

manifest 中的数值范围用于 UI，同时注册 CVar 自身也应提供真实 min/max。安全边界必须存在于 CVar 注册/SetEntryValue，不能只靠 slider 限制，因为 console、配置文件和命令行都能绕过 UI。

## 持久化 channel

`RegisterUserFileChannel(prefix, path)` 按最长前缀匹配 CVar 所属文件；未匹配项落到共享 user file。`SaveUserFiles()` 分桶重写各 channel，只写 Archive 且非默认项。应用专属 namespace 应在 `ConfigureCVars()` 注册 channel，不能让调用方每次手工选择保存路径。

显式 `LoadUserFile` / `SaveUserFile` 仍是单文件兼容接口；面板、console 的常规保存和引擎启动应使用复数形式，避免把 `ed.*` 混入共享文件或漏掉专属 channel。

## 扩展检查表

新增设置时核对：target 生命周期是否覆盖 CVarSystem、命名空间与 channel 是否正确、Archive/ReadOnly/StartupOnly flags、默认文件和应用默认的优先级、真实 min/max、onChanged 是否请求必要的 renderer/swapchain 更新、manifest widget 类型、fallback manifest 是否仍可用，以及重新启动后的读写结果。
