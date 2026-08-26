<template>
  <div class="hero-container gk-grid-bg">
    <!-- 科技感背景光晕 -->
    <div class="gk-radial-glow glow-left"></div>
    <div class="gk-radial-glow glow-right"></div>
    
    <div class="hero-wrapper">
      <!-- 左右两栏布局 -->
      <div class="hero-split">
        <!-- 左栏：标语、介绍、CTA 按钮 -->
        <div class="hero-left">
          <h1 class="hero-title">
            <span class="hero-title-main">{{ t.title1 }}</span><br />
            <span class="hero-title-sub">{{ t.title2 }}</span>
          </h1>

          <p class="hero-subtitle">
            {{ t.subtitle }}
          </p>

          <!-- CTA 操作按钮 -->
          <div class="hero-actions">
            <a :href="isEn ? '/en/#quickstart' : '#quickstart'" class="btn btn-primary">
              <svg class="btn-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                <polyline points="4 17 10 11 4 5"></polyline>
                <line x1="12" y1="19" x2="20" y2="19"></line>
              </svg>
              {{ t.quickstart }}
            </a>
            <a href="https://github.com/gameknife/gkNextEngine" target="_blank" rel="noreferrer" class="btn btn-secondary">
              <svg class="btn-icon" viewBox="0 0 24 24" fill="currentColor">
                <path d="M12 0C5.37 0 0 5.37 0 12c0 5.31 3.435 9.795 8.205 11.385.6.105.825-.255.825-.57 0-.285-.015-1.23-.015-2.235-3.015.555-3.795-.735-4.035-1.41-.135-.345-.72-1.41-1.23-1.695-.42-.225-1.02-.78-.015-.795.945-.015 1.62.87 1.845 1.23 1.08 1.815 2.805 1.305 3.495.99.105-.78.42-1.305.765-1.605-2.67-.3-5.46-1.335-5.46-5.925 0-1.305.465-2.385 1.23-3.225-.12-.3-.54-1.53.12-3.18 0 0 1.005-.315 3.3 1.23.96-.27 1.98-.405 3-.405s2.04.135 3 .405c2.295-1.56 3.3-1.23 3.3-1.23.66 1.65.24 2.88.12 3.18.765.84 1.23 1.905 1.23 3.225 0 4.605-2.805 5.625-5.475 5.925.435.375.81 1.095.81 2.22 0 1.605-.015 2.895-.015 3.3 0 .315.225.69.825.57A12.02 12.02 0 0024 12c0-6.63-5.37-12-12-12z"/>
              </svg>
              {{ t.github }}
            </a>
            <a :href="isEn ? '/en/docs/' : '/docs/'" class="btn btn-tertiary">
              {{ t.docs }}
            </a>
          </div>
        </div>

        <!-- 右栏：交互式渲染视窗与 HUD -->
        <div class="hero-right">
          <div class="viewport-card gk-card">
            <!-- 视窗顶部状态栏 -->
            <div class="viewport-header">
              <div class="window-controls">
                <span class="dot dot-red"></span>
                <span class="dot dot-yellow"></span>
                <span class="dot dot-green"></span>
                <span class="window-title">{{ activeScene.title }}</span>
              </div>
              <div class="hud-status">
                <span class="hud-tag"><span class="pulse-indicator"></span> {{ activeScene.fps }}</span>
                <span class="hud-tag">{{ activeScene.pipeline }}</span>
                <span class="hud-tag">{{ activeScene.vram }}</span>
              </div>
            </div>

            <!-- 渲染画面 -->
            <div class="viewport-screen">
              <img 
                :src="activeScene.image" 
                :alt="activeScene.title"
                class="viewport-img"
                loading="eager"
              />
              <div class="viewport-overlay">
                <div class="overlay-info">
                  <div class="scene-name">{{ activeScene.name }}</div>
                  <div class="scene-desc">{{ activeScene.desc }}</div>
                </div>
              </div>
            </div>

            <!-- 底部 4 场景切换 Tab -->
            <div class="viewport-tabs">
              <button 
                v-for="(scene, idx) in t.scenes" 
                :key="idx"
                class="tab-btn"
                :class="{ active: currentSceneIndex === idx }"
                @click="currentSceneIndex = idx"
              >
                <span class="tab-index">0{{ idx + 1 }}</span>
                <span class="tab-label">{{ scene.name }}</span>
              </button>
            </div>
          </div>
        </div>
      </div>

      <!-- 底部向下滑动提示指示器 -->
      <a :href="isEn ? '/en/#features' : '#features'" class="scroll-hint">
        <span class="scroll-text">{{ t.scrollHint }}</span>
        <svg class="scroll-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <path d="M7 13l5 5 5-5M7 6l5 5 5-5" />
        </svg>
      </a>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import { useData } from 'vitepress'
