// 把 GitHub Discussions（Announcements 分类）抓成静态快照，供论坛板块在构建时消费。
//
// 为什么要抓：Discussions 只有 GraphQL API，且必须带 token，浏览器端拿不到，
// 所以列表只能在构建期落成 JSON。快照是提交进仓库的 —— 没有 token（本地 clone、
// fork 的 CI）时脚本原样退出，站点仍能用上一次的快照构建，不会白页。
//
// 用法：
//   node scripts/fetch-discussions.mjs          # 用 GITHUB_TOKEN / GH_TOKEN，退化到 gh CLI
//   node scripts/fetch-discussions.mjs --check  # 只比对，快照过期时非零退出（给 CI 用）
import { execFileSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

import { community } from '../.vitepress/community.mjs'

const here = path.dirname(fileURLToPath(import.meta.url))
const outFile = path.resolve(here, '../.vitepress/data/discussions.json')
const checkOnly = process.argv.includes('--check')

function resolveToken() {
  const fromEnv = process.env.GITHUB_TOKEN || process.env.GH_TOKEN
  if (fromEnv) return fromEnv
  try {
    // 本地开发时通常已经 gh auth login 过，借它的 token 省得再配环境变量。
    return execFileSync('gh', ['auth', 'token'], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] }).trim()
  } catch {
    return ''
  }
}

const query = `
query($owner: String!, $repo: String!, $categoryId: ID!, $cursor: String) {
  repository(owner: $owner, name: $repo) {
    discussions(first: 50, after: $cursor, categoryId: $categoryId, orderBy: { field: CREATED_AT, direction: DESC }) {
      pageInfo { hasNextPage endCursor }
      nodes {
        number
        title
        url
        bodyHTML
        createdAt
        updatedAt
        upvoteCount
        locked
        author { login url avatarUrl }
        comments { totalCount }
        labels(first: 8) { nodes { name color } }
      }
    }
  }
}`

async function fetchAll(token) {
  const topics = []
  let cursor = null
  for (;;) {
    const res = await fetch('https://api.github.com/graphql', {
      method: 'POST',
      headers: {
        Authorization: `Bearer ${token}`,
        'Content-Type': 'application/json',
        'User-Agent': 'gknext-website-build',
      },
      body: JSON.stringify({
        query,
        variables: { owner: community.owner, repo: community.repo, categoryId: community.categoryId, cursor },
      }),
    })
    if (!res.ok) throw new Error(`GitHub GraphQL ${res.status} ${res.statusText}`)
    const json = await res.json()
    if (json.errors) throw new Error(json.errors.map((e) => e.message).join('; '))

    const page = json.data.repository.discussions
    for (const node of page.nodes) topics.push(normalize(node))
    if (!page.pageInfo.hasNextPage) break
    cursor = page.pageInfo.endCursor
  }
  return topics
}

// bodyText 是 GitHub 去掉 markdown 记号后的纯文本，拿来做卡片摘要正好，
// 不用自己解析 markdown。
const entities = { amp: '&', lt: '<', gt: '>', quot: '"', '#39': "'", nbsp: ' ' }

// 从 bodyHTML 里取正文段落，而不是用 bodyText。bodyText 把标题和段落全用单换行串成一片，
// 拍平后卡片上读起来像乱码（"…条数多是好事。怎么提 一条回帖…"）；
// bodyHTML 里标题是 <h2>，只挑 <p> 就自动跳过了。
function excerpt(bodyHTML, limit = 180) {
  const paragraphs = [...(bodyHTML || '').matchAll(/<p\b[^>]*>([\s\S]*?)<\/p>/gi)]
    .map((match) =>
      match[1]
        .replace(/<[^>]+>/g, '')
        .replace(/&(#39|amp|lt|gt|quot|nbsp);/g, (_, name) => entities[name])
        .replace(/\s+/g, ' ')
        .trim(),
    )
    .filter(Boolean)

  // 首段太短时续上下一段，免得卡片只剩半行。
  let text = ''
  for (const para of paragraphs) {
    text = text ? `${text} ${para}` : para
    if (text.length >= limit * 0.5) break
  }
  return text.length <= limit ? text : `${text.slice(0, limit - 1)}…`
}

function normalize(node) {
  return {
    number: node.number,
    title: node.title,
    url: node.url,
    // bodyHTML 已由 GitHub 渲染并做过 sanitize，主题页直接 v-html 呈现。
    bodyHTML: node.bodyHTML,
    excerpt: excerpt(node.bodyHTML),
    createdAt: node.createdAt,
    updatedAt: node.updatedAt,
    upvotes: node.upvoteCount,
    locked: node.locked,
    replies: node.comments.totalCount,
    author: node.author
      ? { login: node.author.login, url: node.author.url, avatar: node.author.avatarUrl }
      : null,
    labels: node.labels.nodes.map((l) => ({ name: l.name, color: l.color })),
  }
}

function readSnapshot() {
  try {
    return JSON.parse(fs.readFileSync(outFile, 'utf8'))
  } catch {
    return null
  }
}

// generatedAt 每次都会变，比对时忽略它，否则 --check 永远失败。
const topicsOf = (snap) => JSON.stringify(snap?.topics ?? null)

async function main() {
  const token = resolveToken()
  if (!token) {
    const existing = readSnapshot()
    if (checkOnly) {
      console.error('[discussions] --check 需要 GITHUB_TOKEN / GH_TOKEN 或已登录的 gh CLI')
      process.exit(2)
    }
    console.warn(
      `[discussions] 没有可用 token，跳过抓取，沿用${existing ? `已有快照（${existing.topics.length} 个主题）` : '空列表'}`,
    )
    if (!existing) writeSnapshot([])
    return
  }

  let topics
  try {
    topics = await fetchAll(token)
  } catch (err) {
    // 抓取失败不该让整个站点构建挂掉 —— 快照还在，论坛板块顶多少几个新主题。
    if (checkOnly) {
      console.error(`[discussions] 抓取失败：${err.message}`)
      process.exit(2)
    }
    console.warn(`[discussions] 抓取失败，沿用已有快照：${err.message}`)
    if (!readSnapshot()) writeSnapshot([])
    return
  }

  if (checkOnly) {
    const existing = readSnapshot()
    if (topicsOf(existing) !== topicsOf({ topics })) {
      console.error('[discussions] 快照已过期，请运行 npm run fetch:discussions 并提交结果')
      process.exit(1)
    }
    console.log(`[discussions] 快照是最新的（${topics.length} 个主题）`)
    return
  }

  const existing = readSnapshot()
  if (existing && topicsOf(existing) === topicsOf({ topics })) {
    // 内容没变就别动文件 —— 否则每次构建都会因为 generatedAt 冒出一个假 diff。
    console.log(`[discussions] 快照无变化（${topics.length} 个主题）`)
    return
  }

  writeSnapshot(topics)
  console.log(`[discussions] 已写入 ${topics.length} 个主题 → ${path.relative(process.cwd(), outFile)}`)
}

function writeSnapshot(topics) {
  const payload = {
    repo: `${community.owner}/${community.repo}`,
    category: community.category,
    generatedAt: new Date().toISOString(),
    topics,
  }
  fs.mkdirSync(path.dirname(outFile), { recursive: true })
  fs.writeFileSync(outFile, `${JSON.stringify(payload, null, 2)}\n`, 'utf8')
}

main().catch((err) => {
  console.error(`[discussions] ${err.stack || err.message}`)
  process.exit(1)
})
