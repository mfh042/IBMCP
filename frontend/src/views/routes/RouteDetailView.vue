<template>
  <div class="page-card">
    <div class="page-header">
      <h3>路线详情：{{ data?.routeName || routeCode }}</h3>
      <el-button @click="$router.back()">返回</el-button>
      <el-button type="primary" @click="openStepDialog()">添加工步</el-button>
    </div>

    <div v-if="!data" v-loading="loading" style="height: 200px"/>
    <template v-else>
      <el-descriptions :column="3" border>
        <el-descriptions-item label="路线编码">{{ data.routeCode }}</el-descriptions-item>
        <el-descriptions-item label="路线名称">{{ data.routeName }}</el-descriptions-item>
        <el-descriptions-item label="资源类型">{{ data.resourceType }}</el-descriptions-item>
        <el-descriptions-item label="版本">{{ data.version }}</el-descriptions-item>
        <el-descriptions-item label="状态">
          <el-tag :type="statusType(data.status)">{{ statusLabel(data.status) }}</el-tag>
        </el-descriptions-item>
        <el-descriptions-item label="工步数量">{{ (data.steps || []).length }}</el-descriptions-item>
        <el-descriptions-item :span="3" label="资源URL">{{ data.resourceUrl || '-' }}</el-descriptions-item>
        <el-descriptions-item :span="3" label="缩略图URL">{{ data.thumbnailUrl || '-' }}</el-descriptions-item>
        <el-descriptions-item :span="3" label="描述">{{ data.description || '-' }}</el-descriptions-item>
      </el-descriptions>

      <el-divider content-position="left">工步列表</el-divider>
      <el-alert
          :closable="false"
          show-icon
          style="margin-bottom: 12px"
          title="工步一旦被「运行配置」绑定，需先解除绑定才能删除"
          type="info"
      />
      <el-table :data="data.steps || []" border stripe>
        <el-table-column label="顺序" prop="stepSeq" width="80"/>
        <el-table-column label="工步编码" prop="stepCode" width="140"/>
        <el-table-column label="工步名称" min-width="160" prop="stepName"/>
        <el-table-column label="关联设备" prop="deviceCode" width="160"/>
        <el-table-column label="时长(秒)" prop="durationTime" width="100"/>
        <el-table-column label="工作中心" prop="workCenter" width="120"/>
        <el-table-column label="关键工序" width="100">
          <template #default="{ row }">
            <el-tag :type="row.isCritical ? 'danger' : 'info'">{{ row.isCritical ? '是' : '否' }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="状态" width="80">
          <template #default="{ row }">
            <el-tag :type="row.status === 1 ? 'success' : 'info'">{{ row.status === 1 ? '启用' : '停用' }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="参数" min-width="180">
          <template #default="{ row }">
            <pre class="json-pre">{{ formatParams(row.stepParams) }}</pre>
          </template>
        </el-table-column>
        <el-table-column label="操作" width="140">
          <template #default="{ row }">
            <el-button link size="small" @click="openStepDialog(row)">编辑</el-button>
            <el-button link size="small" type="danger" @click="removeStep(row)">删除</el-button>
          </template>
        </el-table-column>
      </el-table>
    </template>

    <el-dialog v-model="stepDialogVisible" :title="editingStep ? '编辑工步' : '添加工步'" width="640px">
      <el-form :model="stepForm" label-width="110px">
        <el-form-item label="工步编码">
          <el-input v-model="stepForm.stepCode" :disabled="!!editingStep" placeholder="如：STEP-010"/>
        </el-form-item>
        <el-form-item label="工步名称">
          <el-input v-model="stepForm.stepName" placeholder="如：上料"/>
        </el-form-item>
        <el-form-item label="顺序">
          <el-input-number v-model="stepForm.stepSeq" :min="1" :step="10" style="width: 100%"/>
        </el-form-item>
        <el-form-item label="关联设备" required>
          <el-select v-model="stepForm.deviceCode" filterable placeholder="请选择设备" style="width: 100%">
            <el-option v-for="d in devices" :key="d.deviceCode" :label="`${d.deviceCode} - ${d.deviceName}`"
                       :value="d.deviceCode"/>
          </el-select>
        </el-form-item>
        <el-form-item label="时长(秒)">
          <el-input-number v-model="stepForm.durationTime" :min="0" style="width: 100%"/>
        </el-form-item>
        <el-form-item label="工作中心">
          <el-input v-model="stepForm.workCenter" placeholder="如：WC-01"/>
        </el-form-item>
        <el-form-item label="前序工步">
          <el-input v-model="stepForm.prevStepCode" placeholder="前序工步编码（可选）"/>
        </el-form-item>
        <el-form-item label="关键工序">
          <el-switch v-model="stepForm.isCritical" :active-value="1" :inactive-value="0"/>
        </el-form-item>
        <el-form-item label="状态">
          <el-radio-group v-model="stepForm.status">
            <el-radio :value="1">启用</el-radio>
            <el-radio :value="0">停用</el-radio>
          </el-radio-group>
        </el-form-item>
        <el-form-item label="工步参数">
          <el-input v-model="stepForm.stepParams" :rows="4" placeholder='JSON 格式，如：{"speed":3000}' type="textarea"/>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="stepDialogVisible = false">取消</el-button>
        <el-button type="primary" @click="submitStep">确定</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script lang="ts" setup>
import {computed, onMounted, ref} from 'vue'
import {useRoute, useRouter} from 'vue-router'
import {ElMessage, ElMessageBox} from 'element-plus'
import {routesApi} from '@/api/routes'
import {baseDevicesApi} from '@/api/baseDevices'

const route = useRoute()
const router = useRouter()
const routeCode = computed(() => String(route.params.code))
const data = ref<any>(null)
const loading = ref(false)
const stepDialogVisible = ref(false)
const editingStep = ref<any>(null)
const devices = ref<any[]>([])

const stepForm = ref({
  id: 0,
  routeCode: '',
  stepCode: '',
  stepName: '',
  stepSeq: 10,
  deviceCode: '',
  durationTime: 0,
  workCenter: '',
  stepParams: '',
  prevStepCode: '',
  isCritical: 0,
  status: 1,
})

function statusLabel(status: number): string {
  const map: Record<number, string> = {0: '草稿', 1: '已启用', 2: '停用'}
  return map[status] ?? '未知'
}

function statusType(status: number): 'info' | 'success' | 'danger' {
  const map: Record<number, 'info' | 'success' | 'danger'> = {0: 'info', 1: 'success', 2: 'danger'}
  return map[status] ?? 'info'
}

async function load() {
  loading.value = true
  try {
    const routeInfo: any = await routesApi.getByCode(routeCode.value)
    if (routeInfo && routeInfo.id) {
      data.value = await routesApi.get(routeInfo.id)
    } else {
      data.value = routeInfo
    }
  } catch (e: any) {
    data.value = null
    ElMessage.error(e?.message || '加载失败')
  } finally {
    loading.value = false
  }
}

async function loadDevices() {
  try {
    const data: any = await baseDevicesApi.deviceList({size: 9999})
    devices.value = data?.items ?? (Array.isArray(data) ? data : [])
  } catch {
  }
}

function formatParams(p: any) {
  if (!p) return '-'
  if (typeof p === 'string') {
    try {
      return JSON.stringify(JSON.parse(p), null, 2)
    } catch {
      return p
    }
  }
  return JSON.stringify(p, null, 2)
}

function openStepDialog(row?: any) {
  editingStep.value = row || null
  if (row) {
    stepForm.value = {
      id: row.id,
      routeCode: data.value?.routeCode || '',
      stepCode: row.stepCode,
      stepName: row.stepName,
      stepSeq: row.stepSeq,
      deviceCode: row.deviceCode || '',
      durationTime: row.durationTime || 0,
      workCenter: row.workCenter || '',
      stepParams: typeof row.stepParams === 'string' ? row.stepParams : JSON.stringify(row.stepParams || {}, null, 2),
      prevStepCode: row.prevStepCode || '',
      isCritical: row.isCritical || 0,
      status: row.status ?? 1,
    }
  } else {
    stepForm.value = {
      id: 0,
      routeCode: data.value?.routeCode || '',
      stepCode: '',
      stepName: '',
      stepSeq: (data.value?.steps?.length || 0) * 10 + 10,
      deviceCode: '',
      durationTime: 0,
      workCenter: '',
      stepParams: '',
      prevStepCode: '',
      isCritical: 0,
      status: 1,
    }
  }
  stepDialogVisible.value = true
}

async function submitStep() {
  if (!stepForm.value.deviceCode) {
    ElMessage.error('请选择关联设备')
    return
  }
  try {
    let parsedParams: any = ''
    if (stepForm.value.stepParams && stepForm.value.stepParams.trim()) {
      try {
        parsedParams = JSON.parse(stepForm.value.stepParams)
      } catch {
        ElMessage.error('工步参数格式错误，请输入有效的 JSON')
        return
      }
    }
    const submitData = {
      routeCode: stepForm.value.routeCode,
      stepCode: stepForm.value.stepCode,
      stepName: stepForm.value.stepName,
      stepSeq: Number(stepForm.value.stepSeq),
      deviceCode: stepForm.value.deviceCode,
      durationTime: Number(stepForm.value.durationTime) || 0,
      workCenter: stepForm.value.workCenter || '',
      stepParams: parsedParams,
      prevStepCode: stepForm.value.prevStepCode || '',
      isCritical: stepForm.value.isCritical ? 1 : 0,
      status: stepForm.value.status ?? 1,
    }
    console.log('提交工步数据:', submitData)
    if (editingStep.value) {
      await routesApi.stepUpdate(editingStep.value.id, submitData)
    } else {
      await routesApi.stepCreate(submitData)
    }
    ElMessage.success('保存成功')
    stepDialogVisible.value = false
    await load()
  } catch (e: any) {
    console.error('工步提交失败:', e)
    ElMessage.error(e.message)
  }
}

async function removeStep(row: any) {
  try {
    await ElMessageBox.confirm(
        `确定删除工步「${row.stepCode} ${row.stepName}」吗？若该工步已被运行配置绑定，需先解除绑定后才能删除。`,
        '删除确认',
        {type: 'warning', confirmButtonText: '删除', cancelButtonText: '取消'},
    )
  } catch {
    return
  }
  try {
    await routesApi.stepRemove(row.id)
    ElMessage.success('已删除')
    await load()
  } catch (e: any) {
    const msg = String(e?.message || '删除失败')
    if (msg.includes('运行配置') || msg.includes('绑定')) {
      ElMessageBox.confirm(`${msg}，是否前往「运行配置」解除绑定？`, '无法删除', {
        type: 'warning',
        confirmButtonText: '去解除绑定',
        cancelButtonText: '知道了',
      })
          .then(() => router.push('/equipment-config'))
          .catch(() => {
          })
    } else {
      ElMessage.error(msg)
    }
  }
}

onMounted(() => {
  load();
  loadDevices()
})
</script>

<style lang="scss" scoped>
.json-pre {
  font-size: 12px;
  margin: 0;
  max-height: 120px;
  overflow: auto;
}
</style>