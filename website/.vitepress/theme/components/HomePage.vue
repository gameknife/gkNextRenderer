<template>
  <div class="gknext-home">
    <HomeHero />
    <FeaturePillars />
    <ShowcaseGrid />
    <BenchmarkData />
    <QuickStartCli />
    <EcosystemTech />
  </div>
</template>

<script setup lang="ts">
import HomeHero from './HomeHero.vue'
import FeaturePillars from './FeaturePillars.vue'
import ShowcaseGrid from './ShowcaseGrid.vue'
import BenchmarkData from './BenchmarkData.vue'
import QuickStartCli from './QuickStartCli.vue'
import EcosystemTech from './EcosystemTech.vue'
</script>

<style scoped>
.gknext-home {
  width: 100%;
  min-height: 100vh;
}
</style>
