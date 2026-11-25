<template>
  <div>
    <h4 style="margin-bottom: 12px">设备绑定（按工艺路线工序）</h4>
    <el-alert :closable="false" show-icon style="margin-bottom: 12px"
              title="工艺路线中每个工步已关联设备，可在此修改绑定设备并填写连接参数。" type="info"/>

    <div v-for="rb in wizard.routeBindings" :key="rb.routeConfigId || rb.routeCode" class="route-block">
      <h5>{{ rb.routeName || rb.routeCode }}</h5>
      <el-table v-if="getRouteSteps(rb.routeCode).length > 0" :data="getRouteSteps(rb.routeCode)" border size="small">
        <el-table-column label="顺序" prop="stepSeq" width="70"/>
        <el-table-column label="工序编码" prop="stepCode" width="120"/>
        <el-table-column label="工序名称" prop="stepName" width="120"/>
        <el-table-column label="绑定设备" width="260">
          <template #default="{ row }">
            <el-select v-model="row._bindDeviceCode" filterable placeholder="选择设备" size="small" style="width: 100%"
                       @change="onDeviceChange(row, $event)">
              <el-option v-for="d in devices" :key="d.deviceCode" :label="`${d.deviceCode} - ${d.deviceName}`"
                         :value="d.deviceCode"/>
            </el-select>
          </template>
        </el-table-column>
        <el-table-column label="端口" width="140">
          <template #default="{ row }">
            <el-tag v-if="row._portCode" size="small">{{ getPortName(row._portCode) }}</el-tag>
            <span v-else class="empty-text">-</span>
          </template>
        </el-table-column>
        <el-table-column label="协议" width="140">
          <template #default="{ row }">
            <el-tag v-if="row._protocolCode" size="small">{{ getProtocolName(row._protocolCode) }}</el-tag>
            <span v-else class="empty-text">-</span>
          </template>
        </el-table-column>
        <el-table-column label="连接参数" min-width="200">
          <template #default="{ row }">
            <el-input v-model="row._connectionStr" :rows="2" placeholder='{"ip":"127.0.0.1","port":502}' size="small"
                      type="textarea"/>
          </template>
        </el-table-column>
      </el-table>
      <el-empty v-else description="该路线暂无工序"/>
    </div>
  </div>
</template>

<script lang="ts" setup>
import {onMounted, ref, watch} from 'vue'
import {baseDevicesApi} from '@/api/baseDevices'
import {routesApi} from '@/api/routes'
import {useDictionariesStore} from '@/stores/dictionaries'

const dictStore = useDictionariesStore()

const props = defineProps<{ wizard: any }>()
defineEmits<{ 'update:wizard': [w: any] }>()

const devices = ref<any[]>([])
const routeStepsMap = ref<Record<string, any[]>>({})
const devicesLoaded = ref(false)

function getPortName(code: string): string {
  if (!code) return '-'
  const ports: any[] = dictStore.ports || []
  const port = ports.find((p: any) => p.portCode === code)
  return port?.portName || code
}

function getProtocolName(code: string): string {
  if (!code) return '-'
  const protocols: any[] = dictStore.protocols || []
  const proto = protocols.find((p: any) => p.protocolCode === code)
  return proto?.protocolName || code
}

function parseJsonArray(val: any): string[] {
  if (!val) return []
  try {
    const raw = typeof val === 'string' ? JSON.parse(val) : val
    return Array.isArray(raw) ? raw : []
  } catch {
    return []
  }
}

async function loadDevices() {
  try {
    const data: any = await baseDevicesApi.deviceList({size: 9999})
    devices.value = data?.items ?? (Array.isArray(data) ? data : [])
  } catch {
  }
}

