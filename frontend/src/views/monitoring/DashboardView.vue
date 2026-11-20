<template>
  <div class="dashboard-view">
    <div class="config-selector page-card" style="margin-bottom: 16px">
      <span>装备运行实例：</span>
      <el-select v-model="selectedConfigId" placeholder="选择实例" style="width: 300px" @change="loadDashboard">
        <el-option
            v-for="c in configList"
            :key="c.id"
            :label="`${c.configCode} - ${c.equipmentName}`"
            :value="c.id"
        />
      </el-select>
    </div>

    <div class="dashboard-body">
      <el-card class="tree-card" shadow="never">
        <template #header><b>设备树</b></template>
        <DeviceTree :config-id="selectedConfigId" @select="handleNodeSelect"/>
      </el-card>

      <el-card class="content-card" shadow="never">
        <template #header>
          <b>实时监控 - {{ currentPointLabel || '请选择点位' }}</b>
        </template>
        <div v-if="!currentPointId" style="color: #94a3b8; padding: 40px; text-align: center">
          请在左侧设备树中选择一个点位开始监控
        </div>
        <template v-else>
          <div class="points-grid">
            <RealTimePoint :label="currentPointLabel" :point-id="currentPointId"/>
          </div>
          <div ref="chartRef" class="trend-chart"></div>
        </template>
      </el-card>
    </div>
  </div>
</template>

<script lang="ts" setup>
import {nextTick, onBeforeUnmount, onMounted, ref} from 'vue'
import * as echarts from 'echarts'
import DeviceTree from '@/components/DeviceTree.vue'
import RealTimePoint from '@/components/RealTimePoint.vue'
import {equipmentConfigApi} from '@/api/equipmentConfig'
import {wsService} from '@/comm/ws'
import {useWsStore} from '@/stores/ws'

const wsStore = useWsStore()

const configList = ref<any[]>([])
const selectedConfigId = ref<number | null>(null)
const currentPointId = ref<number | null>(null)
const currentPointLabel = ref('')
const chartRef = ref<HTMLElement | null>(null)
let chart: any = null
const history = ref<number[]>([])
const MAX_HISTORY = 100

let dataHandler: ((payload: any) => void) | null = null

async function loadConfigs() {
  try {
    const data = await equipmentConfigApi.list()
    const raw: any = data
    configList.value = raw?.items ?? raw?.list ?? (Array.isArray(raw) ? raw : [])
    if (configList.value.length > 0 && !selectedConfigId.value) {
      selectedConfigId.value = configList.value[0].id
      await loadDashboard()
    }
  } catch {
  }
}

async function loadDashboard() {
  currentPointId.value = null
  history.value = []
  chart?.dispose()
  chart = null
}

function subscribeDataTopic() {
  if (dataHandler) return
  dataHandler = (payload: any) => {
    if (payload?.type !== 'data') return
    if (Number(payload.pointId) !== currentPointId.value) return
    const value = Number(payload.value)
    if (!Number.isFinite(value)) return
    history.value.push(value)
    if (history.value.length > MAX_HISTORY) history.value.shift()
    updateChart()
  }
  wsService.subscribe('data', dataHandler)
}

function handleNodeSelect(node: any) {
  if (node?.isPoint && node.pointId) {
    currentPointId.value = node.pointId
    currentPointLabel.value = node.label
    wsStore.subscribePoint(node.pointId)
    subscribeDataTopic()
    history.value = []
    initChart()
  }
}

function initChart() {
  nextTick(() => {
    if (!chartRef.value) return
    if (chart) chart.dispose()
    chart = echarts.init(chartRef.value)
    chart.setOption({
      tooltip: {trigger: 'axis'},
      grid: {left: 40, right: 20, top: 20, bottom: 30},
      xAxis: {type: 'category', data: []},
      yAxis: {type: 'value'},
      series: [{type: 'line', data: [], smooth: true, showSymbol: false, lineStyle: {color: '#2563eb'}}],
    })
    window.addEventListener('resize', onResize)
  })
}

function updateChart() {
  if (!chart) return
  const labels = history.value.map((_, i) => `${i}`)
  chart.setOption({xAxis: {data: labels}, series: [{data: history.value}]})
}

function onResize() {
  chart?.resize()
}

onMounted(() => {
  wsStore.subscribeDevice()
  loadConfigs()
})
onBeforeUnmount(() => {
  if (dataHandler) {
    wsService.unsubscribe('data', dataHandler)
    dataHandler = null
  }
  chart?.dispose()
  window.removeEventListener('resize', onResize)
})
</script>

<style lang="scss" scoped>
.dashboard-body {
  display: flex;
  gap: 16px;
}

.tree-card {
  width: 280px;
  flex-shrink: 0;
}

.content-card {
  flex: 1;
}

.points-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(140px, 1fr));
  gap: 12px;
  margin-bottom: 16px;
}

.trend-chart {
  height: 320px;
}
</style>