import { zhCN, enUS } from '../i18n'

const { lang } = useData()
const isEn = computed(() => lang.value === 'en-US')
const t = computed(() => (isEn.value ? enUS.hero : zhCN.hero))

const currentSceneIndex = ref(0)
const activeScene = computed(() => t.value.scenes[currentSceneIndex.value] || t.value.scenes[0])
</script>

<style scoped>
.hero-container {
  position: relative;
  width: 100%;
  height: calc(100vh - var(--vp-nav-height, 64px));
  min-height: 560px;
  display: flex;
  flex-direction: column;
  justify-content: center;
  align-items: center;
  padding: 0 32px 16px;
  box-sizing: border-box;
  overflow: hidden;
}

.glow-left {
  top: 10%;
  left: -100px;
}

.glow-right {
  bottom: 10%;
  right: -100px;
}

.hero-wrapper {
  max-width: 1280px;
  width: 100%;
  height: 100%;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  position: relative;
  z-index: 1;
  padding-top: 16px;
}

.hero-split {
  display: grid;
  grid-template-columns: 1.05fr 0.95fr;
  gap: 36px;
  align-items: center;
  flex-grow: 1;
}

.hero-left {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  text-align: left;
}

.hero-title {
  font-size: clamp(2rem, 3.4vw, 3.25rem);
  font-weight: 800;
  line-height: 1.2;
  letter-spacing: -0.03em;
  margin: 0 0 20px;
}

.hero-title-main {
  color: #ffffff;
  display: inline-block;
  text-shadow: 0 2px 20px rgba(255, 255, 255, 0.15);
}

.hero-title-sub {
  color: var(--gk-accent-blue);
  font-weight: 700;
  display: inline-block;
  margin-top: 6px;
  background: linear-gradient(135deg, var(--gk-accent-blue) 30%, #8bb5e0 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
}

.hero-subtitle {
  font-size: clamp(0.95rem, 1.3vw, 1.15rem);
  color: var(--vp-c-text-2);
  line-height: 1.65;
  margin: 0 0 36px;
  max-width: 580px;
}

.hero-actions {
  display: flex;
  flex-wrap: wrap;
  gap: 14px;
  align-items: center;
}

.btn {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  padding: 12px 22px;
  border-radius: 8px;
  font-size: 0.92rem;
  font-weight: 600;
  text-decoration: none;
  transition: all 0.2s ease;
}

.btn-icon {
  width: 16px;
  height: 16px;
}

.btn-primary {
  background: #ffffff;
  color: #0c0d10 !important;
  border: 1px solid #ffffff;
  box-shadow: 0 0 15px rgba(255, 255, 255, 0.2);
}

.btn-primary:hover {
  background: #ffffff;
  color: #000000 !important;
  box-shadow: 0 0 25px rgba(255, 255, 255, 0.45);
  transform: translateY(-1px);
}

.btn-secondary {
  background: var(--vp-c-bg-elv);
  color: var(--vp-c-text-1);
  border: 1px solid var(--vp-c-border);
}

.btn-secondary:hover {
  background: var(--vp-c-bg-soft);
  border-color: #ffffff;
  color: #ffffff !important;
  transform: translateY(-1px);
}

.btn-tertiary {
  background: transparent;
  color: var(--vp-c-text-2);
  border: 1px solid transparent;
}

.btn-tertiary:hover {
  color: #ffffff !important;
}

.hero-right {
  display: flex;
  justify-content: center;
  align-items: center;
  width: 100%;
}

.viewport-card {
  width: 100%;
  max-width: 580px;
  box-shadow: 0 20px 50px rgba(0, 0, 0, 0.4);
  border-radius: 12px;
}

.viewport-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 14px;
  background: var(--vp-c-bg-soft);
  border-bottom: 1px solid var(--vp-c-border);
}

