<template>
  <section id="showcase" class="showcase-section">
    <div class="container">
      <div class="section-header">
        <span class="gk-badge gk-badge-amber">{{ t.badge }}</span>
        <h2 class="section-title">
          <span class="gk-gradient-text">{{ t.title }}</span>
        </h2>
        <p class="section-desc">
          {{ t.desc }}
        </p>

        <!-- 筛选 Tabs -->
        <div class="category-tabs">
          <button 
            v-for="cat in t.categories" 
            :key="cat.id"
            class="cat-btn"
            :class="{ active: currentCategory === cat.id }"
            @click="currentCategory = cat.id"
          >
            {{ cat.name }}
            <span class="count-badge">{{ getCategoryCount(cat.id) }}</span>
          </button>
        </div>
      </div>

      <!-- 项目卡片网格 -->
      <div class="showcase-grid">
        <div 
          v-for="item in filteredProjects" 
          :key="item.id" 
          class="project-card gk-card"
        >
          <div class="project-img-box">
            <img :src="item.image" :alt="item.name" class="project-img" loading="lazy" />
            <div class="project-badge">{{ item.categoryName }}</div>
          </div>

          <div class="project-body">
            <div class="project-header">
              <h3 class="project-name">{{ item.name }}</h3>
              <span class="project-role">{{ item.role }}</span>
            </div>
            
            <p class="project-desc">{{ item.desc }}</p>

            <div class="project-tags">
              <span v-for="(tag, tIdx) in item.tags" :key="tIdx" class="tag-pill">
                {{ tag }}
              </span>
            </div>

            <div class="project-footer">
              <span class="cmd-label">Run:</span>
              <code class="cmd-code">./gnb.sh run {{ item.target }}</code>
            </div>
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
const t = computed(() => (isEn.value ? enUS.showcase : zhCN.showcase))

const currentCategory = ref('all')

const filteredProjects = computed(() => {
  if (currentCategory.value === 'all') return t.value.projects
  return t.value.projects.filter(p => p.category === currentCategory.value)
})

const getCategoryCount = (catId: string) => {
  if (catId === 'all') return t.value.projects.length
  return t.value.projects.filter(p => p.category === catId).length
}
</script>

<style scoped>
.showcase-section {
  padding: 100px 24px;
  background: var(--vp-c-bg-soft);
  border-top: 1px solid var(--vp-c-border);
  border-bottom: 1px solid var(--vp-c-border);
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
  margin: 0 auto 36px;
  line-height: 1.8;
  letter-spacing: 0.01em;
}

/* 分类 Tabs */
.category-tabs {
  display: inline-flex;
  gap: 6px;
  padding: 4px;
  background: var(--vp-c-bg-alt);
  border: 1px solid var(--vp-c-border);
  border-radius: 10px;
}

.cat-btn {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 8px 16px;
  border-radius: 6px;
  border: none;
  background: transparent;
  color: var(--vp-c-text-2);
  font-size: 0.88rem;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s ease;
}

.cat-btn:hover {
  color: #ffffff;
}

.cat-btn.active {
  background: var(--vp-c-bg-elv);
  color: #ffffff;
  border: 1px solid rgba(255, 255, 255, 0.25);
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.2);
}

.count-badge {
  font-size: 0.72rem;
  padding: 2px 6px;
  border-radius: 9999px;
  background: var(--vp-c-bg-soft);
  font-family: var(--gk-mono);
  color: var(--vp-c-text-3);
}

/* 网格布局 */
.showcase-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 24px;
}

.project-card {
  display: flex;
  flex-direction: column;
}

.project-img-box {
  position: relative;
  width: 100%;
  height: 180px;
  background: #000;
  overflow: hidden;
}

.project-img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  transition: transform 0.3s ease;
}

.project-card:hover .project-img {
  transform: scale(1.04);
}

.project-badge {
  position: absolute;
  top: 10px;
  right: 10px;
  background: rgba(0, 0, 0, 0.75);
  backdrop-filter: blur(8px);
  border: 1px solid rgba(255, 255, 255, 0.15);
  padding: 3px 8px;
  border-radius: 4px;
  font-size: 0.75rem;
  color: #fff;
  font-weight: 500;
}

.project-body {
  padding: 20px;
  display: flex;
  flex-direction: column;
  flex-grow: 1;
}

.project-header {
  margin-bottom: 8px;
}

.project-name {
  font-size: 1.15rem;
  font-weight: 700;
  color: var(--vp-c-text-1);
  margin: 0 0 2px;
  transition: color 0.2s ease;
}

.project-card:hover .project-name {
  color: #ffffff;
}

.project-role {
  font-size: 0.8rem;
  color: var(--gk-accent-blue);
  font-weight: 500;
}

.project-desc {
  font-size: 0.88rem;
  color: var(--vp-c-text-2);
  line-height: 1.5;
  margin: 0 0 16px;
  flex-grow: 1;
}

.project-tags {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  margin-bottom: 16px;
}

.tag-pill {
  font-size: 0.75rem;
  background: var(--vp-c-bg-soft);
  border: 1px solid var(--vp-c-border);
  padding: 2px 8px;
  border-radius: 4px;
  color: var(--vp-c-text-3);
}

.project-footer {
  display: flex;
  align-items: center;
  gap: 8px;
  padding-top: 12px;
  border-top: 1px solid var(--vp-c-border);
  font-size: 0.8rem;
}

.cmd-label {
  color: var(--vp-c-text-3);
  font-weight: 500;
}

.cmd-code {
  font-family: var(--gk-mono);
  color: var(--gk-accent-blue);
  background: var(--vp-c-bg-soft);
  border: 1px solid var(--vp-c-border);
  padding: 2px 6px;
  border-radius: 4px;
  font-size: 0.78rem;
}

.project-card:hover .cmd-code {
  color: #ffffff;
  border-color: var(--gk-accent-blue);
}

@media (max-width: 960px) {
  .showcase-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}

@media (max-width: 640px) {
  .showcase-grid {
    grid-template-columns: 1fr;
  }
  .category-tabs {
    flex-wrap: wrap;
    justify-content: center;
  }
}
</style>
