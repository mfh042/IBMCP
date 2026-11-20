<template>
  <div class="page-card">
    <div class="page-header">
      <h3>设备主数据</h3>
      <el-button type="primary" @click="openDialog()">新增</el-button>
    </div>

    <div class="toolbar">
      <DictSelect v-model="filterCategory" clearable dict-key="categories" placeholder="类别筛选" style="width: 180px"
                  @change="load"/>
    </div>

    <el-table v-loading="loading" :data="list" stripe>
      <el-table-column label="ID" prop="id" width="70"/>
      <el-table-column label="设备编码" prop="deviceCode" width="120"/>
      <el-table-column label="设备名称" min-width="120" prop="deviceName"/>
      <el-table-column label="类别" width="100">
        <template #default="{ row }">
          {{ getCategoryName(row.categoryCode) }}
        </template>
      </el-table-column>
      <el-table-column label="品牌" width="100">
        <template #default="{ row }">
          {{ getBrandName(row.brandCode) }}
        </template>
      </el-table-column>
      <el-table-column label="型号" prop="modelCode" width="140"/>
      <el-table-column label="支持协议" width="160">
        <template #default="{ row }">
          <el-tag v-for="p in parseProtocols(row.supportedProtocols)" :key="p" size="small" style="margin: 2px">
            {{ getProtocolName(p) }}
          </el-tag>
          <span v-if="!parseProtocols(row.supportedProtocols).length">-</span>
        </template>
      </el-table-column>
      <el-table-column label="支持端口" width="140">
        <template #default="{ row }">
          <el-tag v-for="p in parsePorts(row.supportedPorts)" :key="p" size="small" style="margin: 2px">
            {{ getPortName(p) }}
          </el-tag>
          <span v-if="!parsePorts(row.supportedPorts).length">-</span>
        </template>
      </el-table-column>
      <el-table-column label="数据项" min-width="160">
        <template #default="{ row }">
          <el-tag v-for="item in parseDataItems(row.dataItems)" :key="item.itemCode" size="small" style="margin: 2px">
            {{ item.itemName || item.itemCode }}
          </el-tag>
          <span v-if="!parseDataItems(row.dataItems).length">-</span>
        </template>
      </el-table-column>
      <el-table-column label="描述" min-width="140" prop="description" show-overflow-tooltip/>
      <el-table-column fixed="right" label="操作" width="180">
        <template #default="{ row }">
          <el-button link size="small" @click="openDialog(row)">编辑</el-button>
          <el-popconfirm title="确认删除？" @confirm="remove(row)">
            <template #reference>
              <el-button link size="small" type="danger">删除</el-button>
            </template>
          </el-popconfirm>
        </template>
      </el-table-column>
    </el-table>

    <el-dialog v-model="dialogVisible" :title="editing ? '编辑' : '新增'" width="640px">
      <el-form :model="form" label-width="110px">
        <el-form-item label="设备编码">
          <el-input v-model="form.deviceCode"/>
        </el-form-item>
        <el-form-item label="设备名称">
          <el-input v-model="form.deviceName"/>
        </el-form-item>
        <el-form-item label="设备类别">
          <DictSelect v-model="form.categoryCode" dict-key="categories"/>
        </el-form-item>
        <el-form-item label="品牌">
          <DictSelect v-model="form.brandCode" dict-key="brands" @change="onBrandChange"/>
        </el-form-item>
        <el-form-item label="型号">
          <el-select v-model="form.modelCode" :disabled="!form.brandCode" placeholder="请先选择品牌"
                     style="width: 100%">
            <el-option v-for="m in filteredModels" :key="m.modelCode" :label="`${m.modelCode} - ${m.modelName}`"
                       :value="m.modelCode"/>
          </el-select>
        </el-form-item>
        <el-form-item label="支持协议">
          <el-select v-model="form.supportedProtocolsArr" filterable multiple style="width: 100%">
            <el-option v-for="p in protocols" :key="p.protocolCode" :label="p.protocolName || p.protocolCode"
                       :value="p.protocolCode"/>
          </el-select>
        </el-form-item>
        <el-form-item label="支持端口">
          <el-select v-model="form.supportedPortsArr" filterable multiple style="width: 100%">
            <el-option v-for="p in ports" :key="p.portCode" :label="p.portName || p.portCode" :value="p.portCode"/>
          </el-select>
        </el-form-item>
        <el-form-item label="数据项 JSON">
          <el-select v-model="form.dataItemsArr" filterable multiple placeholder="选择数据项" style="width: 100%">
            <el-option v-for="item in dataItems" :key="item.itemCode" :label="item.itemName || item.itemCode"
                       :value="item.itemCode"/>
          </el-select>
          <div v-if="dataItemsJsonPreview" class="json-preview">{{ dataItemsJsonPreview }}</div>
        </el-form-item>
        <el-form-item label="描述">
          <el-input v-model="form.description" :rows="2" type="textarea"/>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="dialogVisible = false">取消</el-button>
        <el-button type="primary" @click="submit">确定</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script lang="ts" setup>
import {computed, onMounted, reactive, ref} from 'vue'
import {ElMessage} from 'element-plus'
import DictSelect from '@/components/DictSelect.vue'
import {baseDevicesApi} from '@/api/baseDevices'
import {useDictionariesStore} from '@/stores/dictionaries'

const dictStore = useDictionariesStore()

const list = ref<any[]>([])
const loading = ref(false)
const dialogVisible = ref(false)
const editing = ref<any>(null)
const filterCategory = ref<string | number | null>(null)