.window-controls {
  display: flex;
  align-items: center;
  gap: 6px;
}

.dot {
  width: 9px;
  height: 9px;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.2);
}
.dot-red { background: rgba(255, 255, 255, 0.2); }
.dot-yellow { background: rgba(255, 255, 255, 0.35); }
.dot-green { background: rgba(255, 255, 255, 0.6); }

.window-title {
  font-size: 0.75rem;
  color: var(--vp-c-text-3);
  font-family: var(--gk-mono);
  margin-left: 6px;
}

.hud-status {
  display: flex;
  align-items: center;
  gap: 6px;
}

.hud-tag {
  font-size: 0.7rem;
  font-family: var(--gk-mono);
  color: var(--vp-c-text-2);
  background: var(--vp-c-bg-alt);
  border: 1px solid var(--vp-c-border);
  padding: 1px 6px;
  border-radius: 4px;
  display: flex;
  align-items: center;
  gap: 4px;
}

.pulse-indicator {
  width: 5px;
  height: 5px;
  border-radius: 50%;
  background: var(--gk-accent-blue);
  box-shadow: 0 0 6px var(--gk-accent-blue);
}

.viewport-screen {
  position: relative;
  width: 100%;
  height: 0;
  padding-bottom: 56.25%;
  background: #000;
  overflow: hidden;
}

.viewport-img {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  object-fit: cover;
  transition: opacity 0.3s ease;
}

.viewport-overlay {
  position: absolute;
  bottom: 0;
  left: 0;
  right: 0;
  padding: 16px 14px 10px;
  background: linear-gradient(to top, rgba(0, 0, 0, 0.85) 0%, transparent 100%);
  display: flex;
  justify-content: flex-start;
  text-align: left;
}

.scene-name {
  font-size: 0.95rem;
  font-weight: 700;
  color: #fff;
}

.scene-desc {
  font-size: 0.78rem;
  color: #d1d5db;
  margin-top: 1px;
}

.viewport-tabs {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  background: var(--vp-c-bg-soft);
  border-top: 1px solid var(--vp-c-border);
}

.tab-btn {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  padding: 8px 6px;
  background: transparent;
  border: none;
  border-right: 1px solid var(--vp-c-border);
  color: var(--vp-c-text-3);
  cursor: pointer;
  font-size: 0.75rem;
  font-weight: 500;
  transition: all 0.2s ease;
}

.tab-btn:last-child {
  border-right: none;
}

.tab-btn:hover {
  color: #ffffff;
  background: var(--vp-c-bg-alt);
}

.tab-btn.active {
  color: #ffffff;
  background: var(--vp-c-bg-alt);
  border-bottom: 2px solid var(--gk-accent-blue);
}

.tab-index {
  font-family: var(--gk-mono);
  font-size: 0.7rem;
  opacity: 0.6;
}

.scroll-hint {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 4px;
  color: var(--vp-c-text-3);
  text-decoration: none;
  margin-top: auto;
  padding: 8px 0 4px;
  transition: color 0.2s ease;
}

.scroll-hint:hover {
  color: #ffffff;
}

.scroll-text {
  font-size: 0.72rem;
  letter-spacing: 0.05em;
  text-transform: uppercase;
  font-weight: 600;
}

.scroll-icon {
  width: 14px;
  height: 14px;
  animation: bounce 2s infinite;
}

@keyframes bounce {
  0%, 20%, 50%, 80%, 100% {
    transform: translateY(0);
  }
  40% {
    transform: translateY(4px);
  }
  60% {
    transform: translateY(2px);
  }
}

@media (max-width: 1024px) {
  .hero-container {
    height: auto;
    min-height: calc(100svh - var(--vp-nav-height, 64px));
    padding: 30px 20px 20px;
  }
  .hero-split {
    grid-template-columns: 1fr;
    gap: 28px;
  }
  .hero-left {
    align-items: center;
    text-align: center;
  }
  .hero-subtitle {
    max-width: 100%;
  }
  .hero-actions {
    justify-content: center;
  }
}
</style>
