<template>
  <div class="page-card trend-view">
    <div class="page-header">
      <h3>历史趋势查询</h3>
    </div>

    <div class="toolbar">
      <el-input-number v-model="form.pointId" :min="1" placeholder="点位 ID" style="width: 160px"/>
      <el-date-picker
          v-model="form.range"
          end-placeholder="结束时间"
          range-separator="至"
          start-placeholder="开始时间"
          style="width: 340px"
          type="datetimerange"
          value-format="YYYY-MM-DD HH:mm:ss"
      />
      <el-radio-group v-model="form.agg">
        <el-radio-button value="raw">原始</el-radio-button>
        <el-radio-button value="1min">1分钟</el-radio-button>
        <el-radio-button value="5min">5分钟</el-radio-button>
        <el-radio-button value="1h">1小时</el-radio-button>
        <el-radio-button value="1d">1天</el-radio-button>
      </el-radio-group>
      <el-button :loading="loading" type="primary" @click="handleQuery">查询</el-button>
    </div>

    <div ref="chartRef" class="trend-chart"></div>
  </div>
</template>

<script lang="ts" setup>
import {nextTick, onBeforeUnmount, reactive, ref} from 'vue'
import * as echarts from 'echarts'
import {dataApi} from '@/api/data'
import {ElMessage} from 'element-plus'
import {formatTime} from '@/utils/time'
import type {HistoryAgg, HistoryAggRow, HistoryRawRow} from '@/types/api'

const form = reactive<{ pointId: number | null; range: any[]; agg: HistoryAgg }>({
  pointId: null,
  range: [],
  agg: 'raw',
})
const loading = ref(false)
const chartRef = ref<HTMLElement | null>(null)
let chart: any = null

async function handleQuery() {
  if (!form.pointId || !form.range || form.range.length < 2) {
    ElMessage.warning('请填写点位 ID 和时间范围')
    return
  }
  loading.value = true
  try {
    const startSec = Math.floor(new Date(form.range[0]).getTime() / 1000)
    const endSec = Math.floor(new Date(form.range[1]).getTime() / 1000)
    const res: any = await dataApi.history({
      pointId: form.pointId,
      start: startSec,
      end: endSec,
      agg: form.agg,
    })
    const rows = Array.isArray(res?.rows) ? res.rows : Array.isArray(res) ? res : []
    renderChart(rows)
  } catch (e: any) {
    ElMessage.error(e.message || '查询失败')
  } finally {
    loading.value = false
  }
}

function renderChart(rows: (HistoryRawRow | HistoryAggRow)[]) {
  nextTick(() => {
    if (!chartRef.value) return
    if (!chart) {
      chart = echarts.init(chartRef.value)
      window.addEventListener('resize', onResize)
    }

    const times = rows.map((r) => formatTime(r.timestamp))
    const isAgg = form.agg !== 'raw'
    const avg = rows.map((r: any) => (isAgg ? r.avg : r.value))
    const max = isAgg ? rows.map((r: any) => r.max) : []
    const min = isAgg ? rows.map((r: any) => r.min) : []

    const series: any[] = [
      {name: isAgg ? '均值' : '原始值', type: 'line', data: avg, smooth: true, showSymbol: false},
    ]
    if (isAgg) {
      series.push(
          {name: '峰值', type: 'line', data: max, smooth: true, showSymbol: false, lineStyle: {color: '#ef4444'}},
          {name: '谷值', type: 'line', data: min, smooth: true, showSymbol: false, lineStyle: {color: '#22c55e'}},
      )
    }

    chart.setOption(
        {
          tooltip: {trigger: 'axis'},
          legend: {data: series.map((s) => s.name)},
          grid: {left: 50, right: 20, top: 40, bottom: 30},
          xAxis: {type: 'category', data: times},
          yAxis: {type: 'value'},
          series,
        },
        true,
    )
  })
}

function onResize() {
  chart?.resize()
}

onBeforeUnmount(() => {
  chart?.dispose()
  window.removeEventListener('resize', onResize)
})
</script>

<style lang="scss" scoped>
.trend-chart {
  height: 400px;
}
</style>