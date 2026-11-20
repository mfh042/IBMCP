<template>
  <div class="page-card">
    <div class="page-header">
      <h3>装备主数据</h3>
      <div class="toolbar">
        <el-input v-model="keyword" clearable placeholder="搜索编码/名称" style="width: 200px" @clear="load"/>
        <el-input v-model="customerCode" clearable placeholder="客户编码" style="width: 160px" @clear="load"/>
        <el-button type="primary" @click="load">查询</el-button>
        <el-button type="primary" @click="openDialog()">新增</el-button>
      </div>
    </div>
    <el-table v-loading="loading" :data="list" stripe>
      <el-table-column label="ID" prop="id" width="70"/>
      <el-table-column label="装备编码" prop="equipmentCode" width="140"/>
      <el-table-column label="装备名称" min-width="160" prop="equipmentName"/>
      <el-table-column label="客户编码" prop="customerCode" width="130"/>
      <el-table-column label="客户名称" prop="customerName" width="130"/>
      <el-table-column label="类别编码" prop="categoryCode" width="100"/>
      <el-table-column label="品牌编码" prop="brandCode" width="110"/>
      <el-table-column label="型号编码" prop="modelCode" width="140"/>
      <el-table-column label="安装位置" prop="location" width="140"/>
      <el-table-column label="描述" min-width="180" prop="description" show-overflow-tooltip/>
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
    <div class="pagination-bar">
      <el-pagination
          v-model:current-page="page"
          v-model:page-size="size"
          :page-sizes="[10, 20, 50, 100]"
          :total="total"
          layout="total, sizes, prev, pager, next, jumper"
          @size-change="load"
          @current-change="load"
      />
    </div>

    <el-dialog v-model="dialogVisible" :title="editing ? '编辑装备' : '新增装备'" width="640px">
      <el-form :model="form" label-width="110px">
        <el-form-item label="装备编码">
          <el-input v-model="form.equipmentCode" :disabled="!!editing" placeholder="如：EQ-001"/>
        </el-form-item>
        <el-form-item label="装备名称">
          <el-input v-model="form.equipmentName" placeholder="如：1号CNC加工中心"/>
        </el-form-item>
        <el-form-item label="客户编码">
          <el-input v-model="form.customerCode" placeholder="如：CUST-ACME"/>
        </el-form-item>
        <el-form-item label="客户名称">
          <el-input v-model="form.customerName" placeholder="如：ACME汽车"/>
        </el-form-item>
        <el-form-item label="类别编码">
          <el-input v-model="form.categoryCode" placeholder="如：CNC"/>
        </el-form-item>
        <el-form-item label="品牌编码">
          <el-input v-model="form.brandCode" placeholder="如：MAZAK"/>
        </el-form-item>
        <el-form-item label="型号编码">
          <el-input v-model="form.modelCode" placeholder="如：QUICK TURN 250"/>
        </el-form-item>
        <el-form-item label="安装位置">
          <el-input v-model="form.location" placeholder="如：A区-01工位"/>
        </el-form-item>
        <el-form-item label="描述">
          <el-input v-model="form.description" :rows="3" placeholder="设备描述" type="textarea"/>
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
import {equipmentsApi} from '@/api/equipments'

const list = ref<any[]>([])
const loading = ref(false)
const dialogVisible = ref(false)
const editing = ref<any>(null)
const keyword = ref('')
const customerCode = ref('')
const page = ref(1)
const size = ref(20)
const total = ref(0)

const form = reactive({
  id: 0,
  equipmentCode: '',
  equipmentName: '',
  customerCode: '',
  customerName: '',
  categoryCode: '',
  brandCode: '',
  modelCode: '',
  location: '',
  description: '',
})

async function load() {
  loading.value = true
  try {
    const data = await equipmentsApi.list({
      page: page.value,
      size: size.value,
      keyword: keyword.value,
      customerCode: customerCode.value,
    })
    list.value = (data as any)?.items ?? []
    total.value = (data as any)?.total ?? 0
  } catch (e: any) {
    list.value = []
    total.value = 0
    ElMessage.error(e?.message || '加载失败')
  } finally {
    loading.value = false
  }
}

function openDialog(row?: any) {
  editing.value = row || null
  Object.assign(form, row || {
    id: 0,
    equipmentCode: '',
    equipmentName: '',
    customerCode: '',
    customerName: '',
    categoryCode: '',
    brandCode: '',
    modelCode: '',
    location: '',
    description: '',
  })
  dialogVisible.value = true
}

async function submit() {
  try {
    const {id, ...body} = form
    if (editing.value) {
      await equipmentsApi.update(editing.value.id, body)
    } else {
      await equipmentsApi.create(body)
    }
    ElMessage.success('保存成功')
    dialogVisible.value = false
    await load()
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

async function remove(row: any) {
  try {
    await equipmentsApi.remove(row.id)
    await load()
    ElMessage.success('已删除')
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

onMounted(load)
</script>

<style lang="scss" scoped>
.toolbar {
  display: flex;
  gap: 12px;
  align-items: center;
}

.pagination-bar {
  margin-top: 16px;
  display: flex;
  justify-content: flex-end;
}
</style>