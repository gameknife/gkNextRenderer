<template>
  <section id="benchmarks" class="benchmark-section">
    <div class="container">
      <div class="section-header">
        <span class="gk-badge gk-badge-green">{{ t.badge }}</span>
        <h2 class="section-title">
          <span class="gk-gradient-text">{{ t.title }}</span>
        </h2>
        <p class="section-desc">
          {{ t.desc }}
        </p>
      </div>

      <!-- 核心指标摘要卡片 -->
      <div class="metrics-grid">
        <div v-for="(metric, idx) in t.metrics" :key="idx" class="metric-card gk-card">
          <div class="metric-val" :class="metric.colorClass">{{ metric.val }} <span class="metric-unit">{{ metric.unit }}</span></div>
          <div class="metric-label">{{ metric.label }}</div>
          <div class="metric-sub">{{ metric.sub }}</div>
        </div>
      </div>

      <!-- 详细 Benchmark 数据表格卡片 -->
      <div class="table-card gk-card">
        <div class="table-header">
          <div class="table-title">{{ t.tableTitle }}</div>
          <div class="table-env">{{ t.tableEnv }}</div>
        </div>

        <div class="table-responsive">
          <table class="benchmark-table">
            <thead>
              <tr>
                <th v-for="(header, hIdx) in t.tableHeaders" :key="hIdx">{{ header }}</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="(row, idx) in benchmarkRows" :key="idx" :class="{ 'highlight-row': row.highlight }">
                <td class="scene-col font-bold">{{ row.scene }}</td>
                <td>
                  <span class="pipeline-badge" :class="row.isPt ? 'pt-badge' : 'raster-badge'">
                    {{ row.pipeline }}
                  </span>
                </td>
                <td class="gk-mono text-accent-blue">{{ row.frameTime }}</td>
                <td class="gk-mono font-bold">{{ row.fps }}</td>
                <td class="gk-mono text-gray">{{ row.vram }}</td>
                <td class="gk-mono text-gray">{{ row.draws }}</td>
                <td class="gk-mono text-gray">{{ row.triangles }}</td>
              </tr>
            </tbody>
          </table>
        </div>

        <div class="table-footer">
          <span>{{ t.reproduceLabel }}</span>
          <code>./gnb.sh run gkNextMotionBenchmark --benchmark-config assets/configs/motion_benchmark.example.json</code>
        </div>
      </div>
    </div>
  </section>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useData } from 'vitepress'
import { zhCN, enUS } from '../i18n'

const { lang } = useData()
const isEn = computed(() => lang.value === 'en-US')
const t = computed(() => (isEn.value ? enUS.benchmark : zhCN.benchmark))

const benchmarkRows = [
  {
    scene: 'MaterialShowcase',
    pipeline: 'PathTracing',
    isPt: true,
    frameTime: '2.342 ms',
    fps: '427',
    vram: '978 MiB',
    draws: '15 / 15',
    triangles: '13,862 / 13,862',
    highlight: true
  },
  {
    scene: 'MaterialShowcase',
    pipeline: 'SoftwareModernNoAmbient',
    isPt: false,
    frameTime: '0.619 ms',
    fps: '1,614',
    vram: '925 MiB',
    draws: '15 / 15',
    triangles: '13,542 / 13,542',
    highlight: false
  },
  {
    scene: 'LightingShowcase',
    pipeline: 'PathTracing',
    isPt: true,
    frameTime: '2.836 ms',
    fps: '353',
    vram: '978 MiB',
    draws: '9 / 9',
    triangles: '4,953 / 4,953',
    highlight: false
  },
  {
    scene: 'LightingShowcase',
    pipeline: 'SoftwareModernNoAmbient',
    isPt: false,
    frameTime: '0.707 ms',
    fps: '1,414',
    vram: '925 MiB',
    draws: '5 / 5',
    triangles: '2,881 / 2,881',
    highlight: false
  },
  {
    scene: 'KilometerWorld',
    pipeline: 'PathTracing',
    isPt: true,
    frameTime: '1.651 ms',
    fps: '606',
    vram: '925 MiB',
    draws: '401 / 1,780',
    triangles: '4,789 / 21,362',
    highlight: true
  },
  {
    scene: 'MassiveAsteroidBelt',
    pipeline: 'PathTracing',
    isPt: true,
    frameTime: '2.089 ms',
    fps: '479',
    vram: '1,192 MiB',
    draws: '34,265 / 67,786',
    triangles: '2.73M / 5.42M',
    highlight: true
  }
]
</script>

