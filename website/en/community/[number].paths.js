import { topicParams } from '../../.vitepress/topicRoutes.mjs'

export default {
  watch: ['../../.vitepress/data/discussions.json'],
  paths: () => topicParams(),
}
