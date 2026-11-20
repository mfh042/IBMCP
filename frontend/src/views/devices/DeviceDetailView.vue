<template>
  <div class="page-card device-detail">
    <div class="page-header">
      <h3>设备详情 #{{ deviceId }}</h3>
      <el-button @click="$router.back()">返回</el-button>
    </div>

    <el-tabs v-model="activeTab" @tab-change="onTabChange">
      <el-tab-pane label="基本信息" name="basic">
        <el-form :model="form" label-width="100px" style="max-width: 600px">
          <el-form-item label="设备编码">
            <el-input v-model="form.deviceCode"/>
          </el-form-item>
          <el-form-item label="设备名称">
            <el-input v-model="form.deviceName"/>
          </el-form-item>
          <el-form-item label="协议">
            <el-input v-model="form.protocolCode"/>
          </el-form-item>
          <el-form-item label="连接参数">
            <JsonEditor v-model="form.deviceConnectionStr" :rows="6"/>
          </el-form-item>
          <el-form-item>
            <el-button type="primary" @click="saveDevice">保存</el-button>
          </el-form-item>
        </el-form>
      </el-tab-pane>

      <el-tab-pane label="点位配置" name="points">
        <div class="toolbar">
          <el-button disabled type="primary">新增点位</el-button>
        </div>
        <el-table :data="points" border stripe>
          <el-table-column label="ID" prop="id" width="70"/>
          <el-table-column label="名称" min-width="140" prop="itemName"/>
          <el-table-column label="地址" prop="address" width="160"/>
          <el-table-column label="数据类型" prop="dataType" width="100"/>
          <el-table-column label="系数" prop="scale" width="80"/>
          <el-table-column label="单位" prop="unit" width="80"/>
          <el-table-column label="采集周期(秒)" width="120">
            <template #default="{ row }">
              {{ row.collectionCycle ?? '-' }}
            </template>
          </el-table-column>
          <el-table-column label="高限" width="100">
            <template #default="{ row }">
              <el-input-number v-model="row.alarmHigh" :controls="false" size="small"/>
            </template>
          </el-table-column>
          <el-table-column label="低限" width="100">
            <template #default="{ row }">
              <el-input-number v-model="row.alarmLow" :controls="false" size="small"/>
            </template>
          </el-table-column>
          <el-table-column label="启用报警" width="100">
            <template #default="{ row }">
              <el-switch v-model="row.alarmEnabled"/>
            </template>
          </el-table-column>
          <el-table-column label="操作" width="140">
            <template #default="{ row }">
              <el-button link size="small" @click="openPointDialog(row)">编辑</el-button>
              <el-popconfirm title="确认删除该点位？" @confirm="removePoint(row)">
                <template #reference>
                  <el-button link size="small" type="danger">删除</el-button>
                </template>
              </el-popconfirm>
            </template>
          </el-table-column>
        </el-table>
        <div style="margin-top: 12px">
          <el-button :loading="savingPoints" type="primary" @click="savePoints">保存点位</el-button>
        </div>
      </el-tab-pane>

      <el-tab-pane label="控制命令" name="control">
        <el-button type="primary" @click="controlVisible = true">下发控制命令</el-button>
      </el-tab-pane>

      <el-tab-pane label="命令历史" name="history">
        <el-table v-loading="cmdsLoading" :data="commands" border stripe>
          <el-table-column label="ID" prop="id" width="70"/>
          <el-table-column label="点位" min-width="140">
            <template #default="{ row }">{{ pointNameOf(row) }}</template>
          </el-table-column>
          <el-table-column label="值" prop="value" width="120"/>
          <el-table-column label="状态" width="100">
            <template #default="{ row }">
              <el-tag :type="statusType(row.status)" size="small">{{ statusText(row.status) }}</el-tag>
            </template>
          </el-table-column>
          <el-table-column label="下发时间" width="180">
            <template #default="{ row }">{{ formatTime(row.issuedAt) }}</template>
          </el-table-column>
          <el-table-column label="完成时间" width="180">
            <template #default="{ row }">{{ formatTime(row.finishedAt) }}</template>
          </el-table-column>
        </el-table>
      </el-tab-pane>
    </el-tabs>

    <el-dialog v-model="pointDialogVisible" :title="editingPoint ? '编辑点位' : '新增点位'" width="500px">
      <el-form :model="pointForm" label-width="100px">
        <el-form-item label="名称">
          <el-select
              v-model="selectedItemCode"
              filterable
              placeholder="从开放数据项中选择"
              style="width: 100%"
              @change="onItemCodeChange"
          >
            <el-option
                v-for="it in itemOptions"
                :key="it.itemCode || it.id"
                :label="it.itemName"
                :value="it.itemCode"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="地址">
          <el-select
              v-model="pointForm.address"
              allow-create
              default-first-option
              filterable
              placeholder="选择地址规则或手动输入"
              style="width: 100%"
          >
            <el-option v-for="a in addressOptions" :key="a" :label="a" :value="a"/>
          </el-select>
        </el-form-item>
        <el-form-item label="数据类型">
          <el-select v-model="pointForm.dataType" style="width: 100%">
            <el-option v-for="t in dataTypeOptions" :key="t" :label="t" :value="t"/>
          </el-select>
        </el-form-item>
        <el-form-item label="系数">
          <el-input-number v-model="pointForm.scale" :controls="false"/>
        </el-form-item>
        <el-form-item label="偏移">
          <el-input-number v-model="pointForm.offset" :controls="false"/>
        </el-form-item>
        <el-form-item label="单位">
          <el-select
              v-model="pointForm.unit"
              allow-create
              clearable
              default-first-option
              filterable
              placeholder="选择或输入单位"
              style="width: 100%"
          >
            <el-option v-for="u in unitOptions" :key="u" :label="u" :value="u"/>
          </el-select>
        </el-form-item>
        <el-form-item label="采集周期(秒)">
          <el-select v-model="pointForm.collectionCycle" placeholder="选择采集周期" style="width: 100%">
            <el-option v-for="c in cycleOptions" :key="c" :label="`${c} 秒`" :value="c"/>
          </el-select>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="pointDialogVisible = false">取消</el-button>
        <el-button type="primary" @click="confirmPoint">确定</el-button>
      </template>
    </el-dialog>

    <ControlDialog v-model="controlVisible" :device-id="deviceId" :points="points"/>
  </div>
