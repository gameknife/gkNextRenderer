import { topicParams } from '../.vitepress/topicRoutes.mjs'

export default {
  // paths loader 只会在自身变动时重跑，快照是它读的外部文件 ——
  // 不 watch 的话，dev 期间抓到新主题必须重启 server 才会多出路由。
  watch: ['../.vitepress/data/discussions.json'],
  paths: () => topicParams(),
}
