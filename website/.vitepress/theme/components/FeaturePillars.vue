<template>
  <section id="features" class="features-section">
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

      <div class="features-grid">
        <div v-for="(feat, idx) in t.items" :key="idx" class="feature-card gk-card">
          <div class="card-glow" :style="{ background: feat.glowColor }"></div>
          
          <div class="card-header">
            <component :is="icons[idx]" class="card-title-icon" />
            <h3 class="card-title">{{ feat.title }}</h3>
          </div>

          <p class="card-summary">{{ feat.summary }}</p>
          
          <ul class="card-points">
            <li v-for="(pt, pIdx) in feat.points" :key="pIdx">
              <svg class="check-icon" viewBox="0 0 20 20" fill="currentColor">
                <path fill-rule="evenodd" d="M16.707 5.293a1 1 0 010 1.414l-8 8a1 1 0 01-1.414 0l-4-4a1 1 0 011.414-1.414L8 12.586l7.293-7.293a1 1 0 011.414 0z" clip-rule="evenodd" />
              </svg>
              <span>{{ pt }}</span>
            </li>
          </ul>
        </div>
      </div>
    </div>
  </section>
</template>

<script setup lang="ts">
import { h, computed } from 'vue'
import { useData } from 'vitepress'
import { zhCN, enUS } from '../i18n'

const { lang } = useData()
const isEn = computed(() => lang.value === 'en-US')
const t = computed(() => (isEn.value ? enUS.features : zhCN.features))

const RayTracingIcon = () => h('svg', { viewBox: '0 0 24 24', fill: 'none', stroke: 'currentColor', 'stroke-width': '2' }, [
  h('circle', { cx: '12', cy: '12', r: '4' }),
  h('path', { d: 'M12 2v2M12 20v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2M20 12h2M6.34 17.66l-1.41 1.41M19.07 4.93l-1.41 1.41' })
])

const GpuIcon = () => h('svg', { viewBox: '0 0 24 24', fill: 'none', stroke: 'currentColor', 'stroke-width': '2' }, [
  h('rect', { x: '4', y: '4', width: '16', height: '16', rx: '2' }),
  h('rect', { x: '9', y: '9', width: '6', height: '6' }),
  h('path', { d: 'M9 1v3M15 1v3M9 20v3M15 20v3M20 9h3M20 14h3M1 9h3M1 14h3' })
])

const AiIcon = () => h('svg', { viewBox: '0 0 24 24', fill: 'none', stroke: 'currentColor', 'stroke-width': '2' }, [
  h('path', { d: 'M12 2a8 8 0 0 0-8 8c0 3.37 2.08 6.26 5 7.42V20a2 2 0 0 0 2 2h2a2 2 0 0 0 2-2v-2.58c2.92-1.16 5-4.05 5-7.42a8 8 0 0 0-8-8z' }),
  h('line', { x1: '9', y1: '14', x2: '15', y2: '14' })
])

const ToolingIcon = () => h('svg', { viewBox: '0 0 24 24', fill: 'none', stroke: 'currentColor', 'stroke-width': '2' }, [
  h('polyline', { points: '16 18 22 12 16 6' }),
  h('polyline', { points: '8 6 2 12 8 18' }),
  h('line', { x1: '14', y1: '4', x2: '10', y2: '20' })
])

const icons = [RayTracingIcon, GpuIcon, AiIcon, ToolingIcon]
</script>

<style scoped>
.features-section {
  padding: 100px 24px;
  position: relative;
}

.container {
  max-width: 1120px;
  margin: 0 auto;
}

.section-header {
  text-align: center;
  margin-bottom: 64px;
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

.features-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 24px;
}

.feature-card {
  padding: 28px 30px;
  display: flex;
  flex-direction: column;
}

.card-glow {
  position: absolute;
  top: -40px;
  right: -40px;
  width: 160px;
  height: 160px;
  border-radius: 50%;
  filter: blur(50px);
  pointer-events: none;
}

.card-header {
  display: flex;
  align-items: center;
  gap: 10px;
  min-height: 32px;
  margin-bottom: 10px;
}

.card-title-icon {
  width: 22px;
  height: 22px;
  color: var(--gk-accent-blue);
  flex-shrink: 0;
  transition: all 0.2s ease;
}

.feature-card:hover .card-title-icon {
  color: #ffffff;
  transform: scale(1.08);
}

.card-title {
  font-size: 1.25rem;
  font-weight: 700;
  margin: 0;
  color: var(--vp-c-text-1);
  transition: color 0.2s ease;
}

.feature-card:hover .card-title {
  color: #ffffff;
}

.card-summary {
  font-size: 0.94rem;
  color: var(--vp-c-text-2);
  line-height: 1.6;
  margin: 0 0 16px;
}

.card-points {
  list-style: none;
  padding: 0;
  margin: 0;
  display: flex;
  flex-direction: column;
  gap: 12px;
  border-top: 1px solid var(--vp-c-border);
  padding-top: 18px;
}

.card-points li {
  display: flex;
  align-items: flex-start;
  gap: 10px;
  font-size: 0.9rem;
  color: var(--vp-c-text-2);
  line-height: 1.55;
}

.check-icon {
  width: 16px;
  height: 16px;
  color: var(--gk-accent-blue);
  opacity: 0.9;
  flex-shrink: 0;
  margin-top: 2px;
}

@media (max-width: 840px) {
  .features-grid {
    grid-template-columns: 1fr;
  }
  .feature-card {
    padding: 24px;
  }
}
</style>
