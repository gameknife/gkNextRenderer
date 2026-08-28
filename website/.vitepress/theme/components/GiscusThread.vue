<template>
  <div ref="host" class="giscus-host" />
</template>

<script setup lang="ts">
import { onMounted, ref, watch } from 'vue'
import { useData } from 'vitepress'
import { community, repoFullName } from '../../community.mjs'

const props = defineProps<{ term: string | number }>()

const { isDark, lang } = useData()
const host = ref<HTMLElement | null>(null)

// giscus 只在挂载时读一次 data-theme，之后靠 postMessage 改；
// 站点用 html.dark 切换，这里跟着 isDark 走。
const giscusTheme = () => (isDark.value ? 'transparent_dark' : 'light')

onMounted(() => {
  const script = document.createElement('script')
  script.src = 'https://giscus.app/client.js'
  script.async = true
  script.crossOrigin = 'anonymous'
  script.setAttribute('data-repo', repoFullName)
  script.setAttribute('data-repo-id', community.repoId)
  script.setAttribute('data-category', community.category)
  script.setAttribute('data-category-id', community.categoryId)
  // mapping=number 绑定到已存在的讨论号。相比常见的 pathname 映射，
  // 它不会在访客首次评论时替对方新建讨论 —— 访客只能回帖，开主题的只有维护者。
  script.setAttribute('data-mapping', 'number')
  script.setAttribute('data-term', String(props.term))
  script.setAttribute('data-reactions-enabled', '1')
  script.setAttribute('data-emit-metadata', '0')
  script.setAttribute('data-input-position', 'top')
  script.setAttribute('data-theme', giscusTheme())
  script.setAttribute('data-lang', lang.value === 'en-US' ? 'en' : 'zh-CN')
  script.setAttribute('data-loading', 'lazy')
  host.value?.appendChild(script)
})

watch(isDark, () => {
  const frame = document.querySelector<HTMLIFrameElement>('iframe.giscus-frame')
  if (!frame?.contentWindow) return
  frame.contentWindow.postMessage(
    { giscus: { setConfig: { theme: giscusTheme() } } },
    'https://giscus.app',
  )
})
</script>

<style scoped>
.giscus-host {
  margin-top: 8px;
  min-height: 200px;
}
</style>
