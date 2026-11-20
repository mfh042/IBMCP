<template>
  <div class="page-card">
    <div class="page-header">
      <h3>设备列表</h3>
    </div>

    <div class="toolbar">
      <el-input v-model="keyword" clearable placeholder="搜索设备编码/名称/装备" style="width: 240px"
                @keyup.enter="loadList"/>
      <el-button :icon="Search" type="primary" @click="loadList">搜索</el-button>
    </div>

    <el-table v-loading="loading" :data="list" border stripe @row-click="goDetail">
      <el-table-column fixed label="ID" prop="id" width="70"/>
      <el-table-column label="设备编码" prop="deviceCode" show-overflow-tooltip width="130"/>
      <el-table-column label="设备名称" min-width="150" prop="deviceName" show-overflow-tooltip/>
      <el-table-column label="装备编码" prop="equipmentCode" show-overflow-tooltip width="130"/>
      <el-table-column label="装备名称" min-width="150" prop="equipmentName" show-overflow-tooltip/>
      <el-table-column label="协议" prop="protocolCode" show-overflow-tooltip width="120"/>
      <el-table-column label="状态" width="90">
        <template #default="{ row }">
          <el-tag :type="row.status === 1 ? 'success' : row.status === 2 ? 'danger' : 'info'" size="small">
            {{ row.status === 1 ? '在线' : row.status === 2 ? '故障' : '离线' }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column label="连接参数" min-width="200" prop="deviceConnection" show-overflow-tooltip/>
      <el-table-column label="备注" min-width="140" prop="remark" show-overflow-tooltip/>
      <el-table-column label="创建时间" width="160">
        <template #default="{ row }">{{ formatTime(row.createdAt) }}</template>
      </el-table-column>
      <el-table-column label="更新时间" width="160">
        <template #default="{ row }">{{ formatTime(row.updatedAt) }}</template>
      </el-table-column>
    </el-table>
  </div>
</template>

<script lang="ts" setup>
import {onMounted, ref} from 'vue'
import {useRouter} from 'vue-router'
import {Search} from '@element-plus/icons-vue'
import {devicesApi} from '@/api/devices'
import {formatTime} from '@/utils/time'

const router = useRouter()
const keyword = ref('')
const list = ref<any[]>([])
const loading = ref(false)

async function loadList() {
  loading.value = true
  try {
    const data = await devicesApi.list({keyword: keyword.value || undefined})
    list.value = (data as any)?.list ?? (Array.isArray(data) ? data : [])
  } finally {
    loading.value = false
  }
}

function goDetail(row: any) {
  router.push(`/devices/${row.id}`)
}

onMounted(loadList)
</script>