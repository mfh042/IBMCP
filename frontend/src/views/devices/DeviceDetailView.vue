<template>
  <div class="page-card device-detail">
    <div class="page-header">
      <h3>设备详情 - {{ deviceCode }}</h3>
      <el-button @click="$router.back()">返回</el-button>
    </div>

    <el-tabs v-model="activeTab" @tab-change="onTabChange">
      <el-tab-pane label="基本信息" name="basic">
        <el-form :model="form" label-width="100px" style="max-width: 600px">
          <el-form-item label="设备编码">
            <el-input v-model="form.deviceCode" disabled/>
          </el-form-item>
          <el-form-item label="设备名称">
            <el-input v-model="form.deviceName"/>
          </el-form-item>
          <el-form-item label="协议">
            <el-input v-model="form.protocolCode" disabled/>
          </el-form-item>
          <el-form-item label="端口">
            <el-input v-model="form.portCode"/>
          </el-form-item>
          <el-form-item label="运行状态">
            <el-tag :type="statusTagType(runtimeInfo.status)" effect="dark" size="small">
              {{ statusText(runtimeInfo.status) }}
            </el-tag>
            <span v-if="runtimeInfo.lastError" style="margin-left: 12px; color: #ef4444; font-size: 12px">
              {{ runtimeInfo.lastError }}
            </span>
          </el-form-item>
          <el-form-item>
            <el-button type="primary" @click="saveDevice">保存名称 / 端口</el-button>
            <span class="hint">协议与连接参数来自运行计划，如需修改请在「装备配置」中调整后重新激活。</span>
          </el-form-item>
        </el-form>
      </el-tab-pane>

      <el-tab-pane label="点位配置" name="points">
        <div class="toolbar">
          <el-button disabled type="primary">新增点位</el-button>
          <span class="hint">点位变更写入运行计划（running_device_collection），重新激活配置后投影到运行时。</span>
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
        <div class="toolbar">
          <el-input-number v-model="queryCommandId" :min="1" placeholder="命令 ID" style="width: 180px"/>
          <el-button :loading="cmdsLoading" type="primary" @click="loadCommands">查询</el-button>
          <span class="hint">后端仅支持按命令 ID 单条查询（/api/v1/control/commands/{command_id}）</span>
        </div>
        <el-table v-loading="cmdsLoading" :data="commands" border stripe>
          <el-table-column label="ID" prop="id" width="70"/>
          <el-table-column label="点位" min-width="140">
            <template #default="{ row }">{{ pointNameOf(row) }}</template>
          </el-table-column>
          <el-table-column label="值" prop="value" width="120"/>
          <el-table-column label="状态" width="100">
            <template #default="{ row }">
              <el-tag :type="cmdStatusType(row.status)" size="small">{{ cmdStatusText(row.status) }}</el-tag>
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

    <ControlDialog v-model="controlVisible" :device-code="deviceCode" :points="points"/>
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
import ControlDialog from '@/components/ControlDialog.vue'

const route = useRoute()
const deviceCode = computed(() => String(route.params.id ?? ''))

const activeTab = ref('basic')
const form = reactive({
  deviceCode: '',
  deviceName: '',
  protocolCode: '',
  portCode: '',
})
const runtimeInfo = ref<any>({})
const masterId = ref(0)
const points = ref<any[]>([])
const savingPoints = ref(false)
const commands = ref<any[]>([])
const cmdsLoading = ref(false)
const controlVisible = ref(false)
const queryCommandId = ref<number | null>(null)
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

