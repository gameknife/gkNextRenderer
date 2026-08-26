<template>
  <section id="quickstart" class="quickstart-section">
    <div class="container">
      <div class="section-header">
        <span class="gk-badge">{{ t.badge }}</span>
        <h2 class="section-title">
          <span class="gk-gradient-text">{{ t.title }}</span>
        </h2>
        <p class="section-desc">
          {{ t.desc }}
        </p>
      </div>

      <div class="cli-card gk-card">
        <!-- 平台切换与复制按钮 -->
        <div class="cli-header">
          <div class="cli-tabs">
            <button 
              v-for="platform in t.platforms" 
              :key="platform.id"
              class="platform-btn"
              :class="{ active: currentPlatform === platform.id }"
              @click="currentPlatform = platform.id"
            >
              <span class="platform-icon">{{ platform.icon }}</span>
              {{ platform.name }}
            </button>
          </div>

          <button class="copy-btn" @click="copyCommand">
            <svg v-if="!copied" class="copy-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect>
              <path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path>
            </svg>
            <svg v-else class="copy-icon text-green" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <polyline points="20 6 9 17 4 12"></polyline>
            </svg>
            <span>{{ copied ? t.copied : t.copyBtn }}</span>
          </button>
        </div>

        <!-- 终端命令行展示 -->
        <div class="cli-body">
          <pre class="cli-pre"><code class="cli-code" v-html="currentSnippetHtml"></code></pre>
        </div>

        <!-- 底部快捷提示 -->
        <div class="cli-footer">
          <div class="footer-tip">
            <span class="tip-dot"></span>
            <span>{{ t.footerTip }}</span>
          </div>
        </div>
      </div>
    </div>
  </section>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import { useData } from 'vitepress'
import { zhCN, enUS } from '../i18n'

const { lang } = useData()
const isEn = computed(() => lang.value === 'en-US')
const t = computed(() => (isEn.value ? enUS.quickstart : zhCN.quickstart))

const currentPlatform = ref('windows')
const copied = ref(false)

const snippetsZh: Record<string, string> = {
  windows: `<span class="c-comment"># 1. 克隆代码仓库</span>
<span class="c-cmd">git clone</span> https://github.com/gameknife/gkNextEngine.git
<span class="c-cmd">cd</span> gkNextEngine

<span class="c-comment"># 2. 一键准备依赖 (自动下载 Vulkan SDK 1.4 + Slang + vcpkg)</span>
<span class="c-cmd">./gnb.bat setup</span>

<span class="c-comment"># 3. 极速增量构建核心目标 (gkNextRenderer + gkNextUnitTests)</span>
<span class="c-cmd">./gnb.bat build</span>

<span class="c-comment"># 4. 运行主渲染器</span>
<span class="c-cmd">./gnb.bat run gkNextRenderer</span>`,

  linux: `<span class="c-comment"># 1. 克隆代码仓库</span>
<span class="c-cmd">git clone</span> https://github.com/gameknife/gkNextEngine.git
<span class="c-cmd">cd</span> gkNextEngine

<span class="c-comment"># 2. 自动检查系统包并安装依赖工具链</span>
<span class="c-cmd">./gnb.sh setup</span>

<span class="c-comment"># 3. 编译核心渲染引擎</span>
<span class="c-cmd">./gnb.sh build</span>

<span class="c-comment"># 4. 运行主渲染器 (或 ./gnb.sh tui 终端字符画模式)</span>
<span class="c-cmd">./gnb.sh run gkNextRenderer</span>`,

  macos: `<span class="c-comment"># 1. 克隆代码仓库</span>
<span class="c-cmd">git clone</span> https://github.com/gameknife/gkNextEngine.git
<span class="c-cmd">cd</span> gkNextEngine

<span class="c-comment"># 2. 准备依赖 (自动下载 arm64 对应工具链)</span>
<span class="c-cmd">./gnb.sh setup</span>

<span class="c-comment"># 3. 编译并启动</span>
<span class="c-cmd">./gnb.sh build</span>
<span class="c-cmd">./gnb.sh run gkNextRenderer</span>`,

  remote: `<span class="c-comment"># 任意桌面 Target 可原生作为 WebRTC Host 启动</span>
<span class="c-cmd">./gnb.sh remote</span> --target gkNextRenderer --scene assets/models/playground.glb --res 1280x720

<span class="c-comment"># 终端输出: [Remote] WebRTC signaling ready on http://127.0.0.1:8080</span>
<span class="c-comment"># 手机或任意电脑浏览器打开链接，即可 60FPS 零安装游玩并支持键鼠/虚拟手柄！</span>`
}