const protocols = computed(() => dictStore.protocols)
const ports = computed(() => dictStore.ports)
const dataItems = computed(() => dictStore.dataItems)
const filteredModels = computed(() => {
  if (!form.brandCode) return []
  return dictStore.brands.filter((b: any) => b.brandCode === form.brandCode)
})

const dataItemsJsonPreview = computed(() => {
  if (!form.dataItemsArr || form.dataItemsArr.length === 0) return ''
  const items = form.dataItemsArr.map((code: string) => {
    const item = dataItems.value.find((d: any) => d.itemCode === code)
    return item ? {
      itemCode: item.itemCode,
      itemName: item.itemName,
      dataType: item.dataType,
      unit: item.unit
    } : {itemCode: code}
  })
  return JSON.stringify(items, null, 2)
})

const form = reactive({
  id: 0, deviceCode: '', deviceName: '', categoryCode: '', brandCode: '',
  modelCode: '',
  supportedProtocolsArr: [] as string[], supportedPortsArr: [] as string[],
  dataItemsArr: [] as string[], description: '',
})

async function load() {
  loading.value = true
  try {
    const params: any = {page: 1, size: 9999}
    if (filterCategory.value) params.categoryCode = filterCategory.value
    const data = await baseDevicesApi.deviceList(params)
    const raw: any = data
    list.value = raw?.items ?? raw?.list ?? (Array.isArray(raw) ? raw : [])
  } catch (e: any) {
    list.value = [];
    ElMessage.error(e?.message || '加载失败')
  } finally {
    loading.value = false
  }
}

function onBrandChange() {
  form.modelCode = ''
}

function parseProtocols(val: any): string[] {
  if (!val) return []
  try {
    return JSON.parse(val)
  } catch {
    return []
  }
}

function parsePorts(val: any): string[] {
  if (!val) return []
  try {
    return JSON.parse(val)
  } catch {
    return []
  }
}

function parseDataItems(val: any): any[] {
  if (!val) return []
  try {
    const raw = typeof val === 'string' ? JSON.parse(val) : val
    return Array.isArray(raw) ? raw : []
  } catch {
    return []
  }
}

function getCategoryName(code: string): string {
  if (!code) return '-'
  const cat = dictStore.categories.find((c: any) => c.categoryCode === code)
  return cat?.categoryName || code
}

function getBrandName(code: string): string {
  if (!code) return '-'
  const brand = dictStore.brands.find((b: any) => b.brandCode === code)
  return brand?.brandName || code
}

function getProtocolName(code: string): string {
  if (!code) return '-'
  const proto = dictStore.protocols.find((p: any) => p.protocolCode === code)
  return proto?.protocolName || code
}

function getPortName(code: string): string {
  if (!code) return '-'
  const port = dictStore.ports.find((p: any) => p.portCode === code)
  return port?.portName || code
}

function openDialog(row?: any) {
  editing.value = row || null
  let parsedProtocols: string[] = []
  let parsedPorts: string[] = []
  let parsedItems: string[] = []
  if (row?.supportedProtocols) {
    try {
      parsedProtocols = JSON.parse(row.supportedProtocols)
    } catch {
    }
  }
  if (row?.supportedPorts) {
    try {
      parsedPorts = JSON.parse(row.supportedPorts)
    } catch {
    }
  }
  if (row?.dataItems) {
    try {
      const raw = typeof row.dataItems === 'string' ? JSON.parse(row.dataItems) : row.dataItems
      parsedItems = Array.isArray(raw) ? raw.map((i: any) => i.itemCode).filter(Boolean) : []
    } catch {
    }
  }
  Object.assign(form, {
    id: row?.id ?? 0,
    deviceCode: row?.deviceCode ?? '',
    deviceName: row?.deviceName ?? '',
    categoryCode: row?.categoryCode ?? '',
    brandCode: row?.brandCode ?? '',
    modelCode: row?.modelCode ?? '',
    supportedProtocolsArr: parsedProtocols,
    supportedPortsArr: parsedPorts,
    dataItemsArr: parsedItems,
    description: row?.description ?? '',
  })
  dialogVisible.value = true
}

async function submit() {
  try {
    const dataItemsJson = form.dataItemsArr.map((code: string) => {
      const item = dataItems.value.find((d: any) => d.itemCode === code)
      return item ? {
        itemCode: item.itemCode,
        itemName: item.itemName,
        dataType: item.dataType,
        unit: item.unit
      } : {itemCode: code}
    })
    const body: any = {
      deviceCode: form.deviceCode,
      deviceName: form.deviceName,
      categoryCode: form.categoryCode,
      brandCode: form.brandCode,
      modelCode: form.modelCode,
      supportedProtocols: JSON.stringify(form.supportedProtocolsArr),
      supportedPorts: JSON.stringify(form.supportedPortsArr),
      dataItems: JSON.stringify(dataItemsJson),
      description: form.description,
      createdBy: 'admin',
      updatedBy: 'admin',
    }
    if (editing.value) await baseDevicesApi.deviceUpdate(editing.value.id, body)
    else await baseDevicesApi.deviceCreate(body)
    ElMessage.success('保存成功');
    dialogVisible.value = false;
    await load()
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

async function remove(row: any) {
  try {
    await baseDevicesApi.deviceRemove(row.id);
    await load();
    ElMessage.success('已删除')
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

onMounted(async () => {
  await load()
})
</script>

<style lang="scss" scoped>
.json-preview {
  margin-top: 8px;
  padding: 8px;
  background: #f5f7fa;
  border-radius: 4px;
  font-family: monospace;
  font-size: 12px;
  white-space: pre-wrap;
  word-break: break-all;
  max-height: 200px;
  overflow-y: auto;
}
</style>