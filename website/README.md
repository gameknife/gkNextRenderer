# gkNextEngine Official Website

这是 gkNextEngine 的官方主页与文档站源码，采用 **VitePress** 构建，视觉设计参考 **Defold** 现代暗色极简风格。

## 🚀 本地开发与构建 (推荐使用 gnb)

可以在仓库根目录直接通过 `gnb` 工具链管理网站，自动处理依赖安装与构建：

```bash
# 启动本地热重载开发服务器 (http://localhost:5173)
./gnb.sh website            # 或 ./gnb.bat website

# 编译纯静态部署产物 (输出至 website/.vitepress/dist)
./gnb.sh website build

# 本地预览构建产物 (http://localhost:4173)
./gnb.sh website preview

# 模拟 GitHub Pages 二级子路径 (/gkNextEngine/) 进行构建与预览
./gnb.sh website build --pages
./gnb.sh website preview --pages
```

也可以在 `website/` 目录下使用原生 npm 命令：
```bash
cd website
npm install
npm run dev
npm run build
npm run preview
```

## 🌐 服务器部署 (Nginx)

参考 [nginx.conf.example](./nginx.conf.example)，将 `.vitepress/dist/` 复制到服务器后通过静态 Web 服务器分发即可。内存占用 < 10MB，极度轻量。

## 💬 社区论坛（GitHub Discussions + giscus）

`/community/`（英文 `/en/community/`）是一个论坛形态的板块：**主题只能由维护者发布，访客只能回帖**。

### 它是怎么做到「访客不能开主题」的

两道锁，缺一不可：

1. **分类**：主题发在 GitHub Discussions 的 **Announcements** 分类。这是 GitHub 内建的公告型分类，
   只有仓库维护者能在里面开新讨论，其他人只能回帖。
2. **映射**：giscus 用 `data-mapping="number"` 绑定到**已存在的讨论号**。
   常见的 `pathname` 映射会在访客首次评论时由 giscus bot 代建讨论，`number` 映射不会。

### 主题列表从哪来

GitHub Discussions 只有 GraphQL API 且必须带 token，浏览器端拿不到，所以列表是**构建期抓的快照**：

```bash
npm run fetch:discussions    # 抓 Announcements 分类 → .vitepress/data/discussions.json
npm run check:discussions    # 只比对，快照过期时非零退出
npm run build                # build 会自动先抓一次
```

- 快照**提交进仓库**。没有 token（本地 clone、fork 的 CI）时脚本原样退出，站点沿用上一次的快照，不会白页。
- token 取 `GITHUB_TOKEN` / `GH_TOKEN`，都没有时退化到已登录的 `gh` CLI。
- 主题页是 VitePress 动态路由（`community/[number].md`），正文用讨论的 `bodyHTML` 渲染 —— 走 `v-html`
  而不是注入 markdown，因为讨论正文里的 `{{ }}` 会被 Vue 模板编译器当成插值。
- 部署工作流带了每日 cron，新发的主题不用等下一次 push 才上线。

### 新开一台机器 / 换仓库时要改什么

配置集中在 [`.vitepress/community.mjs`](./.vitepress/community.mjs)：

```bash
# repoId
gh api repos/<owner>/<repo> --jq .node_id
# categoryId
gh api graphql -f query='{repository(owner:"<owner>",name:"<repo>"){discussionCategories(first:20){nodes{id name}}}}'
```

另外需要在 GitHub 上手动做两件事：给仓库开 Discussions，以及安装 [giscus app](https://github.com/apps/giscus)
（没装的话评论区会显示 "giscus is not installed on this repository"）。

## 📊 Cloudflare Web Analytics

在 [`.vitepress/community.mjs`](./.vitepress/community.mjs) 的 `cfBeaconToken` 里填 Cloudflare 控制台
Web Analytics 给出的站点 token，或在 CI 里设 `CF_BEACON_TOKEN` 环境变量（工作流已经从
`secrets.CF_BEACON_TOKEN` 透传）。两边都为空则整段 beacon 脚本不注入。

token 是站点级的，**不能和其它站点共用**，否则数据会混在一起；它本来就会明文出现在页面 HTML 里，
提交进仓库没有泄密问题。
