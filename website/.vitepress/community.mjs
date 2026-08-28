// 社区（论坛）板块的唯一配置源：config.mts、抓取脚本、Vue 组件都从这里取值。
//
// 论坛的形态是「我在 GitHub Discussions 的 Announcements 分类里发主题，访客只能回帖」：
//   1. Announcements 是 GitHub 内建的公告型分类，只有仓库维护者能开新讨论；
//   2. giscus 用 mapping=number 绑定到已存在的讨论号，因此它永远不会替访客新建讨论。
// 两道锁缺一不可 —— 只靠 giscus 的话，访客仍能直接去 GitHub 开帖；只靠分类的话，
// 换成 pathname 映射时 giscus bot 又会代访客建讨论。
export const community = {
  owner: 'gameknife',
  repo: 'gkNextEngine',
  // giscus 需要 GraphQL 的 node id，不是仓库名。
  // 取法：gh api repos/gameknife/gkNextEngine --jq .node_id
  repoId: 'R_kgDOMDz7XA',
  category: 'Announcements',
  // 取法：gh api graphql -f query='{repository(owner:"gameknife",name:"gkNextEngine"){discussionCategories(first:20){nodes{id name}}}}'
  categoryId: 'DIC_kwDOMDz7XM4Ckp9Q',
}

export const repoFullName = `${community.owner}/${community.repo}`
export const discussionsUrl = `https://github.com/${repoFullName}/discussions`
export const discussionsCategoryUrl = `${discussionsUrl}/categories/${community.category.toLowerCase()}`

// Cloudflare Web Analytics 的 beacon token。站点级 token，不能和别的站点共用。
// 在 Cloudflare 控制台 Web Analytics 里给 gameknife.github.io/gkNextEngine 建站点后，
// 把 token 填在这里（它本来就会明文出现在页面 HTML 里，提交没有泄密问题），
// 或者在 CI 里设 CF_BEACON_TOKEN 环境变量覆盖。留空则整段脚本不注入。
export const cfBeaconToken = ''