const snippetsEn: Record<string, string> = {
  windows: `<span class="c-comment"># 1. Clone repository</span>
<span class="c-cmd">git clone</span> https://github.com/gameknife/gkNextEngine.git
<span class="c-cmd">cd</span> gkNextEngine

<span class="c-comment"># 2. One-click setup (auto downloads Vulkan SDK 1.4 + Slang + vcpkg)</span>
<span class="c-cmd">./gnb.bat setup</span>

<span class="c-comment"># 3. Fast incremental build for core targets (gkNextRenderer + gkNextUnitTests)</span>
<span class="c-cmd">./gnb.bat build</span>

<span class="c-comment"># 4. Run main renderer</span>
<span class="c-cmd">./gnb.bat run gkNextRenderer</span>`,

  linux: `<span class="c-comment"># 1. Clone repository</span>
<span class="c-cmd">git clone</span> https://github.com/gameknife/gkNextEngine.git
<span class="c-cmd">cd</span> gkNextEngine

<span class="c-comment"># 2. Automatically install system packages and tools</span>
<span class="c-cmd">./gnb.sh setup</span>

<span class="c-comment"># 3. Build core engine</span>
<span class="c-cmd">./gnb.sh build</span>

<span class="c-comment"># 4. Run main renderer (or ./gnb.sh tui for terminal mode)</span>
<span class="c-cmd">./gnb.sh run gkNextRenderer</span>`,

  macos: `<span class="c-comment"># 1. Clone repository</span>
<span class="c-cmd">git clone</span> https://github.com/gameknife/gkNextEngine.git
<span class="c-cmd">cd</span> gkNextEngine

<span class="c-comment"># 2. Setup arm64 dependencies</span>
<span class="c-cmd">./gnb.sh setup</span>

<span class="c-comment"># 3. Build and launch</span>
<span class="c-cmd">./gnb.sh build</span>
<span class="c-cmd">./gnb.sh run gkNextRenderer</span>`,

  remote: `<span class="c-comment"># Launch any desktop target as a native WebRTC host</span>
<span class="c-cmd">./gnb.sh remote</span> --target gkNextRenderer --scene assets/models/playground.glb --res 1280x720

<span class="c-comment"># Output: [Remote] WebRTC signaling ready on http://127.0.0.1:8080</span>
<span class="c-comment"># Open the URL in any browser for zero-install 60FPS play!</span>`
}

const rawCommands: Record<string, string> = {
  windows: `git clone https://github.com/gameknife/gkNextEngine.git\ncd gkNextEngine\n./gnb.bat setup\n./gnb.bat build\n./gnb.bat run gkNextRenderer`,
  linux: `git clone https://github.com/gameknife/gkNextEngine.git\ncd gkNextEngine\n./gnb.sh setup\n./gnb.sh build\n./gnb.sh run gkNextRenderer`,
  macos: `git clone https://github.com/gameknife/gkNextEngine.git\ncd gkNextEngine\n./gnb.sh setup\n./gnb.sh build\n./gnb.sh run gkNextRenderer`,
  remote: `./gnb.sh remote --target gkNextRenderer --scene assets/models/playground.glb --res 1280x720`
}

const currentSnippetHtml = computed(() => {
  const dict = isEn.value ? snippetsEn : snippetsZh
  return dict[currentPlatform.value]
})

const copyCommand = async () => {
  try {
    await navigator.clipboard.writeText(rawCommands[currentPlatform.value])
    copied.value = true
    setTimeout(() => {
      copied.value = false
    }, 2000)
  } catch (err) {
    console.error('Copy failed:', err)
  }
}
</script>

<style scoped>
.quickstart-section {
  padding: 100px 24px;
  background: var(--vp-c-bg-soft);
  border-top: 1px solid var(--vp-c-border);
}

.container {
  max-width: 1000px;
  margin: 0 auto;
}

.section-header {
  text-align: center;
  margin-bottom: 56px;
  display: flex;
  flex-direction: column;
  align-items: center;
}

.section-header .gk-badge {
  margin-bottom: 20px;
}

.section-title {
  font-size: clamp(2rem, 3.8vw, 2.85rem);
  font-weight: 800;
  line-height: 1.25;
  margin: 0 0 22px;
  letter-spacing: -0.02em;
}

.section-desc {
  font-size: 1.1rem;
  color: var(--vp-c-text-2);
  max-width: 720px;
  margin: 0 auto;
  line-height: 1.8;
  letter-spacing: 0.01em;
}

.cli-card {
  box-shadow: 0 20px 40px rgba(0, 0, 0, 0.3);
  border-radius: 12px;
}

.cli-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 8px 16px;
  background: var(--vp-c-bg-soft);
  border-bottom: 1px solid var(--vp-c-border);
  flex-wrap: wrap;
  gap: 10px;
}

.cli-tabs {
  display: flex;
  gap: 6px;
}

.platform-btn {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 6px 12px;
  background: transparent;
  border: 1px solid transparent;
  border-radius: 6px;
  color: var(--vp-c-text-2);
  font-size: 0.82rem;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s ease;
}

.platform-btn:hover {
  color: #ffffff;
}

.platform-btn.active {
  background: var(--vp-c-bg-elv);
  color: #ffffff;
  border-color: rgba(255, 255, 255, 0.3);
}

.platform-icon {
  font-size: 0.9rem;
}

.copy-btn {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 6px 12px;
  border-radius: 6px;
  background: var(--vp-c-bg-alt);
  border: 1px solid var(--vp-c-border);
  color: var(--vp-c-text-2);
  font-size: 0.8rem;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s ease;
}

.copy-btn:hover {
  background: var(--vp-c-bg-soft);
  border-color: #ffffff;
  color: #ffffff;
}

.copy-icon {
  width: 14px;
  height: 14px;
}

.text-green {
  color: #ffffff;
}

.cli-body {
  padding: 24px;
  background: #090a0d;
  overflow-x: auto;
}

.cli-pre {
  margin: 0;
  font-family: var(--gk-mono);
  font-size: 0.92rem;
  line-height: 1.7;
}

:deep(.c-comment) {
  color: #71717a;
  user-select: none;
}

:deep(.c-cmd) {
  color: var(--gk-accent-blue);
  font-weight: 600;
}

.cli-footer {
  padding: 12px 20px;
  background: var(--vp-c-bg-soft);
  border-top: 1px solid var(--vp-c-border);
}

.footer-tip {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 0.82rem;
  color: var(--vp-c-text-2);
}

.tip-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: var(--gk-accent-blue);
  box-shadow: 0 0 6px var(--gk-accent-blue);
  flex-shrink: 0;
}

@media (max-width: 700px) {
  .cli-header {
    flex-direction: column;
    align-items: stretch;
  }
  .cli-tabs {
    overflow-x: auto;
  }
}
</style>
