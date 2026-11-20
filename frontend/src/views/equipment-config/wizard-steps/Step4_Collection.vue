<template>
  <div>
    <h4 style="margin-bottom: 12px">采集项配置</h4>
    <el-alert :closable="false" show-icon style="margin-bottom: 12px" title="为每个设备绑定选择需要采集的数据项并设置采集参数。"
              type="info"/>

    <div v-for="(bd, idx) in wizard.deviceBindings" :key="idx" class="device-collections">
      <h5>{{ bd.deviceName || bd.deviceCode || '(未绑定设备)' }}
        <el-tag size="small" type="info">{{ bd.stepName || bd.stepCode }}</el-tag>
      </h5>
      <el-table :data="bd.collections" border size="small">
        <el-table-column label="采集项" min-width="180">
          <template #default="{ row }">
            <el-select v-model="row.itemCode" filterable placeholder="选择数据项" size="small" style="width: 100%"
                       @change="onItemChange(row)">
              <el-option v-for="di in dataItems" :key="di.itemCode" :label="di.itemName || di.itemCode"
                         :value="di.itemCode"/>
            </el-select>
          </template>
        </el-table-column>
        <el-table-column align="center" label="数据类型" width="90">
          <template #default="{ row }">
            <el-tag v-if="row._dataType" :type="getDataTypeTag(row._dataType)" size="small">{{ row._dataType }}</el-tag>
            <span v-else class="empty-text">-</span>
          </template>
        </el-table-column>
        <el-table-column align="center" label="读写" width="70">
          <template #default="{ row }">
            <el-tag v-if="row._readWrite" :type="getReadWriteTag(row._readWrite)" size="small">{{
                row._readWrite
              }}
            </el-tag>
            <span v-else class="empty-text">-</span>
          </template>
        </el-table-column>
        <el-table-column align="center" label="单位" width="70">
          <template #default="{ row }">
            <span v-if="row._unit">{{ row._unit }}</span>
            <span v-else class="empty-text">-</span>
          </template>
        </el-table-column>
        <el-table-column align="center" label="精度" width="60">
          <template #default="{ row }">
            <span v-if="row._precision !== null && row._precision !== undefined">{{ row._precision }}</span>
            <span v-else class="empty-text">-</span>
          </template>
        </el-table-column>
        <el-table-column align="center" label="范围" width="120">
          <template #default="{ row }">
            <span
                v-if="row._minValue !== null && row._minValue !== undefined && row._maxValue !== null && row._maxValue !== undefined">
              {{ row._minValue }} ~ {{ row._maxValue }}
            </span>
            <span v-else class="empty-text">-</span>
          </template>
        </el-table-column>
        <el-table-column label="地址规则" width="120">
          <template #default="{ row }">
            <span v-if="row._addressRule" class="address-rule">{{ row._addressRule }}</span>
            <span v-else class="empty-text">-</span>
          </template>
        </el-table-column>
        <el-table-column label="周期(ms)" width="100">
          <template #default="{ row }">
            <el-input-number v-model="row.collectionCycle" :controls="false" :min="100" :step="100" size="small"
                             style="width: 100%"/>
          </template>
        </el-table-column>
        <el-table-column label="地址覆盖" width="120">
          <template #default="{ row }">
            <el-input v-model="row.addressOverride" placeholder="覆盖地址" size="small"/>
          </template>
        </el-table-column>
        <el-table-column align="center" label="启用" width="70">
          <template #default="{ row }">
            <el-switch v-model="row.enabled" size="small"/>
          </template>
        </el-table-column>
        <el-table-column align="center" label="操作" width="70">
          <template #default="{ $index }">
            <el-button link size="small" type="danger" @click="bd.collections.splice($index, 1)">删除</el-button>
          </template>
        </el-table-column>
      </el-table>
      <el-button link size="small" type="primary"
                 @click="bd.collections.push({ id: 0, itemCode: '', itemName: '', collectionCycle: 1000, addressOverride: '', enabled: true })">
        + 添加采集项
      </el-button>
    </div>

    <el-empty v-if="!wizard.deviceBindings || wizard.deviceBindings.length === 0"
              description="请先在设备绑定步骤中绑定设备"/>
  </div>
</template>

<script lang="ts" setup>
import {onMounted, ref} from 'vue'
import {baseDevicesApi} from '@/api/baseDevices'

const props = defineProps<{ wizard: any }>()
defineEmits<{ 'update:wizard': [w: any] }>()

const dataItems = ref<any[]>([])

async function loadDataItems() {
  try {
    const data = await baseDevicesApi.itemList({page: 1, size: 9999})
    dataItems.value = (data as any)?.items ?? (Array.isArray(data) ? data : [])
  } catch {
  }
}

function onItemChange(c: any) {
  const item = dataItems.value.find((di) => di.itemCode === c.itemCode)
  if (item && c) {
    c.itemName = item.itemName || ''
    c._dataType = item.dataType || ''
    c._readWrite = item.readWrite || ''
    c._unit = item.unit || ''
    c._precision = item.precision ?? null
    c._minValue = item.minValue ?? item.min_value ?? null
    c._maxValue = item.maxValue ?? item.max_value ?? null
    c._addressRule = item.addressRule || item.address_rule || ''
    // Auto-fill collection cycle from data item default
    if (item.collectionCycle && !c.collectionCycle) {
      c.collectionCycle = item.collectionCycle
    }
  }
}

function getDataTypeTag(type: string): string | undefined {
  const map: Record<string, string> = {
    INT: 'info', INT16: 'info', INT32: 'info',
    FLOAT: 'success', DOUBLE: 'success',
    BOOL: 'warning', STRING: '',
  }
  return map[type] || undefined
}

function getReadWriteTag(rw: string): string | undefined {
  const map: Record<string, string> = {
    R: 'info', W: 'warning', RW: 'success',
  }
  return map[rw] || undefined
}

onMounted(loadDataItems)
</script>

<style lang="scss" scoped>
.device-collections {
  padding: 12px;
  border: 1px solid #e2e8f0;
  border-radius: 6px;
  margin-bottom: 16px;
}

.empty-text {
  color: #94a3b8;
  font-size: 12px;
}

.address-rule {
  font-family: 'Courier New', monospace;
  font-size: 12px;
  color: #475569;
}
</style>