</template>

<script lang="ts" setup>
import {computed, onMounted, reactive, ref} from 'vue'
import {useRoute} from 'vue-router'
import {ElMessage} from 'element-plus'
import {devicesApi} from '@/api/devices'
import {configsApi} from '@/api/configs'
import {baseDevicesApi} from '@/api/baseDevices'
import {formatTime} from '@/utils/time'
import JsonEditor from '@/components/JsonEditor.vue'
import ControlDialog from '@/components/ControlDialog.vue'

const route = useRoute()
const deviceId = computed(() => Number(route.params.id))

const activeTab = ref('basic')
const form = reactive({
  deviceCode: '',
  deviceName: '',
  protocolCode: '',
  portCode: '',
  deviceConnectionStr: '',
})
const points = ref<any[]>([])
const savingPoints = ref(false)
const commands = ref<any[]>([])
const cmdsLoading = ref(false)
const controlVisible = ref(false)
const pointDialogVisible = ref(false)
const editingPoint = ref<any>(null)
const pointForm = reactive<any>({})

const dataItems = ref<any[]>([])
const selectedItemCode = ref<string>('')
const legacyItemOption = ref<any>(null)

const itemOptions = computed(() => {
  const list = dataItems.value.slice()
  if (legacyItemOption.value) list.unshift(legacyItemOption.value)
  return list
})

const currentItem = computed(() =>
    itemOptions.value.find((it) => it.itemCode === selectedItemCode.value),
)

const addressOptions = computed(() => {
  const picked = currentItem.value
  const pool = picked ? [picked] : dataItems.value
  const set = new Set<string>()
  pool.forEach((it) => {
    const addr = it.addressRule ?? it.address_rule ?? ''
    if (addr) set.add(addr)
  })
  if (pointForm.address) set.add(pointForm.address)
  return Array.from(set)
})

const unitOptions = computed(() => {
  const picked = currentItem.value
  const pool = picked ? [picked] : dataItems.value
  const set = new Set<string>()
  pool.forEach((it) => {
    if (it.unit) set.add(it.unit)
  })
  if (pointForm.unit) set.add(pointForm.unit)
  return Array.from(set)
})

const dataTypeOptions = computed(() => {
  const set = new Set<string>(['INT16', 'INT32', 'FLOAT', 'DOUBLE', 'BOOL', 'STRING'])
  dataItems.value.forEach((it) => {
    if (it.dataType) set.add(it.dataType)
  })
  if (pointForm.dataType) set.add(pointForm.dataType)
  return Array.from(set)
})

const cycleOptions = computed(() => {
  const set = new Set<number>([1, 2, 5, 10, 30, 60])
  dataItems.value.forEach((it) => {
    const c = Number(it.collectionCycle)
    if (c > 0) set.add(c)
  })
  const cur = Number(pointForm.collectionCycle)
  if (cur > 0) set.add(cur)
  return Array.from(set).sort((a, b) => a - b)
})

async function loadDataItems() {
  try {
    const data: any = await baseDevicesApi.itemList({page: 1, size: 9999})
    dataItems.value = data?.list ?? data?.items ?? (Array.isArray(data) ? data : [])
  } catch (e: any) {
    dataItems.value = []
  }
}

