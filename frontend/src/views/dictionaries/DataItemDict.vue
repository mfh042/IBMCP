<template>
  <div class="page-card">
    <div class="page-header">
      <h3>开放数据项字典</h3>
      <el-button type="primary" @click="openDialog()">新增</el-button>
    </div>

    <div class="toolbar">
      <el-input v-model="keyword" clearable placeholder="搜索编码/名称" style="width: 240px" @keyup.enter="load"/>
      <el-button :icon="Search" type="primary" @click="load">搜索</el-button>
    </div>

    <el-table v-loading="loading" :data="list" stripe>
      <el-table-column label="ID" prop="id" width="60"/>
      <el-table-column label="编码" prop="itemCode" width="180"/>
      <el-table-column label="名称" prop="itemName" width="120"/>
      <el-table-column label="类别" width="100">
        <template #default="{ row }">
          {{ getCategoryName(row.categoryCode) }}
        </template>
      </el-table-column>
      <el-table-column label="协议" width="100">
        <template #default="{ row }">
          {{ getProtocolName(row.protocolCode) }}
        </template>
      </el-table-column>
      <el-table-column label="类型" prop="dataType" width="80">
        <template #default="{ row }">
          <el-tag :type="getDataTypeTag(row.dataType)" size="small">{{ row.dataType }}</el-tag>
        </template>
      </el-table-column>
      <el-table-column label="读写" width="70">
        <template #default="{ row }">
          <el-tag :type="row.readWrite === 'W' ? 'warning' : 'success'" size="small">{{ row.readWrite || 'R' }}</el-tag>
        </template>
      </el-table-column>
      <el-table-column label="单位" prop="unit" width="70"/>
      <el-table-column label="精度" prop="precision" width="60"/>
      <el-table-column label="最小值" width="100">
        <template #default="{ row }">
          {{ row.minValue ?? row.min_value ?? '-' }}
        </template>
      </el-table-column>
      <el-table-column label="最大值" width="100">
        <template #default="{ row }">
          {{ row.maxValue ?? row.max_value ?? '-' }}
        </template>
      </el-table-column>
      <el-table-column label="地址规则" width="140">
        <template #default="{ row }">
          {{ row.addressRule ?? row.address_rule ?? '-' }}
        </template>
      </el-table-column>
      <el-table-column label="周期(ms)" prop="collectionCycle" width="80"/>
      <el-table-column label="描述" min-width="160" prop="description" show-overflow-tooltip/>
      <el-table-column fixed="right" label="操作" width="140">
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

    <el-dialog v-model="dialogVisible" :title="editing ? '编辑' : '新增'" width="680px">
      <el-form :model="form" label-width="100px">
        <el-row :gutter="16">
          <el-col :span="12">
            <el-form-item label="编码">
              <el-input v-model="form.itemCode" placeholder="如 CNC_SPINDLE_SPEED"/>
            </el-form-item>
          </el-col>
          <el-col :span="12">
            <el-form-item label="名称">
              <el-input v-model="form.itemName" placeholder="如 主轴转速"/>
            </el-form-item>
          </el-col>
        </el-row>
        <el-row :gutter="16">
          <el-col :span="12">
            <el-form-item label="类别">
              <DictSelect v-model="form.categoryCode" dict-key="categories" placeholder="选择类别"/>
            </el-form-item>
          </el-col>
          <el-col :span="12">
            <el-form-item label="协议">
              <DictSelect v-model="form.protocolCode" dict-key="protocols" placeholder="选择协议"/>
            </el-form-item>
          </el-col>
        </el-row>
        <el-row :gutter="16">
          <el-col :span="12">
            <el-form-item label="数据类型">
              <el-select v-model="form.dataType" style="width: 100%">
                <el-option label="INT" value="INT"/>
                <el-option label="INT16" value="INT16"/>
                <el-option label="INT32" value="INT32"/>
                <el-option label="FLOAT" value="FLOAT"/>
                <el-option label="DOUBLE" value="DOUBLE"/>
                <el-option label="BOOL" value="BOOL"/>
                <el-option label="STRING" value="STRING"/>
              </el-select>
            </el-form-item>
          </el-col>
          <el-col :span="12">
            <el-form-item label="读写属性">
              <el-select v-model="form.readWrite" style="width: 100%">
                <el-option label="只读 (R)" value="R"/>
                <el-option label="只写 (W)" value="W"/>
                <el-option label="读写 (RW)" value="RW"/>
              </el-select>
            </el-form-item>
          </el-col>
        </el-row>
        <el-row :gutter="16">
          <el-col :span="8">
            <el-form-item label="单位">
              <el-input v-model="form.unit" placeholder="如 rpm"/>
            </el-form-item>
          </el-col>
          <el-col :span="8">
            <el-form-item label="精度">
              <el-input-number v-model="form.precision" :max="10" :min="0" style="width: 100%"/>
            </el-form-item>
          </el-col>
          <el-col :span="8">
            <el-form-item label="采集周期(ms)">
              <el-input-number v-model="form.collectionCycle" :min="100" :step="100" style="width: 100%"/>
            </el-form-item>
          </el-col>
        </el-row>
        <el-row :gutter="16">
          <el-col :span="12">
            <el-form-item label="最小值">
              <el-input-number v-model="form.minValue" :precision="4" placeholder="可选" style="width: 100%"/>
            </el-form-item>
          </el-col>
          <el-col :span="12">
            <el-form-item label="最大值">
              <el-input-number v-model="form.maxValue" :precision="4" placeholder="可选" style="width: 100%"/>
            </el-form-item>
          </el-col>
        </el-row>
        <el-form-item label="地址规则">
          <el-input v-model="form.addressRule" placeholder="如 %MW100, 4x0001"/>
        </el-form-item>
        <el-form-item label="描述">
          <el-input v-model="form.description" :rows="2" placeholder="详细描述数据项含义、范围、报警阈值等"
                    type="textarea"/>
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
import {onMounted, reactive, ref} from 'vue'
import {ElMessage} from 'element-plus'
import {Search} from '@element-plus/icons-vue'
import DictSelect from '@/components/DictSelect.vue'
import {baseDevicesApi} from '@/api/baseDevices'
import {useDictionariesStore} from '@/stores/dictionaries'

