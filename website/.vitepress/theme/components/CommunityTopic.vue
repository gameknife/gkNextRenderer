<template>
  <section class="topic">
    <div class="container">
      <a class="back" :href="boardHref">{{ t.backToBoard }}</a>

      <template v-if="topic">
        <header class="topic-head">
          <h1 class="topic-title">{{ topic.title }}</h1>
          <div class="topic-meta">
            <img v-if="topic.author" class="avatar" :src="topic.author.avatar" :alt="topic.author.login" loading="lazy" />
            <span v-if="topic.author">{{ t.postedBy }} {{ topic.author.login }}</span>
            <span class="sep">·</span>
            <span>{{ day(topic.createdAt) }}</span>
            <span class="sep">·</span>
            <a :href="topic.url" target="_blank" rel="noopener">{{ t.viewOnGitHub }} ↗</a>
          </div>
          <div v-if="topic.labels.length" class="labels">
            <span v-for="label in topic.labels" :key="label.name" class="label-pill">{{ label.name }}</span>
          </div>
        </header>

        <!-- bodyHTML 是 GitHub 渲染并 sanitize 过的正文，这里只负责套排版样式。 -->
        <article class="topic-body markdown-body" v-html="topic.bodyHTML" />

        <section class="replies">
          <div class="replies-head">
            <h2>{{ t.repliesHeading }}</h2>
            <span class="replies-count">{{ topic.replies }}</span>
          </div>
          <p class="replies-hint">{{ topic.locked ? t.lockedHint : t.signInHint }}</p>
          <GiscusThread :term="topic.number" />
        </section>
      </template>

      <div v-else class="missing gk-card">
        <p class="empty-title">{{ t.empty }}</p>
        <p class="empty-hint">{{ t.emptyHint }}</p>
      </div>
    </div>
  </section>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useData, withBase } from 'vitepress'
import { zhCN, enUS } from '../i18n'
import snapshot from '../../data/discussions.json'
import GiscusThread from './GiscusThread.vue'

const { lang, params } = useData()

const t = computed(() => (lang.value === 'en-US' ? enUS.community : zhCN.community))

// 动态路由 community/[number].md 把讨论号放在 params 里；
// 正文本身来自构建期抓下来的快照，不进 markdown（讨论正文含 {{ }} 之类的
// 内容会被 Vue 模板编译器当成插值，走 v-html 才安全）。
const topic = computed(() => {
  const number = Number(params.value?.number)
  return snapshot.topics.find((item) => item.number === number) ?? null
})

const day = (iso: string) => iso.slice(0, 10)

const boardHref = computed(() =>
  withBase(lang.value === 'en-US' ? '/en/community/' : '/community/'),
)
</script>

<style scoped>
.topic {
  padding: 88px 24px 120px;
  min-height: 70vh;
}

.container {
  max-width: 820px;
  margin: 0 auto;
}

.back {
  display: inline-block;
  margin-bottom: 28px;
  font-size: 0.85rem;
  color: var(--vp-c-text-3);
  text-decoration: none;
}

.back:hover {
  color: var(--gk-accent-blue);
}

.topic-head {
  padding-bottom: 24px;
  border-bottom: 1px solid var(--vp-c-border);
}

.topic-title {
  margin: 0 0 14px;
  font-size: clamp(1.6rem, 3vw, 2.15rem);
  font-weight: 750;
  line-height: 1.3;
  letter-spacing: -0.02em;
  border: 0;
  padding: 0;
}

.topic-meta {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 8px;
  font-size: 0.82rem;
  color: var(--vp-c-text-3);
}

.topic-meta a {
  color: var(--gk-accent-blue);
  text-decoration: none;
}

.avatar {
  width: 20px;
  height: 20px;
  border-radius: 50%;
}

.sep {
  opacity: 0.5;
}

.labels {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  margin-top: 12px;
}

.label-pill {
  font-size: 0.72rem;
  padding: 2px 9px;
  border-radius: 999px;
  border: 1px solid var(--gk-accent-blue-border);
  color: var(--gk-accent-blue);
}

