# 1 小时任务开发计划 第五批 (2026-04, VisualTest 工具链专题)

## Context

前四批共 31 项任务。本批为**单一专题**:把 VisualTest 工具链从「能跑、能截图、能算 RMSE」升级到「面向 CI 与高效迭代的成熟工具」。

当前 `gkNextVisualTest`(D1 实现 + S3 完善 + 批次 2 baseline 比较)能力:
- 按 `visual_test.json` 列表跑场景
- 截图 + 与 baseline 对比 RMSE / 通道差 / 像素差比例
- HTML / Markdown 报告
- per-scene timeout 配置
- `--update-baseline` 全量更新

**仍缺的关键能力**(本批补齐):
- 选择性运行(只跑匹配的场景)
- CI 友好输出(JSON + 末尾 summary + fail-fast)
- 观察性(慢场景告警 + dry-run)
- HTML 报告可交互(排序)

本批与 **batch 4 零文件冲突**(batch 4 只在 `visual_test.json` 加场景行,不动 cpp/hpp;本批反之主要动 cpp/hpp,JSON 仅可能扩展新顶层字段),可与 batch 4 并行实现。

## 使用方法

1. **挑任务**: 任选 1 项,逐项完成 TODO。
2. **遵循公共约束**: 命名/构建/平台规则全部沿用 [`AGENTS.md`](../../AGENTS.md)。
3. **构建 preset**: 验证一律使用 `full-*` preset。
4. **报告**: 完成后简述「改了哪些文件、测了什么、看到的输出」,**不**总结代码意图。

## 公共上下文(快速参考)

| 项 | 值 |
|---|---|
| 构建 | `./build.bat --preset full-windows` / `./build.sh --preset full-macos-arm64` |
| 运行 visual test | `./out/build/full-windows/bin/gkNextVisualTest.exe` |
| 配置文件 | `assets/configs/visual_test.json` |
| 现有 CLI flag | `--update-baseline`(整批覆盖 baseline) |
| 现有报告产物 | `screenshots/visual_test/visual_test_report.{md,html}` 与 `_diff.png` |
| 命名规范 | 类型/函数 PascalCase;变量/参数 camelCase;私有成员 trailing `_` |
| 头文件首选 | `#include "Common/CoreMinimal.hpp"` |

> 提示:**禁止**修改 `src/ThirdParty/` 与 `external/`。本批所有改动应集中在 `src/Application/gkNextVisualTest/`。

---

## 任务索引

