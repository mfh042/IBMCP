<template>
  <div class="page-card">
    <div class="page-header"><h3>活动报警</h3></div>

    <div class="toolbar">
      <el-select v-model="filterState" placeholder="状态" style="width: 120px" @change="loadList">
        <el-option :value="0" label="全部"/>
        <el-option :value="1" label="活动中"/>
        <el-option :value="2" label="已确认"/>
        <el-option :value="3" label="已恢复"/>
      </el-select>
      <el-select v-model="filterLevel" clearable placeholder="级别" style="width: 120px" @change="loadList">
        <el-option :value="1" label="警告"/>
        <el-option :value="2" label="报警"/>
        <el-option :value="3" label="严重"/>
      </el-select>
      <el-button :icon="Refresh" type="primary" @click="loadList">刷新</el-button>
    </div>

    <el-table v-loading="loading" :data="list" stripe>
      <el-table-column label="级别" width="100">
        <template #default="{ row }">
          <el-tag :type="levelTagType(row.level)" effect="dark">{{ levelText(row.level) }}</el-tag>
        </template>
      </el-table-column>
      <el-table-column label="设备编码" prop="deviceCode" width="140"/>
      <el-table-column label="点位地址" prop="pointAddress" width="120"/>
      <el-table-column label="点位" prop="pointName" width="140"/>
      <el-table-column label="描述" min-width="200" prop="message"/>
      <el-table-column label="当前值" width="120">
        <template #default="{ row }">{{ row.value }}</template>
      </el-table-column>
      <el-table-column label="触发时间" width="180">
        <template #default="{ row }">{{ formatTime(row.triggeredAt) }}</template>
      </el-table-column>
      <el-table-column label="操作" width="180">
        <template #default="{ row }">
          <el-button :disabled="row.state >= 2" link size="small" @click="ackAlarm(row)">确认</el-button>
          <el-button :disabled="row.state >= 3" link size="small" @click="recoverAlarm(row)">恢复</el-button>
        </template>
      </el-table-column>
    </el-table>
  </div>
</template>

<script lang="ts" setup>
import {onMounted, ref} from 'vue'
import {Refresh} from '@element-plus/icons-vue'
import {ElMessage} from 'element-plus'
import {alarmsApi} from '@/api/alarms'
import {formatTime} from '@/utils/time'

const filterState = ref(1)
const filterLevel = ref<number | undefined>(undefined)
const list = ref<any[]>([])
const loading = ref(false)

async function loadList() {
  loading.value = true
  try {
    const params: any = {}
    if (filterState.value && filterState.value !== 0) params.state = filterState.value
    const data: any = await alarmsApi.list(params)
    const items = data?.items ?? data?.list ?? (Array.isArray(data) ? data : [])
    list.value = filterLevel.value ? items.filter((a: any) => a.level === filterLevel.value) : items
  } finally {
    loading.value = false
  }
}

async function ackAlarm(row: any) {
  try {
    await alarmsApi.ack(row.id)
    ElMessage.success('已确认')
    row.state = 2
    await loadList()
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

async function recoverAlarm(row: any) {
  try {
    await alarmsApi.recover(row.id)
    ElMessage.success('已恢复')
    row.state = 3
    await loadList()
  } catch (e: any) {
    ElMessage.error(e.message)
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