function onItemCodeChange(code: string) {
  const item = itemOptions.value.find((it) => it.itemCode === code)
  if (!item) return
  pointForm.itemName = item.itemName || ''
  const addr = item.addressRule ?? item.address_rule ?? ''
  if (addr) pointForm.address = addr
  pointForm.dataType = item.dataType || pointForm.dataType || 'FLOAT'
  if (item.unit) pointForm.unit = item.unit
  const cycle = Number(item.collectionCycle)
  if (cycle > 0) pointForm.collectionCycle = cycle
}

async function load() {
  try {
    const d: any = await devicesApi.get(deviceId.value)
    form.deviceCode = d.deviceCode || ''
    form.deviceName = d.deviceName || ''
    form.protocolCode = d.protocolCode || ''
    form.portCode = d.portCode || ''
    form.deviceConnectionStr = d.deviceConnection ? JSON.stringify(d.deviceConnection, null, 2) : ''

    const pts: any = await configsApi.listDevicePoints(deviceId.value)
    points.value = (pts as any)?.list ?? (Array.isArray(pts) ? pts : [])
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

function pointNameOf(row: any) {
  if (row.pointName) return row.pointName
  const pt = points.value.find((p: any) => p.id === row.pointId)
  return pt ? (pt.itemName || pt.name || `#${row.pointId}`) : `#${row.pointId}`
}

function onTabChange(name: any) {
  if (name === 'history') loadCommands()
}

async function loadCommands() {
  cmdsLoading.value = true
  try {
    const res: any = await devicesApi.commands(deviceId.value)
    commands.value = Array.isArray(res) ? res : ((res as any)?.list ?? (res as any)?.items ?? [])
  } catch (e: any) {
    commands.value = []
    ElMessage.error(e.message)
  } finally {
    cmdsLoading.value = false
  }
}

async function saveDevice() {
  try {
    const deviceConnection = form.deviceConnectionStr ? JSON.parse(form.deviceConnectionStr) : {}
    await configsApi.upsertDevice({
      id: deviceId.value,
      deviceCode: form.deviceCode,
      deviceName: form.deviceName,
      protocolCode: form.protocolCode,
      portCode: form.portCode,
      deviceConnection,
    })
    ElMessage.success('保存成功')
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

function openPointDialog(row?: any) {
  editingPoint.value = row || null
  Object.keys(pointForm).forEach((k) => delete pointForm[k])
  Object.assign(
      pointForm,
      row || {id: 0, itemName: '', address: '', dataType: 'FLOAT', scale: 1, offset: 0, unit: '', collectionCycle: 1},
  )
  const matched = row
      ? dataItems.value.find((it) => it.itemName === row.itemName)
      : undefined
  if (matched) {
    legacyItemOption.value = null
    selectedItemCode.value = matched.itemCode
  } else if (row && row.itemName) {
    legacyItemOption.value = {
      itemCode: `__custom__${row.id ?? row.itemName}`,
      itemName: row.itemName,
      addressRule: row.address ?? '',
      dataType: row.dataType ?? '',
      unit: row.unit ?? '',
      collectionCycle: row.collectionCycle ?? 1,
    }
    selectedItemCode.value = legacyItemOption.value.itemCode
  } else {
    legacyItemOption.value = null
    selectedItemCode.value = ''
  }
  pointDialogVisible.value = true
}

async function confirmPoint() {
  if (!pointForm.itemName) {
    ElMessage.warning('请选择点位名称')
    return
  }
  if (!pointForm.address) {
    ElMessage.warning('请选择或输入点位地址')
    return
  }
  try {
    await configsApi.upsertPoint({...pointForm, deviceId: deviceId.value})
    ElMessage.success('点位保存成功')
    pointDialogVisible.value = false
    await load()
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

async function removePoint(row: any) {
  try {
    await configsApi.removePoint(row.id)
    ElMessage.success('已删除')
    await load()
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

async function savePoints() {
  if (!points.value.length) return
  savingPoints.value = true
  try {
    for (const row of points.value) {
      await configsApi.upsertPoint({
        id: row.id,
        deviceId: deviceId.value,
        deviceCode: row.deviceCode,
        itemName: row.itemName,
        address: row.address,
        dataType: row.dataType,
        unit: row.unit,
        scale: row.scale,
        offset: row.offset,
        collectionCycle: row.collectionCycle,
        alarmHigh: row.alarmHigh,
        alarmLow: row.alarmLow,
        alarmEnabled: row.alarmEnabled,
        enabled: row.enabled,
      })
    }
    ElMessage.success('点位已更新')
    await load()
  } catch (e: any) {
    ElMessage.error(e.message || '保存失败')
  } finally {
    savingPoints.value = false
  }
}

function statusType(s: number) {
  return s === 2 ? 'success' : s === 3 ? 'danger' : 'info'
}

function statusText(s: number) {
  return s === 0 ? '待下发' : s === 1 ? '下发中' : s === 2 ? '成功' : s === 3 ? '失败' : '超时'
}

onMounted(async () => {
  await loadDataItems()
  await load()
})
</script>