| # | 标题 | 工时 | 优先级 |
|---|---|---|---|
| [VT1](#vt1---scene-glob-cli-filter) | `--scene <glob>` CLI filter | ~30m | P0 |
| [VT2](#vt2-summary-stats-行末尾汇总) | Summary stats 行(末尾汇总) | ~30m | P1 |
| [VT3](#vt3-json-报告输出供-ci-解析) | JSON 报告输出(供 CI 解析) | ~30m | P1 |
| [VT4](#vt4---fail-fast-flag) | `--fail-fast` flag | ~15m | P2 |
| [VT5](#vt5-慢场景告警) | 慢场景告警 | ~20m | P2 |
| [VT6](#vt6-html-表格列可排序) | HTML 表格列可排序 | ~45m | P2 |
| [VT7](#vt7---list-flag-dry-run) | `--list` flag(dry-run) | ~15m | P3 |
| [Known Issue: A1 carry-over](#known-issue-a1-save-scene-as-文件对话框) | Save Scene As 文件对话框(deferred 第五次) | ~30m | (deferred) |

---

## VT1. `--scene <glob>` CLI filter

**优先级**: P0  **工时**: ~30m  **风险**: 低

### 背景
当前必须跑完 `visual_test.json` 全部场景才能验证某一个的渲染。改了一个 procedural showcase 还要等其他十几个场景跑完,迭代很慢。加 `--scene <glob>` 只跑匹配的子集,**对本批所有其他 VT 任务的调试效率直接翻倍**,所以列为 P0 先做。

### TODO

- [ ] 在 `src/Application/gkNextVisualTest/gkNextVisualTest.cpp/.hpp` 解析 CLI 入口找到现有的 `--update-baseline` 参数解析处
- [ ] 新增 `--scene <pattern>` 参数:
  - [ ] `pattern` 支持 glob 风格(`*`、`?`)以及子串匹配 — 优先用现有 fnmatch / glob 工具,若无,自实现一个简单的 `MatchesGlob(name, pattern)`:把 `*` 当作 `.*` 即可(避开正则依赖,直接遍历两指针)
  - [ ] 大小写不敏感
  - [ ] 多次出现的 `--scene` 累加(任一匹配即跑)
- [ ] 在场景调度循环里:`scenes_` 过滤一遍,只保留匹配的;空匹配 → SPDLOG_WARN 并提前退出(返回 0)
- [ ] 帮助/usage 输出加该 flag 描述

### 涉及文件
- `src/Application/gkNextVisualTest/gkNextVisualTest.cpp`(主改)
- `src/Application/gkNextVisualTest/gkNextVisualTest.hpp`(若 CLI struct 在 header)

### 验收方法
1. 构建通过
2. `./out/build/full-windows/bin/gkNextVisualTest.exe --scene MaterialShowcase.proc` → **只**跑该场景,日志显示 1 / 1
3. `--scene "*Showcase*"` → 跑所有名字含 Showcase 的(MaterialShowcase / LightingShowcase / CameraShowcase / 若 batch4 已合 AnimationShowcase + PhysicsShowcase 也算)
4. `--scene XYZ_NotExist` → SPDLOG_WARN 提示 0 匹配,exit 0
5. 不传 `--scene` → 行为等同现状(全跑)
6. 与 `--update-baseline` 组合 → 仅更新匹配场景的 baseline

### 注意
- **不要**引入 `std::regex`(编译慢且过度);自实现两指针 glob 已足够
- **不要**改 `visual_test.json` 的「scenes」字段语义 — 这是运行时过滤,不是配置过滤
- 大小写处理在比较前对两端都 `tolower` 一下即可

---

## VT2. Summary stats 行(末尾汇总)

**优先级**: P1  **工时**: ~30m  **风险**: 低

### 背景
当前报告(markdown / html)只列每个场景一条记录,看不到「这次跑下来整体怎样」。CI 需要一行扫描得到「pass rate 几何 / 最慢谁 / 总耗时多少」。

### TODO

- [ ] 在 `GenerateReport()` 与 `GenerateHtmlReport()` 末尾(footer 之前)加 summary 块:
  - [ ] 总数 / pass / fail / skipped
  - [ ] pass rate(百分比)
  - [ ] 总耗时(所有场景 render 时间累加,**不**含 baseline 比较)
  - [ ] 最慢 3 个场景(名 + 耗时)
  - [ ] baseline 命中数 / miss 数 / 更新数
- [ ] markdown 用 `## Summary` heading + bullet list
- [ ] html 用 `<section class="summary">` + 简单 `<ul>` 或 `<table>`
- [ ] 同步 SPDLOG_INFO 输出一行 `[VisualTest] Summary: pass=X/Y (Z%), total=Ts, slowest=<name>(Ts)`,便于 CI 直接 grep

### 涉及文件
- `src/Application/gkNextVisualTest/gkNextVisualTest.cpp`

### 验收方法
1. 构建通过
2. 跑全套或部分 → markdown 报告末尾出现 Summary
3. html 报告末尾同样
4. 终端 stdout 出现 `[VisualTest] Summary: ...` 一行
5. 全 pass 时 `pass rate = 100%`;有 fail 时正确反映

### 注意
- 「最慢 3 个」用 `nth_element` 或简单 sort 取前 3;**不要**为此引入新依赖
- 总耗时累加用 `double seconds` 即可,不要纳入 baseline 比较时间(那是 IO,不代表渲染管线性能)
- skipped 类(VT1 的不匹配 / VT4 的 fail-fast 提前退出)也要单列

---

## VT3. JSON 报告输出(供 CI 解析)

**优先级**: P1  **工时**: ~30m  **风险**: 低

### 背景
markdown / HTML 给人看,JSON 给 CI 看。CI pipeline 想做「失败场景列表 → 评论到 PR / 触发告警」需要结构化数据。当前没有,只能 grep markdown,脆弱。

### TODO

- [ ] 在 `GenerateReport()` / `GenerateHtmlReport()` 之后,新增 `GenerateJsonReport()`,产物 `screenshots/visual_test/visual_test_report.json`
- [ ] schema(扁平,易于 jq 处理):
  ```json
  {
    "renderer": "...",
    "captureMode": "...",
    "diffThreshold": 5,
    "summary": { "total": N, "pass": N, "fail": N, "skipped": N, "totalSeconds": ... },
    "results": [
      {
        "sceneName": "...",
        "scenePath": "...",
        "success": true,
        "errorMessage": "",
        "renderSeconds": 1.23,
        "screenshotPath": "...",
        "baseline": { "compared": true, "updated": false, "rmse": 0.0, "maxR": 0, "maxG": 0, "maxB": 0, "diffPixelRatio": 0.0 }
      }
    ]
  }
  ```
- [ ] 不引入 JSON 库 — 项目可能已用 nlohmann/json 或 rapidjson(检查 vcpkg.json)。若两者都未引入,**手写**输出(escape + naive serialization)足够,因为 schema 确定
- [ ] 把 `GenerateJsonReport` 在 `Run()` 末尾、`GenerateAgentManifest` 之前调用

### 涉及文件
- `src/Application/gkNextVisualTest/gkNextVisualTest.cpp`(主改)
- `src/Application/gkNextVisualTest/gkNextVisualTest.hpp`(若需要 declare)

### 验收方法
1. 构建通过
2. 跑一次 → `visual_test_report.json` 出现,内容可被 `python -m json.tool` 或 `jq .` 验证为合法 JSON
3. `jq '.summary.pass'` 输出对应数字
4. `jq '.results[] | select(.success == false) | .sceneName'` 列出失败场景
5. 字段顺序无所谓,但所有 results 都有相同字段集

### 注意
- 字符串 escape **至少**处理:`\\` `\"` `\n` `\r` `\t`,以及 `<0x20` 控制字符
- 浮点数输出用 `fmt::format("{:.6f}", x)` 等避免本地化逗号
- **不要**把图片以 base64 塞进 JSON — 只放路径

---

## VT4. `--fail-fast` flag

**优先级**: P2  **工时**: ~15m  **风险**: 极低

### 背景
CI 调试某个回归时,后面场景的失败是噪声。`--fail-fast` 让第一个失败立即停下,产生最小可疑场景列表。一行 flag,与 VT1 / VT2 / VT3 都有自然交叉(报告需要正确反映 skipped 类)。

### TODO

- [ ] CLI 解析新增 `--fail-fast` bool flag(成员 `failFast_ = false`)
- [ ] 主循环每个场景结束后:若 `failFast_ && !result.success && !result.baselineCompared`(或更准确:实际走渲染流程并失败的)→ 立即 `break`
- [ ] 跳出后,剩余场景作为 skipped 录入 `results_`,error message 设为 `"skipped due to --fail-fast"`,success = false
- [ ] VT2 Summary 的 skipped 计数应正确反映这部分

### 涉及文件
- `src/Application/gkNextVisualTest/gkNextVisualTest.cpp`

### 验收方法
1. 构建通过
2. 临时修改某个场景使其加载失败(或跑一个不存在的 scene)→ 不带 `--fail-fast`:剩余场景照跑;带 `--fail-fast`:立即停,后面场景标 skipped
3. 报告末尾 Summary 显示 1 fail + N skipped
4. exit code 非 0(若现状已如此就不动)

### 注意
- 不要把 `--fail-fast` 跟 `--update-baseline` 混合时跑半截 baseline 进基线目录;**只在比较模式下** fail-fast 才有意义。`--update-baseline` 模式下 flag 应被忽略并 SPDLOG_WARN
- 单元化:把判定逻辑封一个 `bool ShouldStopOnFailure(const Result&)` 函数,后续可扩展(如「pixel diff > X 算失败」)

---

## VT5. 慢场景告警

**优先级**: P2  **工时**: ~20m  **风险**: 极低

### 背景
某个场景慢,可能是性能回归 / shader 重编译 / 错误的 wait 配置。当前只能事后翻 markdown 看耗时。加一个 SPDLOG_WARN 阈值,渲染 > N 秒立即记录,迭代时直接看终端就能发现。

### TODO

- [ ] 在 `visual_test.json` 加可选顶层字段 `"slowSceneThresholdSeconds": 5.0`(默认值)
- [ ] 解析进配置;若缺省取 5.0
- [ ] 在每个场景渲染完成处,若 `renderSeconds > threshold`,SPDLOG_WARN(`"[VisualTest] Slow scene: {} took {:.2f}s (>{:.2f}s threshold)"`)
- [ ] **不**改变 success/fail 判定 — 仅作观察告警

### 涉及文件
- `src/Application/gkNextVisualTest/gkNextVisualTest.cpp`
- `assets/configs/visual_test.json`(新增可选字段)

### 验收方法
1. 构建通过
2. 设阈值 0.1 → 跑全套,**所有**场景几乎都触发 WARN(印证逻辑)
3. 设阈值 60 → 无 WARN
4. 缺省字段 → 默认 5.0 工作
5. 报告未变化(VT5 不动报告内容)

### 注意
- 不要把告警次数累加进 fail / pass — 它是观察性的
- 阈值字段名要与 batch 2 S3 加的 `sceneTimeouts` 区分清楚(那是超时即失败,这是慢但不失败)

---

## VT6. HTML 表格列可排序

**优先级**: P2  **工时**: ~45m  **风险**: 中(需写少量 vanilla JS)

### 背景
跑 30+ 场景时 HTML 报告默认按 `visual_test.json` 顺序排,想找「最慢 / RMSE 最高 / 失败的」只能 Ctrl+F。点击列头排序是标配。用最小 vanilla JS 实现(不引 jQuery / 框架)。

### TODO

- [ ] 在 `GenerateHtmlReport()` 给 `<table>` 加 `id="results"`,给每个 `<th>` 加 `onclick="sortTable(N)"`(N 为列 index)与 `style="cursor:pointer"`
- [ ] 在 `<head>` 加内联 `<script>`:
  - [ ] `sortTable(col)`:取 tbody 行,基于 `col` 列的 textContent 排序;若全部为数字则数值排,否则字符串排;再次点击同列倒序
  - [ ] 用 `dataset` 记当前排序列与方向
- [ ] 列头加 ▲/▼ 提示(只在当前排序列显示)
- [ ] **不要**引入外部 JS 文件 — 一切内联,确保单文件 portable

### 涉及文件
- `src/Application/gkNextVisualTest/gkNextVisualTest.cpp`(只是 raw HTML 字符串拼接)

### 验收方法
1. 构建通过
2. 打开 `visual_test_report.html`,点 RMSE 列头 → 行按 RMSE 升序;再点 → 降序
3. 点 Status 列 → fail 集中(字符串排序)
4. 点 Scene 列 → 字典序
5. 单列箭头指示正确显示
6. 浏览器 console 无 JS 错误

### 注意
- 数值列要识别:取第一个 `<td>` 的 textContent,先 `parseFloat`,失败再退回 string
- diff 列含图片 `<img>`,排序应跳过这种列(给 `<th class="nosort">` 不绑定 onclick)
- 不要写 ES 模块 / `import`,纯 ES5+ 函数定义即可;考虑 IE/旧 Edge 暂不必,但**不要**用最前沿 ES2022 语法
- 不要在 JS 里 fetch / 改文件 — 纯 DOM 排序

---

## VT7. `--list` flag(dry-run)

**优先级**: P3  **工时**: ~15m  **风险**: 极低

### 背景
不实际跑,只列「当前配置 + 当前 CLI flag 下,会跑哪些场景」。配合 VT1 的 `--scene` glob 调试 pattern 时极其有用。

### TODO

- [ ] CLI 加 `--list` bool flag
- [ ] 命中时:执行完场景过滤逻辑(VT1 的 glob 也作用于此)→ 把最终 scene 列表打印到 stdout(每行一个)→ exit 0
- [ ] 不加载引擎、不创建窗口、**不**渲染任何东西(避免拉起 Vulkan)
- [ ] 与 `--scene XX --list` 组合:列出匹配的;无匹配空输出 + exit 0(供 shell `xargs` 用)

### 涉及文件
- `src/Application/gkNextVisualTest/gkNextVisualTest.cpp`

### 验收方法
1. 构建通过
2. `gkNextVisualTest --list` → 终端列出所有配置场景,每行一个,程序立即退出,**不**起 Vulkan
3. `gkNextVisualTest --scene "*Material*" --list` → 仅列匹配的
4. exit code 0
5. 可链式:`gkNextVisualTest --list | wc -l`

### 注意
- 入口要在「初始化引擎之前」就 short-circuit,否则起 Vulkan 还是慢
- 输出**只**有 scene 文件名,不要前缀 `[VisualTest]` — 方便 shell 管道
- stderr / stdout 分离:程序信息走 stderr,scene 列表走 stdout

---

## Known Issue: A1 Save Scene As 文件对话框

**第五次 carry-over**(自第一批起)。工时 ~30m。

将与「场景 IO 完整化迷你专题」一起在 batch 6 处理(A1 + Save As Copy + GLTF/GLB 选择 + 拖入加载 + 脏标记保存提示)。本批专注 VisualTest,不动 TitleBar。

---

## 完成后的常规收尾

- 不要在代码里留 `// TODO` / `// FIXME`,除非明确标注下一项任务
- 不要新增 `.md` 文档,除非用户/计划文档明确要求
- `SPDLOG_INFO` 给最终成功路径,`SPDLOG_WARN` 给可恢复异常,`SPDLOG_ERROR` 给真正失败
- 提交前 `git status` 不应有未跟踪的中间文件(`out/`、临时截图等)
- 严格遵循 `AGENTS.md` 的「执行前确认」原则:**不要**做任务卡范围之外的「顺手清理」

## 任务挑选建议

**强烈建议先做 VT1**(`--scene` filter),后续所有 VT 任务的迭代验证都需要它来加速 — 单场景跑一次 ~10s,全套 ~3min。

之后顺序:
- **第 1 块(30m)**: VT1
- **第 2 块(30m)**: VT2 (summary 行)
- **第 3 块(30m)**: VT3 (JSON 输出)— VT2 / VT3 互不依赖,可任意顺序;放一起做能复用同一组 result 数据结构
- **第 4 块(15m)**: VT4 (fail-fast)
- **第 5 块(20m)**: VT5 (慢场景 WARN)
- **第 6 块(45m)**: VT6 (HTML 排序)
- **第 7 块(15m)**: VT7 (--list)

VT4-VT7 完全独立,可挑可换。

## 与 batch 4 的协调

- batch 4 SC1/SC2 仅在 `visual_test.json` 的 `scenes` 数组追加条目
- 本批 VT1 / VT2 / VT3 / VT4 / VT6 / VT7 **不动** `visual_test.json`
- VT5 在 `visual_test.json` 加新顶层字段 `slowSceneThresholdSeconds`,与 batch 4 修改的 `scenes` 数组**不同 key 不同区域**,git 自动合并即可

故 batch 4 与 batch 5 可并行实现,合并到同一分支或先后落地皆可。

