<template>
  <div class="page-card">
    <div class="page-header">
      <h3>设备列表</h3>
    </div>

    <div class="toolbar">
      <el-input
          v-model="keyword"
          clearable
          placeholder="搜索设备编码 / 名称"
          style="width: 240px"
          @keyup.enter="applyFilter"
      />
      <el-button :icon="Search" type="primary" @click="applyFilter">搜索</el-button>
    </div>

    <el-table v-loading="loading" :data="filteredList" border stripe @row-click="goDetail">
      <el-table-column label="设备编码" prop="deviceKey" show-overflow-tooltip width="160"/>
      <el-table-column label="设备名称" prop="deviceName" min-width="160" show-overflow-tooltip/>
      <el-table-column label="协议" prop="protocolCode" show-overflow-tooltip width="120"/>
      <el-table-column label="状态" width="90">
        <template #default="{ row }">
          <el-tag :type="statusTagType(row.status)" effect="dark" size="small">
            {{ statusText(row.status) }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column label="正常点位" prop="pointsOk" width="100" align="center"/>
      <el-table-column label="故障点位" prop="pointsFail" width="100" align="center"/>
      <el-table-column label="最后错误" prop="lastError" min-width="200" show-overflow-tooltip/>
      <el-table-column label="快照时间" width="170">
        <template #default="{ row }">{{ formatTime(row.snapshotAt) }}</template>
      </el-table-column>
    </el-table>
  </div>
</template>

<script lang="ts" setup>
import {computed, onMounted, ref} from 'vue'
import {useRouter} from 'vue-router'
import {Search} from '@element-plus/icons-vue'
import {devicesApi} from '@/api/devices'
import {baseDevicesApi} from '@/api/baseDevices'
import {formatTime} from '@/utils/time'

const router = useRouter()
const keyword = ref('')
const list = ref<any[]>([])
const loading = ref(false)

const filteredList = computed(() => {
  const kw = keyword.value.trim().toLowerCase()
  if (!kw) return list.value
  return list.value.filter((d) =>
      (d.deviceKey || '').toLowerCase().includes(kw) ||
      (d.deviceName || '').toLowerCase().includes(kw),
  )
})

async function loadList() {
  loading.value = true
  try {
    const [runtimeRes, masterRes]: any = await Promise.all([
      devicesApi.list({size: 200}),
      baseDevicesApi.deviceList({size: 10000}),
    ])
    const runtime = runtimeRes?.items ?? runtimeRes?.list ?? []
    const masters: any[] = masterRes?.items ?? masterRes?.list ?? []
    const nameMap = new Map<string, string>()
    for (const m of masters) {
      if (m?.deviceCode) nameMap.set(m.deviceCode, m.deviceName || '')
    }
    list.value = runtime.map((d: any) => ({
      ...d,
      deviceName: d.deviceName || nameMap.get(d.deviceKey) || '',
    }))
  } finally {
    loading.value = false
  }
}

function applyFilter() {
  // computed 自动响应
}

function statusTagType(status: number) {
  return status === 1 ? 'success' : status === 2 ? 'danger' : 'info'
}

function statusText(status: number) {
  return status === 1 ? '在线' : status === 2 ? '故障' : '离线'
}

function goDetail(row: any) {
  if (!row?.deviceKey) return
  router.push(`/devices/${encodeURIComponent(row.deviceKey)}`)
}

onMounted(loadList)
</script>