# gkNextEngine Official Website

这是 gkNextEngine 的官方主页与文档站源码，采用 **VitePress** 构建，视觉设计参考 **Defold** 现代暗色极简风格。

## 🚀 本地开发与构建

### 安装依赖
```bash
npm install
```

### 启动本地热重载开发服务
```bash
npm run dev
```

### 构建纯静态部署产物 (SSG)
```bash
npm run build
```
构建产物输出至 `.vitepress/dist/`，可直接部署在任意轻量服务器（如 Nginx, Caddy）或 GitHub Pages / Cloudflare Pages 上。

### 本地预览构建产物
```bash
npm run preview
```

## 🌐 服务器部署 (Nginx)

参考 [nginx.conf.example](./nginx.conf.example)，将 `.vitepress/dist/` 复制到服务器后通过静态 Web 服务器分发即可。内存占用 < 10MB，极度轻量。
