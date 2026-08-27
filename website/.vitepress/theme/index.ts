import DefaultTheme from 'vitepress/theme'
import type { Theme } from 'vitepress'
import HomePage from './components/HomePage.vue'
import HomeHero from './components/HomeHero.vue'
import FeaturePillars from './components/FeaturePillars.vue'
import ShowcaseGrid from './components/ShowcaseGrid.vue'
import BenchmarkData from './components/BenchmarkData.vue'
import QuickStartCli from './components/QuickStartCli.vue'
import EcosystemTech from './components/EcosystemTech.vue'
import './custom.css'

export default {
  extends: DefaultTheme,
  enhanceApp({ app }) {
    app.component('HomePage', HomePage)
    app.component('HomeHero', HomeHero)
    app.component('FeaturePillars', FeaturePillars)
    app.component('ShowcaseGrid', ShowcaseGrid)
    app.component('BenchmarkData', BenchmarkData)
    app.component('QuickStartCli', QuickStartCli)
    app.component('EcosystemTech', EcosystemTech)
  }
} satisfies Theme
