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
