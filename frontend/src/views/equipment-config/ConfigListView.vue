<template>
  <div class="page-card">
    <div class="page-header">
      <h3>装备运行实例</h3>
      <el-button type="primary" @click="$router.push('/equipment-config/wizard')">新建配置</el-button>
    </div>

    <el-table v-loading="loading" :data="list" stripe>
      <el-table-column label="ID" prop="id" width="70"/>
      <el-table-column label="配置编码" prop="configCode" width="160"/>
      <el-table-column label="装备编码" prop="equipmentCode" width="140"/>
      <el-table-column label="装备名称" min-width="160" prop="equipmentName"/>
      <el-table-column label="配置状态" width="110">
        <template #default="{ row }">
          <el-tag :type="row.configStatus === 1 ? 'success' : 'info'" size="small">
            {{ row.configStatus === 1 ? '已下发' : '草稿' }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column label="运行状态" width="110">
        <template #default="{ row }">
          <el-tag :type="row.runStatus === 1 ? 'success' : row.runStatus === 2 ? 'danger' : 'info'" size="small">
            {{ row.runStatus === 1 ? '运行中' : row.runStatus === 2 ? '异常' : '已停止' }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column label="位置" prop="location" width="160"/>
      <el-table-column label="更新时间" prop="updatedAt" width="180"/>
      <el-table-column fixed="right" label="操作" width="160">
        <template #default="{ row }">
          <el-button link size="small" type="primary" @click.stop="goDetail(row)">详情</el-button>
          <el-button link size="small" type="primary" @click.stop="goEdit(row)">编辑</el-button>
          <el-button link size="small" type="danger" @click.stop="handleDelete(row)">删除</el-button>
        </template>
      </el-table-column>
    </el-table>
  </div>
</template>

<script lang="ts" setup>
import {onMounted, ref} from 'vue'
import {useRouter} from 'vue-router'
import {ElMessage, ElMessageBox} from 'element-plus'
import {equipmentConfigApi} from '@/api/equipmentConfig'

const router = useRouter()
const list = ref<any[]>([])
const loading = ref(false)

async function loadList() {
  loading.value = true
  try {
    const data = await equipmentConfigApi.list()
    list.value = (data as any)?.items ?? (Array.isArray(data) ? data : [])
  } finally {
    loading.value = false
  }
}

function goDetail(row: any) {
  router.push(`/equipment-config/${row.id}`)
}

function goEdit(row: any) {
  router.push(`/equipment-config/${row.id}/edit`)
}

async function handleDelete(row: any) {
  try {
    await ElMessageBox.confirm(`确定要删除配置 "${row.configCode}" 吗？此操作将级联删除关联的路线和功能。`, '确认删除', {
      type: 'warning',
    })
    await equipmentConfigApi.remove(row.id)
    ElMessage.success('删除成功')
    loadList()
  } catch {
  }
}

onMounted(loadList)
</script>