<style scoped>
.benchmark-section {
  padding: 100px 24px;
  position: relative;
}

.container {
  max-width: 1120px;
  margin: 0 auto;
}

.section-header {
  text-align: center;
  margin-bottom: 56px;
  display: flex;
  flex-direction: column;
  align-items: center;
}

.section-header .gk-badge {
  margin-bottom: 20px;
}

.section-title {
  font-size: clamp(2rem, 3.8vw, 2.85rem);
  font-weight: 800;
  line-height: 1.25;
  margin: 0 0 22px;
  letter-spacing: -0.02em;
}

.section-desc {
  font-size: 1.1rem;
  color: var(--vp-c-text-2);
  max-width: 720px;
  margin: 0 auto;
  line-height: 1.8;
  letter-spacing: 0.01em;
}

.metrics-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 16px;
  margin-bottom: 32px;
}

.metric-card {
  padding: 24px 20px;
  text-align: center;
}

.metric-val {
  font-size: 2.2rem;
  font-weight: 800;
  font-family: var(--gk-mono);
  line-height: 1;
  margin-bottom: 8px;
  color: #ffffff;
}

.metric-unit {
  font-size: 1rem;
  font-weight: 500;
  color: var(--vp-c-text-3);
}

.metric-card:hover .metric-val {
  color: #ffffff;
  text-shadow: 0 0 15px rgba(255, 255, 255, 0.4);
}

.metric-label {
  font-size: 0.95rem;
  font-weight: 600;
  color: var(--vp-c-text-1);
  margin-bottom: 4px;
}

.metric-sub {
  font-size: 0.78rem;
  color: var(--vp-c-text-3);
  line-height: 1.3;
}

.table-card {
  overflow: hidden;
}

.table-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 16px 20px;
  background: var(--vp-c-bg-soft);
  border-bottom: 1px solid var(--vp-c-border);
}

.table-title {
  font-weight: 600;
  font-size: 0.95rem;
  color: #ffffff;
}

.table-env {
  font-size: 0.8rem;
  color: var(--vp-c-text-3);
  font-family: var(--gk-mono);
}

.table-responsive {
  overflow-x: auto;
}

.benchmark-table {
  width: 100%;
  border-collapse: collapse;
  text-align: left;
  font-size: 0.88rem;
}

.benchmark-table th {
  padding: 12px 16px;
  background: var(--vp-c-bg-alt);
  color: var(--vp-c-text-3);
  font-weight: 600;
  border-bottom: 1px solid var(--vp-c-border);
}

.benchmark-table td {
  padding: 12px 16px;
  border-bottom: 1px solid var(--vp-c-divider);
}

.highlight-row {
  background: rgba(255, 255, 255, 0.02);
}

.pipeline-badge {
  font-size: 0.75rem;
  padding: 2px 8px;
  border-radius: 4px;
  font-family: var(--gk-mono);
  background: var(--vp-c-bg-alt);
  color: var(--vp-c-text-1);
  border: 1px solid var(--vp-c-border);
}

.pipeline-badge:hover {
  color: #ffffff;
  border-color: rgba(255, 255, 255, 0.3);
}

.font-bold { font-weight: 700; color: var(--vp-c-text-1); }
.text-gray { color: var(--vp-c-text-2); }
.text-accent-blue { color: var(--gk-accent-blue); }

.table-footer {
  padding: 12px 20px;
  background: var(--vp-c-bg-soft);
  font-size: 0.8rem;
  color: var(--vp-c-text-3);
  display: flex;
  align-items: center;
  gap: 8px;
  border-top: 1px solid var(--vp-c-border);
}

.table-footer code {
  font-family: var(--gk-mono);
  color: var(--gk-accent-blue);
  background: var(--vp-c-bg-alt);
  border: 1px solid var(--vp-c-border);
  padding: 2px 6px;
  border-radius: 4px;
}

@media (max-width: 900px) {
  .metrics-grid {
    grid-template-columns: repeat(2, 1fr);
  }
  .table-header {
    flex-direction: column;
    align-items: flex-start;
    gap: 4px;
  }
}

@media (max-width: 500px) {
  .metrics-grid {
    grid-template-columns: 1fr;
  }
}
</style>
