// 动态路由 community/[number] 的路径来源：构建期抓下来的讨论快照。
// zh 与 en 两份 .paths.js 共用它，省得快照的定位逻辑写两遍。
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

// paths loader 会被 esbuild 打包后再执行，import.meta.url 未必还指向本文件，
// 所以先按 website 根目录找，再退回相对本文件。
function snapshotFile() {
  const candidates = [
    path.resolve(process.cwd(), '.vitepress/data/discussions.json'),
    path.resolve(process.cwd(), 'website/.vitepress/data/discussions.json'),
    path.resolve(path.dirname(fileURLToPath(import.meta.url)), 'data/discussions.json'),
  ]
  return candidates.find((file) => fs.existsSync(file))
}

export function topicParams() {
  const file = snapshotFile()
  if (!file) {
    console.warn('[discussions] 找不到快照，论坛主题页不会生成')
    return []
  }
  const { topics } = JSON.parse(fs.readFileSync(file, 'utf8'))
  return topics.map((topic) => ({ params: { number: String(topic.number) } }))
}