// running_device_collection 可写列白名单（address -> address_override，剔除 unit/deviceId 等不存在列）
function toCollectionPayload(src: any) {
  const p: any = {}
  if (src?.itemName != null) p.itemName = src.itemName
  if (src?.itemCode != null) p.itemCode = src.itemCode
  if (src?.address != null) p.addressOverride = src.address
  if (src?.dataType != null) p.dataType = src.dataType
  if (src?.readWrite != null) p.readWrite = src.readWrite
  if (src?.scale != null) p.scale = src.scale
  if (src?.offset != null) p.offset = src.offset
  if (src?.collectionCycle != null) p.collectionCycle = src.collectionCycle
  if (src?.alarmHigh != null) p.alarmHigh = src.alarmHigh
  if (src?.alarmLow != null) p.alarmLow = src.alarmLow
  if (src?.alarmEnabled != null) p.alarmEnabled = src.alarmEnabled
  if (src?.enabled != null) p.enabled = src.enabled ? 1 : 0
  return p
}

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
  masterId.value = 0
  runtimeInfo.value = {}
  try {
    const d: any = await devicesApi.get(deviceCode.value)
    runtimeInfo.value = d || {}
    form.deviceCode = d?.deviceKey || deviceCode.value
    form.protocolCode = d?.protocolCode || ''

    try {
      const m: any = await baseDevicesApi.deviceGetByCode(deviceCode.value)
      if (m?.id) {
        masterId.value = m.id
        form.deviceName = m.deviceName || ''
        form.portCode = m.portCode || ''
      }
    } catch {
      // 静态主数据中不存在该设备编码：名称/端口不可保存
    }

    const pts: any = await configsApi.listDevicePoints(deviceCode.value)
    points.value = pts?.items ?? pts?.list ?? (Array.isArray(pts) ? pts : [])
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
  if (!queryCommandId.value) {
    commands.value = []
    return
  }
  cmdsLoading.value = true
  try {
    const res: any = await devicesApi.command(queryCommandId.value)
    commands.value = res ? [res] : []
  } catch (e: any) {
    commands.value = []
    ElMessage.error(e.message)
  } finally {
    cmdsLoading.value = false
  }
}

async function saveDevice() {
  if (!masterId.value) {
    ElMessage.warning('未在静态设备主数据中找到该设备编码，无法保存名称/端口')
    return
  }
  try {
    await baseDevicesApi.deviceUpdate(masterId.value, {
      deviceName: form.deviceName,
      portCode: form.portCode,
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
  const planId = Number(pointForm.usingPointId ?? pointForm.id ?? 0)
  if (!planId) {
    ElMessage.warning('该点位未投影到运行计划，无法编辑；请先在装备配置中激活')
    return
  }
  try {
    await configsApi.upsertPoint({id: planId, ...toCollectionPayload(pointForm)})
    ElMessage.success('点位保存成功')
    pointDialogVisible.value = false
    await load()
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

async function removePoint(row: any) {
  const planId = Number(row?.usingPointId ?? 0)
  if (!planId) {
    ElMessage.warning('该点位未投影到运行计划，无法删除')
    return
  }
  try {
    await configsApi.removePoint(planId)
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
    let skipped = 0
    for (const row of points.value) {
      const planId = Number(row?.usingPointId ?? 0)
      if (!planId) {
        skipped++
        continue
      }
      await configsApi.upsertPoint({id: planId, ...toCollectionPayload(row)})
    }
    if (skipped > 0) {
      ElMessage.warning(`已更新 ${points.value.length - skipped} 个点位，${skipped} 个未投影到运行计划已跳过`)
    } else {
      ElMessage.success('点位已更新')
    }
    await load()
  } catch (e: any) {
    ElMessage.error(e.message || '保存失败')
  } finally {
    savingPoints.value = false
  }
}

function statusTagType(status: number) {
  return status === 1 ? 'success' : status === 2 ? 'danger' : 'info'
}

function statusText(status: number) {
  return status === 1 ? '在线' : status === 2 ? '故障' : '离线'
}

function cmdStatusType(s: number) {
  return s === 2 ? 'success' : s === 3 ? 'danger' : 'info'
}

function cmdStatusText(s: number) {
  return s === 0 ? '待下发' : s === 1 ? '下发中' : s === 2 ? '成功' : s === 3 ? '失败' : '超时'
}

onMounted(async () => {
  await loadDataItems()
  await load()
})
</script>

<style lang="scss" scoped>
.hint {
  margin-left: 12px;
  color: #94a3b8;
  font-size: 12px;
}
</style>