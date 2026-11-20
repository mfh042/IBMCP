<template>
  <div class="page-card">
    <div class="page-header"><h3>报警历史</h3></div>

    <div class="toolbar">
      <el-select v-model="filterState" style="width: 120px">
        <el-option :value="0" label="全部状态" />
        <el-option :value="2" label="已确认" />
        <el-option :value="3" label="已恢复" />
      </el-select>
      <el-button type="primary" @click="loadList">查询</el-button>
    </div>

    <el-table :data="list" v-loading="loading" stripe>
      <el-table-column label="级别" width="100">
        <template #default="{ row }">
          <el-tag :type="levelTagType(row.level)">{{ levelText(row.level) }}</el-tag>
        </template>
      </el-table-column>
      <el-table-column prop="deviceCode" label="设备编码" width="140" />
      <el-table-column prop="pointAddress" label="点位地址" width="120" />
      <el-table-column prop="pointName" label="点位" width="140" />
      <el-table-column prop="message" label="描述" min-width="200" />
      <el-table-column label="触发时间" width="180">
        <template #default="{ row }">{{ formatTime(row.triggeredAt) }}</template>
      </el-table-column>
      <el-table-column label="确认时间" width="180">
        <template #default="{ row }">{{ formatTime(row.acknowledgedAt) }}</template>
      </el-table-column>
      <el-table-column label="恢复时间" width="180">
        <template #default="{ row }">{{ formatTime(row.recoveredAt) }}</template>
      </el-table-column>
    </el-table>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { alarmsApi } from '@/api/alarms'
import { formatTime } from '@/utils/time'

const filterState = ref(0)
const list = ref<any[]>([])
const loading = ref(false)

async function loadList() {
  loading.value = true
  try {
    const params: any = {}
    if (filterState.value) params.state = filterState.value
    const data: any = await alarmsApi.list(params)
    list.value = data?.items ?? data?.list ?? (Array.isArray(data) ? data : [])
  } finally {
    loading.value = false
  }
}

function levelTagType(level: number) {
  return level === 1 ? 'info' : level === 2 ? 'warning' : 'danger'
}
function levelText(level: number) {
  return level === 1 ? '警告' : level === 2 ? '报警' : '严重'
}

onMounted(loadList)
</script>