import fs from 'node:fs'
import path from 'node:path'
import { defineConfig } from 'vitepress'

function syncBrandAssetsPlugin() {
  const brandSourceDir = path.resolve(__dirname, '../../assets/brand')
  return {
    name: 'sync-brand-assets',
    configureServer(server: any) {
      server.middlewares.use((req: any, res: any, next: any) => {
        if (req.url && (req.url.startsWith('/brand/') || req.url.startsWith('/gkNextEngine/brand/'))) {
          const cleanUrl = req.url.replace(/^\/(gkNextEngine\/)?brand\//, '').split('?')[0]
          const filePath = path.join(brandSourceDir, cleanUrl)
          if (fs.existsSync(filePath)) {
            const ext = path.extname(filePath).toLowerCase()
            const mimeTypes: Record<string, string> = {
              '.svg': 'image/svg+xml',
              '.png': 'image/png',
              '.jpg': 'image/jpeg',
              '.ico': 'image/x-icon',
              '.txt': 'text/plain'
            }
            res.setHeader('Content-Type', mimeTypes[ext] || 'application/octet-stream')
            fs.createReadStream(filePath).pipe(res)
            return
          }
        }
        next()
      })
    },
    closeBundle() {
      const brandDistDir = path.resolve(__dirname, 'dist/brand')
      if (fs.existsSync(brandSourceDir)) {
        fs.mkdirSync(brandDistDir, { recursive: true })
        fs.cpSync(brandSourceDir, brandDistDir, { recursive: true })
      }
    }
  }
}

export default defineConfig({
  base: process.env.GITHUB_PAGES ? '/gkNextEngine/' : '/',
  title: 'gkNextEngine',
  description: '轻量、现代、极速光追。自由开源的跨平台 3D 游戏引擎',
  srcExclude: ['README.md'],
  ignoreDeadLinks: true,
  head: [
    ['link', { rel: 'icon', type: 'image/svg+xml', href: '/brand/gknext_logo_icon.svg' }],
    ['meta', { name: 'theme-color', content: '#0b0d13' }],
    ['meta', { property: 'og:type', content: 'website' }],
    ['meta', { property: 'og:title', content: 'gkNextEngine - 轻量、现代、极速光追的跨平台 3D 游戏引擎' }],
    ['meta', { property: 'og:description', content: '基于 C++20 与 Vulkan，为实时路径追踪、游戏原型与 AI Native 工作流打造。' }],
    ['meta', { property: 'og:image', content: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/still.webp' }],
  ],

  locales: {
    root: {
      label: '简体中文',
      lang: 'zh-CN',
      themeConfig: {
        nav: [
          { text: '首页', link: '/' },
          { text: '核心特性', link: '/#features' },
          { text: '子项目展厅', link: '/#showcase' },
          { text: '性能指标', link: '/#benchmarks' },
          { text: '快速开始', link: '/#quickstart' },
          {
            text: '文档手册',
            items: [
              { text: '文档总览', link: '/docs/' },
              { text: '快速起步指南', link: '/docs/getting-started' },
              { text: '架构与管线设计', link: '/docs/architecture' },
              { text: 'AI Agent 开发指引', link: '/docs/agent-guide' },
              { text: 'C# 脚本开发', link: '/docs/csharp-development' },
              { text: 'SCAD 程序化管线', link: '/docs/scad-pipeline' },
              { text: '15+ 子项目清单', link: '/docs/subprojects' }
            ]
          }
        ],
        sidebar: {
          '/docs/': [
            {
              text: '入门与指南',
              items: [
                { text: '文档总览', link: '/docs/' },
                { text: '快速起步', link: '/docs/getting-started' },
                { text: '引擎架构设计', link: '/docs/architecture' },
                { text: '子项目与玩法原型', link: '/docs/subprojects' },
              ]
            },
            {
              text: '核心子系统',
              items: [
                { text: 'C# 托管脚本开发', link: '/docs/csharp-development' },
                { text: 'SCAD 程序化内容管线', link: '/docs/scad-pipeline' },
                { text: 'AI Agent 与自动化验证', link: '/docs/agent-guide' },
              ]
            }
          ]
        },
        footer: {
          message: '基于 MIT 协议开源 · 个人 R&D 引擎实验场',
          copyright: 'Copyright © 2026 gameknife / gkNextEngine 贡献者'
        }
      }
    },
    en: {
      label: 'English',
      lang: 'en-US',
      link: '/en/',
      themeConfig: {
        nav: [
          { text: 'Home', link: '/en/' },
          { text: 'Features', link: '/en/#features' },
          { text: 'Showcase', link: '/en/#showcase' },
          { text: 'Benchmarks', link: '/en/#benchmarks' },
          { text: 'Quick Start', link: '/en/#quickstart' },
          {
            text: 'Documentation',
            items: [
              { text: 'Docs Overview', link: '/en/docs/' },
              { text: 'Getting Started', link: '/en/docs/getting-started' },
              { text: 'Architecture & Pipeline', link: '/en/docs/architecture' },
              { text: 'AI Agent Guide', link: '/en/docs/agent-guide' },
              { text: 'C# Scripting', link: '/en/docs/csharp-development' },
              { text: 'OpenSCAD Pipeline', link: '/en/docs/scad-pipeline' },
              { text: '15+ Subprojects', link: '/en/docs/subprojects' }
            ]
          }
        ],
        sidebar: {
          '/en/docs/': [
            {
              text: 'Guides & Manuals',
              items: [
                { text: 'Overview', link: '/en/docs/' },
                { text: 'Getting Started', link: '/en/docs/getting-started' },
                { text: 'Architecture Design', link: '/en/docs/architecture' },
                { text: 'Subproject Prototypes', link: '/en/docs/subprojects' },
              ]
            },
            {
              text: 'Core Subsystems',
              items: [
                { text: 'C# Managed Scripting', link: '/en/docs/csharp-development' },
                { text: 'OpenSCAD Pipeline', link: '/en/docs/scad-pipeline' },
                { text: 'AI Agent & Verification', link: '/en/docs/agent-guide' },
              ]
            }
          ]
        },
        footer: {
          message: 'Released under the MIT License · Personal R&D Engine Playground',
          copyright: 'Copyright © 2026 gameknife / gkNextEngine Contributors'
        }
      }
    }
  },

  themeConfig: {
    logo: {
      light: '/brand/gknext_logo_icon.svg',
      dark: '/brand/gknext_logo_icon.svg',
      alt: 'gkNextEngine Logo'
    },
    socialLinks: [
      { icon: 'github', link: 'https://github.com/gameknife/gkNextEngine' }
    ]
  },

  vite: {
    plugins: [syncBrandAssetsPlugin()]
  }
})
