<template>
  <section class="ecosystem-section">
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

      <div class="tech-grid">
        <div v-for="(group, idx) in t.groups" :key="idx" class="tech-card gk-card">
          <div class="tech-category">{{ group.category }}</div>
          <div class="tech-items">
            <div v-for="(item, iIdx) in group.items" :key="iIdx" class="tech-tag">
              <span class="tech-name">{{ item.name }}</span>
              <span class="tech-role">{{ item.role }}</span>
            </div>
          </div>
        </div>
      </div>

      <!-- 底部致谢与许可协议 -->
      <div class="license-banner gk-card">
        <div class="license-info">
          <div class="license-title">{{ t.licenseTitle }}</div>
          <div class="license-desc">
            {{ t.licenseDesc }}
          </div>
        </div>
        <div class="license-actions">
          <a href="https://github.com/gameknife/gkNextEngine" target="_blank" rel="noreferrer" class="gh-star-btn">
            <svg class="star-icon" viewBox="0 0 24 24" fill="currentColor">
              <path d="M12 .587l3.668 7.431 8.2 1.192-5.934 5.784 1.399 8.16-7.333-3.856-7.333 3.856 1.399-8.16-5.934-5.784 8.2-1.192zm0 5.702l-2.232 4.523-4.991.725 3.612 3.521-.852 4.966 4.463-2.347 4.463 2.347-.852-4.966 3.612-3.521-4.991-.725z"/>
            </svg>
            {{ t.starBtn }}
          </a>
        </div>
      </div>
    </div>
  </section>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useData } from 'vitepress'
import { zhCN, enUS } from '../i18n'

const { lang } = useData()
const isEn = computed(() => lang.value === 'en-US')
const t = computed(() => (isEn.value ? enUS.ecosystem : zhCN.ecosystem))
</script>

<style scoped>
.ecosystem-section {
  padding: 100px 24px;
}

.container {
  max-width: 1120px;
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

.tech-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 20px;
  margin-bottom: 40px;
}

.tech-card {
  padding: 24px;
}

.tech-category {
  font-size: 0.82rem;
  font-weight: 700;
  color: var(--gk-accent-blue);
  text-transform: uppercase;
  letter-spacing: 0.05em;
  margin-bottom: 16px;
  border-bottom: 1px solid var(--vp-c-border);
  padding-bottom: 10px;
}

.tech-items {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 12px;
}

.tech-tag {
  display: flex;
  flex-direction: column;
  padding: 8px 12px;
  background: var(--vp-c-bg-soft);
  border: 1px solid var(--vp-c-border);
  border-radius: 6px;
  transition: all 0.2s ease;
}

.tech-tag:hover {
  border-color: rgba(255, 255, 255, 0.35);
  background: var(--vp-c-bg-elv);
}

.tech-name {
  font-size: 0.9rem;
  font-weight: 600;
  color: var(--vp-c-text-1);
  transition: color 0.2s ease;
}

.tech-tag:hover .tech-name {
  color: #ffffff;
}

.tech-role {
  font-size: 0.75rem;
  color: var(--vp-c-text-3);
  margin-top: 2px;
}

.license-banner {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 24px 32px;
  background: var(--vp-c-bg-soft);
  flex-wrap: wrap;
  gap: 20px;
}

.license-title {
  font-size: 1.1rem;
  font-weight: 700;
  color: #ffffff;
  margin-bottom: 4px;
}

.license-desc {
  font-size: 0.9rem;
  color: var(--vp-c-text-2);
}

.gh-star-btn {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  padding: 10px 20px;
  background: var(--vp-c-bg-elv);
  border: 1px solid var(--vp-c-border);
  border-radius: 8px;
  color: var(--vp-c-text-1);
  font-size: 0.9rem;
  font-weight: 600;
  text-decoration: none;
  transition: all 0.2s ease;
}

.gh-star-btn:hover {
  background: var(--vp-c-bg-soft);
  border-color: #ffffff;
  color: #ffffff !important;
  transform: translateY(-1px);
}

.star-icon {
  width: 16px;
  height: 16px;
  color: #ffffff;
}

@media (max-width: 768px) {
  .tech-grid {
    grid-template-columns: 1fr;
  }
  .tech-items {
    grid-template-columns: 1fr;
  }
  .license-banner {
    flex-direction: column;
    align-items: stretch;
  }
}
</style>