const dictStore = useDictionariesStore()

const list = ref<any[]>([])
const loading = ref(false)
const dialogVisible = ref(false)
const editing = ref<any>(null)
const keyword = ref('')
const form = reactive({
  id: 0, itemCode: '', itemName: '', categoryCode: '', protocolCode: '',
  dataType: 'FLOAT', readWrite: 'R', unit: '', precision: 0,
  minValue: null as number | null, maxValue: null as number | null,
  addressRule: '', collectionCycle: 1000, description: '',
})

function getDataTypeTag(type: string): string | undefined {
  const map: Record<string, string> = {
    INT: 'info', INT16: 'info', INT32: 'info',
    FLOAT: 'success', DOUBLE: 'success',
    BOOL: 'warning', STRING: '',
  }
  return map[type] || undefined
}

function getCategoryName(code: string): string {
  if (!code) return '-'
  const cat = dictStore.categories.find((c: any) => c.categoryCode === code)
  return cat?.categoryName || code
}

function getProtocolName(code: string): string {
  if (!code) return '-'
  const proto = dictStore.protocols.find((p: any) => p.protocolCode === code)
  return proto?.protocolName || code
}

async function load() {
  loading.value = true
  try {
    const params: any = {page: 1, size: 9999}
    if (keyword.value) params.keyword = keyword.value
    const data = await baseDevicesApi.itemList(params)
    const raw: any = data
    const rawList = raw?.items ?? raw?.list ?? (Array.isArray(raw) ? raw : [])
    console.log('[DataItemDict] Raw API response:', rawList)
    if (rawList.length > 0) {
      console.log('[DataItemDict] First item keys:', Object.keys(rawList[0]))
      console.log('[DataItemDict] First item:', rawList[0])
    }
    // Normalize snake_case to camelCase for min/max values
    list.value = rawList.map((item: any) => ({
      ...item,
      minValue: item.minValue ?? item.min_value,
      maxValue: item.maxValue ?? item.max_value,
    }))
  } catch (e: any) {
    list.value = [];
    ElMessage.error(e?.message || '加载失败')
  } finally {
    loading.value = false
  }
}

function openDialog(row?: any) {
  editing.value = row || null
  Object.assign(form, row || {
    id: 0, itemCode: '', itemName: '', categoryCode: '', protocolCode: '',
    dataType: 'FLOAT', readWrite: 'R', unit: '', precision: 0,
    minValue: null, maxValue: null, addressRule: '', collectionCycle: 1000, description: '',
  })
  dialogVisible.value = true
}

async function submit() {
  try {
    const body: any = {
      itemCode: form.itemCode,
      itemName: form.itemName,
      categoryCode: form.categoryCode,
      protocolCode: form.protocolCode,
      dataType: form.dataType,
      readWrite: form.readWrite,
      unit: form.unit,
      precision: form.precision,
      minValue: form.minValue,
      maxValue: form.maxValue,
      addressRule: form.addressRule,
      collectionCycle: form.collectionCycle,
      description: form.description,
    }
    if (editing.value) await baseDevicesApi.itemUpdate(editing.value.id, body)
    else await baseDevicesApi.itemCreate(body)
    ElMessage.success('保存成功');
    dialogVisible.value = false;
    await load()
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

async function remove(row: any) {
  try {
    await baseDevicesApi.itemRemove(row.id);
    await load();
    ElMessage.success('已删除')
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

onMounted(async () => {
  await dictStore.loadAll()
  await load()
})
</script>