async function loadRouteSteps(routeCode: string) {
  if (!routeCode) return
  const rb = props.wizard.routeBindings.find((r: any) => r.routeCode === routeCode)
  if (!rb) return

  try {
    // 工序存放在 base_process_route_step，通过 process_steps?route_code= 查询
    const data: any = await routesApi.stepList({routeCode, size: 999})
    const steps: any[] = data?.items ?? (Array.isArray(data) ? data : [])
    steps.sort((a: any, b: any) => (a.stepSeq || 0) - (b.stepSeq || 0))

    for (const s of steps) {
      // 编辑模式下用 stepCode 匹配已有绑定（running_route_device 无 step_id 列）
      const existingBinding = props.wizard.deviceBindings.find(
          (d: any) => d.routeCode === routeCode && d.stepCode === s.stepCode
      )
      s._bindDeviceCode = existingBinding?.deviceCode || s.deviceCode || ''
      s._deviceName = existingBinding?.deviceName || ''
      s._portCode = existingBinding?.portCode || ''
      s._protocolCode = existingBinding?.protocolCode || ''
      s._connectionStr = existingBinding?.deviceConnectionStr || ''
      s._stepId = s.id
      s._stepCode = s.stepCode
      s._stepName = s.stepName
      s._stepSeq = s.stepSeq
      s._routeCode = routeCode
      s._bindingId = existingBinding?.id || 0

      // 设备已选但端口/协议为空时，从设备主数据自动补全
      if (s._bindDeviceCode && (!s._portCode || !s._protocolCode)) {
        const device = devices.value.find((d: any) => d.deviceCode === s._bindDeviceCode)
        if (device) {
          const ports = parseJsonArray(device.supportedPorts)
          const protocols = parseJsonArray(device.supportedProtocols)
          if (!s._portCode && ports.length > 0) s._portCode = ports[0]
          if (!s._protocolCode && protocols.length > 0) s._protocolCode = protocols[0]
        }
      }
    }
    routeStepsMap.value[routeCode] = steps
  } catch (e) {
    console.error('[Step3] Failed to load route steps:', e)
  }
}

// Watch for route binding changes and reload steps
watch(
    () => props.wizard.routeBindings.map((r: any) => `${r.routeCode}-${r.routeId}`).join(','),
    async (newVal, oldVal) => {
      if (!newVal || newVal === oldVal) return
      console.log('[Step3] Route bindings changed:', newVal)
      // Clear cache and reload all route steps
      routeStepsMap.value = {}
      for (const rb of props.wizard.routeBindings) {
        if (rb.routeCode) {
          await loadRouteSteps(rb.routeCode)
        }
      }
    }
)

function getRouteSteps(routeCode: string): any[] {
  return routeStepsMap.value[routeCode] || []
}

function onDeviceChange(step: any, code: string) {
  const device = devices.value.find((d) => d.deviceCode === code)
  if (device && step) {
    step._deviceName = device.deviceName || ''
    // Auto-fill port and protocol from device's supported data
    const ports = parseJsonArray(device.supportedPorts)
    const protocols = parseJsonArray(device.supportedProtocols)
    step._portCode = ports.length > 0 ? ports[0] : ''
    step._protocolCode = protocols.length > 0 ? protocols[0] : ''
  }
}

// Sync step bindings back to wizard.deviceBindings before submit
function syncDeviceBindings() {
  const newBindings: any[] = []
  for (const routeCode of Object.keys(routeStepsMap.value)) {
    const steps = routeStepsMap.value[routeCode] || []
    for (const s of steps) {
      // 用 stepCode 匹配已有绑定，保留 Step4 已配置的 collections
      const existingBinding = props.wizard.deviceBindings.find(
          (d: any) => d.routeCode === s._routeCode && d.stepCode === s._stepCode
      )
      newBindings.push({
        id: s._bindingId || 0,
        routeCode: s._routeCode,
        stepId: s._stepId || 0,
        stepCode: s._stepCode || '',
        stepName: s._stepName || '',
        stepSeq: s._stepSeq || 0,
        deviceCode: s._bindDeviceCode || '',
        deviceName: s._deviceName || '',
        deviceConnectionStr: s._connectionStr || '',
        portCode: s._portCode || '',
        protocolCode: s._protocolCode || '',
        collections: existingBinding?.collections || [],
      })
    }
  }
  props.wizard.deviceBindings = newBindings
}

// Expose sync for parent to call before submit
defineExpose({syncDeviceBindings})

onMounted(async () => {
  await loadDevices()
  devicesLoaded.value = true
  // Load steps for all bound routes after devices are loaded
  for (const rb of props.wizard.routeBindings) {
    if (rb.routeCode) {
      await loadRouteSteps(rb.routeCode)
    }
  }
})
</script>

<style lang="scss" scoped>
.route-block {
  padding: 12px;
  border: 1px solid #e2e8f0;
  border-radius: 6px;
  margin-bottom: 16px;
}

.empty-text {
  color: #94a3b8;
  font-size: 12px;
}
</style>