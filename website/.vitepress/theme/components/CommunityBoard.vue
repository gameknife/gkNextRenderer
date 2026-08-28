<template>
  <section class="board">
    <div class="container">
      <header class="board-header">
        <span class="gk-badge">{{ t.badge }}</span>
        <h1 class="board-title"><span class="gk-gradient-text">{{ t.title }}</span></h1>
        <p v-if="t.desc" class="board-desc">{{ t.desc }}</p>
      </header>

      <p class="board-notice">{{ t.notice }}</p>

      <ul v-if="topics.length" class="topic-list">
        <li v-for="topic in topics" :key="topic.number" class="topic-card gk-card">
          <a class="topic-link" :href="topicHref(topic.number)">
            <div class="topic-main">
              <h2 class="topic-title">{{ topic.title }}</h2>
              <p v-if="topic.excerpt" class="topic-excerpt">{{ topic.excerpt }}</p>
              <div v-if="topic.labels.length" class="topic-labels">
                <span v-for="label in topic.labels" :key="label.name" class="label-pill">{{ label.name }}</span>
              </div>
              <div class="topic-meta">
                <img v-if="topic.author" class="topic-avatar" :src="topic.author.avatar" :alt="topic.author.login" loading="lazy" />
                <span v-if="topic.author" class="meta-item">{{ topic.author.login }}</span>
                <span class="meta-sep">·</span>
                <span class="meta-item">{{ day(topic.createdAt) }}</span>
              </div>
            </div>
            <div class="topic-stats">
              <div class="stat">
                <span class="stat-num">{{ topic.replies }}</span>
                <span class="stat-label">{{ t.replies }}</span>
              </div>
              <div class="stat">
                <span class="stat-num">{{ topic.upvotes }}</span>
                <span class="stat-label">{{ t.upvotes }}</span>
              </div>
            </div>
          </a>
        </li>
      </ul>

      <div v-else class="board-empty gk-card">
        <p class="empty-title">{{ t.empty }}</p>
        <p class="empty-hint">{{ t.emptyHint }}</p>
      </div>

      <p class="board-foot">
        <a :href="discussionsCategoryUrl" target="_blank" rel="noopener">{{ t.viewOnGitHub }} →</a>
      </p>
    </div>
  </section>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useData, withBase } from 'vitepress'
import { zhCN, enUS } from '../i18n'
import { discussionsCategoryUrl } from '../../community.mjs'
import snapshot from '../../data/discussions.json'

const { lang } = useData()
const t = computed(() => (lang.value === 'en-US' ? enUS.community : zhCN.community))

const topics = snapshot.topics

// 用 ISO 串直接切，而不是 toLocaleDateString —— 后者在 SSR 和浏览器里
// 可能给出不同结果，会触发 hydration 不一致。
const day = (iso: string) => iso.slice(0, 10)

const topicHref = (n: number) =>
  withBase(lang.value === 'en-US' ? `/en/community/${n}` : `/community/${n}`)
</script>

<style scoped>
.board {
  padding: 96px 24px 120px;
  min-height: 70vh;
}

.container {
  max-width: 880px;
  margin: 0 auto;
}

.board-header {
  display: flex;
  flex-direction: column;
  align-items: center;
  text-align: center;
  margin-bottom: 32px;
}

.board-header .gk-badge {
  margin-bottom: 20px;
}

.board-title {
  font-size: clamp(1.9rem, 3.4vw, 2.6rem);
  font-weight: 800;
  letter-spacing: -0.02em;
  line-height: 1.25;
  margin: 0 0 18px;
  border: 0;
  padding: 0;
}

.board-desc {
  max-width: 620px;
  margin: 0;
  color: var(--vp-c-text-2);
  line-height: 1.75;
}

.board-notice {
  margin: 0 0 40px;
  padding: 14px 18px;
  border-left: 2px solid var(--gk-accent-blue);
  background: var(--gk-accent-blue-soft);
  border-radius: 0 6px 6px 0;
  color: var(--vp-c-text-2);
  font-size: 0.88rem;
  line-height: 1.7;
}

.topic-list {
  list-style: none;
  padding: 0;
  margin: 0;
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.topic-card {
  border: 1px solid var(--gk-card-border);
  background: var(--gk-card-bg);
  border-radius: 10px;
  transition: border-color 0.2s ease, transform 0.2s ease;
}

.topic-card:hover {
  border-color: var(--gk-card-hover-border);
  transform: translateY(-2px);
}

.topic-link {
  display: flex;
  align-items: flex-start;
  gap: 24px;
  padding: 22px 24px;
  color: inherit;
  text-decoration: none;
}

.topic-main {
  flex: 1;
  min-width: 0;
}

.topic-title {
  margin: 0 0 8px;
  font-size: 1.08rem;
  font-weight: 650;
  line-height: 1.45;
  color: var(--vp-c-text-1);
  border: 0;
  padding: 0;
}

.topic-card:hover .topic-title {
  color: var(--gk-accent-blue);
}

.topic-excerpt {
  margin: 0 0 12px;
  font-size: 0.88rem;
  line-height: 1.7;
  color: var(--vp-c-text-2);
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
  overflow: hidden;
}

.topic-labels {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  margin-bottom: 10px;
}

.label-pill {
  font-size: 0.72rem;
  padding: 2px 9px;
  border-radius: 999px;
  border: 1px solid var(--gk-accent-blue-border);
  color: var(--gk-accent-blue);
}

.topic-meta {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 0.78rem;
  color: var(--vp-c-text-3);
}

.topic-avatar {
  width: 18px;
  height: 18px;
  border-radius: 50%;
}

.meta-sep {
  opacity: 0.5;
}

.topic-stats {
  display: flex;
  gap: 20px;
  flex-shrink: 0;
  padding-top: 4px;
}

.stat {
  display: flex;
  flex-direction: column;
  align-items: center;
  min-width: 40px;
}

.stat-num {
  font-size: 1.1rem;
  font-weight: 700;
  color: var(--vp-c-text-1);
  font-variant-numeric: tabular-nums;
}

.stat-label {
  font-size: 0.7rem;
  color: var(--vp-c-text-3);
}

.board-empty {
  padding: 56px 24px;
  text-align: center;
  border: 1px dashed var(--gk-card-border);
  border-radius: 10px;
}

.empty-title {
  margin: 0 0 8px;
  font-weight: 650;
  color: var(--vp-c-text-1);
}

.empty-hint {
  margin: 0;
  font-size: 0.86rem;
  color: var(--vp-c-text-3);
}

.board-foot {
  margin-top: 32px;
  text-align: center;
  font-size: 0.88rem;
}

.board-foot a {
  color: var(--gk-accent-blue);
  text-decoration: none;
}

.board-foot a:hover {
  text-decoration: underline;
}

@media (max-width: 640px) {
  .topic-link {
    flex-direction: column;
    gap: 14px;
  }

  .topic-stats {
    gap: 24px;
    padding-top: 0;
  }
}
</style>