.topic-body {
  padding: 28px 0 8px;
  line-height: 1.8;
  color: var(--vp-c-text-1);
}

.replies {
  margin-top: 48px;
  padding-top: 28px;
  border-top: 1px solid var(--vp-c-border);
}

.replies-head {
  display: flex;
  align-items: baseline;
  gap: 10px;
}

.replies-head h2 {
  margin: 0;
  font-size: 1.1rem;
  font-weight: 650;
  border: 0;
  padding: 0;
}

.replies-count {
  font-size: 0.8rem;
  padding: 1px 9px;
  border-radius: 999px;
  background: var(--gk-accent-blue-soft);
  color: var(--gk-accent-blue);
  font-variant-numeric: tabular-nums;
}

.replies-hint {
  margin: 10px 0 20px;
  font-size: 0.82rem;
  color: var(--vp-c-text-3);
}

.missing {
  padding: 56px 24px;
  text-align: center;
  border: 1px dashed var(--gk-card-border);
  border-radius: 10px;
}

.empty-title {
  margin: 0 0 8px;
  font-weight: 650;
}

.empty-hint {
  margin: 0;
  font-size: 0.86rem;
  color: var(--vp-c-text-3);
}
</style>

<style>
/* GitHub 渲染出来的正文是裸 HTML，不走 VitePress 的 markdown 样式表，
   这里给它补一套最小排版（scoped 会打不到 v-html 的子节点，所以不加 scoped）。 */
.topic-body.markdown-body > *:first-child { margin-top: 0; }
.topic-body.markdown-body p { margin: 0 0 1.1em; }
.topic-body.markdown-body h1,
.topic-body.markdown-body h2,
.topic-body.markdown-body h3,
.topic-body.markdown-body h4 {
  margin: 1.9em 0 0.7em;
  font-weight: 650;
  line-height: 1.35;
  letter-spacing: -0.01em;
}
.topic-body.markdown-body h1 { font-size: 1.5rem; }
.topic-body.markdown-body h2 { font-size: 1.28rem; }
.topic-body.markdown-body h3 { font-size: 1.1rem; }
.topic-body.markdown-body a { color: var(--gk-accent-blue); text-decoration: none; }
.topic-body.markdown-body a:hover { text-decoration: underline; }
.topic-body.markdown-body ul,
.topic-body.markdown-body ol { padding-left: 1.4em; margin: 0 0 1.1em; }
.topic-body.markdown-body li { margin: 0.35em 0; }
.topic-body.markdown-body img { max-width: 100%; border-radius: 8px; }
.topic-body.markdown-body blockquote {
  margin: 0 0 1.1em;
  padding: 2px 0 2px 16px;
  border-left: 2px solid var(--gk-accent-blue-border);
  color: var(--vp-c-text-2);
}
.topic-body.markdown-body code {
  font-size: 0.86em;
  padding: 2px 6px;
  border-radius: 4px;
  background: var(--vp-c-bg-soft);
  border: 1px solid var(--vp-c-border);
}
.topic-body.markdown-body pre {
  margin: 0 0 1.2em;
  padding: 16px 18px;
  overflow-x: auto;
  border-radius: 8px;
  background: var(--vp-c-bg-alt);
  border: 1px solid var(--vp-c-border);
}
.topic-body.markdown-body pre code {
  padding: 0;
  border: 0;
  background: none;
  font-size: 0.84rem;
  line-height: 1.7;
}
.topic-body.markdown-body table {
  width: 100%;
  border-collapse: collapse;
  margin: 0 0 1.2em;
  font-size: 0.88rem;
  display: block;
  overflow-x: auto;
}
.topic-body.markdown-body th,
.topic-body.markdown-body td {
  border: 1px solid var(--vp-c-border);
  padding: 8px 12px;
  text-align: left;
}
.topic-body.markdown-body hr {
  border: 0;
  border-top: 1px solid var(--vp-c-border);
  margin: 2em 0;
}
</style>
