<template>
  <div class="page-card">
    <div class="page-header">
      <h3>装备功能字典</h3>
      <div class="toolbar">
        <el-input v-model="keyword" clearable placeholder="搜索编码/名称" style="width: 200px" @clear="load"/>
        <el-button type="primary" @click="load">查询</el-button>
        <el-button type="primary" @click="openDialog()">新增</el-button>
      </div>
    </div>
    <el-table v-loading="loading" :data="list" stripe>
      <el-table-column label="ID" prop="id" width="70"/>
      <el-table-column label="功能编码" prop="functionCode" width="140"/>
      <el-table-column label="功能名称" min-width="140" prop="functionName"/>
      <el-table-column label="功能类型" prop="functionType" width="100">
        <template #default="{ row }">
          <el-tag :type="functionTypeTag(row.functionType)">{{ row.functionType || '-' }}</el-tag>
        </template>
      </el-table-column>
      <el-table-column label="类别编码" prop="categoryCode" width="100"/>
      <el-table-column label="状态" width="80">
        <template #default="{ row }">
          <el-tag :type="row.status === 1 ? 'success' : 'info'">{{ row.status === 1 ? '启用' : '停用' }}</el-tag>
        </template>
      </el-table-column>
      <el-table-column label="描述" min-width="200" prop="description" show-overflow-tooltip/>
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

    <el-dialog v-model="dialogVisible" :title="editing ? '编辑功能' : '新增功能'" width="640px">
      <el-form :model="form" label-width="110px">
        <el-form-item label="功能编码">
          <el-input v-model="form.functionCode" :disabled="!!editing" placeholder="如：TEMP_ACQ"/>
        </el-form-item>
        <el-form-item label="功能名称">
          <el-input v-model="form.functionName" placeholder="如：温度采集"/>
        </el-form-item>
        <el-form-item label="功能类型">
          <el-select v-model="form.functionType" placeholder="请选择" style="width: 100%">
            <el-option label="采集" value="采集"/>
            <el-option label="控制" value="控制"/>
            <el-option label="报警" value="报警"/>
            <el-option label="追溯" value="追溯"/>
          </el-select>
        </el-form-item>
        <el-form-item label="类别编码">
          <el-input v-model="form.categoryCode" placeholder="如：CNC"/>
        </el-form-item>
        <el-form-item label="状态">
          <el-radio-group v-model="form.status">
            <el-radio :value="1">启用</el-radio>
            <el-radio :value="0">停用</el-radio>
          </el-radio-group>
        </el-form-item>
        <el-form-item label="描述">
          <el-input v-model="form.description" :rows="3" placeholder="功能描述" type="textarea"/>
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
const page = ref(1)
const size = ref(20)
const total = ref(0)

const form = reactive({
  id: 0,
  functionCode: '',
  functionName: '',
  functionType: '采集',
  categoryCode: '',
  status: 1,
  description: '',
})

function functionTypeTag(type: string): 'primary' | 'success' | 'warning' | 'danger' | 'info' {
  const map: Record<string, 'primary' | 'success' | 'warning' | 'danger' | 'info'> = {
    '采集': 'primary',
    '控制': 'success',
    '报警': 'danger',
    '追溯': 'warning',
  }
  return map[type] || 'info'
}

async function load() {
  loading.value = true
  try {
    const data = await equipmentsApi.functionList({
      page: page.value,
      size: size.value,
      keyword: keyword.value,
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
    functionCode: '',
    functionName: '',
    functionType: '采集',
    categoryCode: '',
    status: 1,
    description: '',
  })
  dialogVisible.value = true
}

async function submit() {
  try {
    const {id, ...body} = form
    if (editing.value) {
      await equipmentsApi.functionUpdate(editing.value.id, body)
    } else {
      await equipmentsApi.functionCreate(body)
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
    await equipmentsApi.functionRemove(row.id)
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