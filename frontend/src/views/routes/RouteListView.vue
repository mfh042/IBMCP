<template>
  <div class="page-card">
    <div class="page-header">
      <h3>工艺路线列表</h3>
      <el-button type="primary" @click="openDialog()">新增</el-button>
    </div>

    <div class="toolbar">
      <el-input v-model="keyword" clearable placeholder="搜索路线编码/名称" style="width: 200px" @clear="load"
                @keyup.enter="load"/>
      <el-select v-model="statusFilter" clearable placeholder="状态筛选" style="width: 120px; margin-left: 10px"
                 @change="load">
        <el-option :value="0" label="草稿"/>
        <el-option :value="1" label="已启用"/>
        <el-option :value="2" label="停用"/>
      </el-select>
      <el-button style="margin-left: 10px" type="primary" @click="load">查询</el-button>
    </div>

    <el-table v-loading="loading" :data="list" stripe @row-click="goDetail">
      <el-table-column label="ID" prop="id" width="70"/>
      <el-table-column label="路线编码" prop="routeCode" width="160"/>
      <el-table-column label="路线名称" min-width="200" prop="routeName"/>
      <el-table-column label="资源类型" prop="resourceType" width="120"/>
      <el-table-column label="版本" prop="version" width="80"/>
      <el-table-column label="状态" width="100">
        <template #default="{ row }">
          <el-tag :type="statusType(row.status)">{{ statusLabel(row.status) }}</el-tag>
        </template>
      </el-table-column>
      <el-table-column label="描述" min-width="180" prop="description" show-overflow-tooltip/>
      <el-table-column label="操作" width="200">
        <template #default="{ row }" @click.stop>
          <el-button link size="small" @click="goDetail(row)">详情</el-button>
          <el-button link size="small" @click="openDialog(row)">编辑</el-button>
          <el-popconfirm title="确认删除？" @confirm="remove(row)">
            <template #reference>
              <el-button link size="small" type="danger">删除</el-button>
            </template>
          </el-popconfirm>
        </template>
      </el-table-column>
    </el-table>

    <el-pagination
        v-model:current-page="pagination.page"
        v-model:page-size="pagination.size"
        :page-sizes="[10, 20, 50, 100]"
        :total="pagination.total"
        layout="total, sizes, prev, pager, next, jumper"
        style="margin-top: 16px; justify-content: flex-end"
        @current-change="load"
        @size-change="load"
    />

    <el-dialog v-model="dialogVisible" :title="editing ? '编辑' : '新增'" width="640px">
      <el-form :model="form" label-width="110px">
        <el-form-item label="路线编码">
          <el-input v-model="form.routeCode" :disabled="!!editing" placeholder="如：RT-ASSY-01"/>
        </el-form-item>
        <el-form-item label="路线名称">
          <el-input v-model="form.routeName" placeholder="如：总装线工艺路线1"/>
        </el-form-item>
        <el-form-item label="资源类型">
          <el-select v-model="form.resourceType" placeholder="请选择" style="width: 100%">
            <el-option label="装配线" value="ASSEMBLY"/>
            <el-option label="加工线" value="MACHINING"/>
            <el-option label="检测线" value="INSPECTION"/>
            <el-option label="包装线" value="PACKAGING"/>
            <el-option label="SVG图纸" value="SVG"/>
            <el-option label="其他" value="OTHER"/>
          </el-select>
        </el-form-item>
        <el-form-item label="资源URL">
          <el-input v-model="form.resourceUrl" placeholder="资源文件URL（可选）"/>
        </el-form-item>
        <el-form-item label="缩略图URL">
          <el-input v-model="form.thumbnailUrl" placeholder="缩略图URL（可选）"/>
        </el-form-item>
        <el-form-item label="版本">
          <el-input v-model="form.version" placeholder="如：v1.0"/>
        </el-form-item>
        <el-form-item label="状态">
          <el-radio-group v-model="form.status">
            <el-radio :value="0">草稿</el-radio>
            <el-radio :value="1">已启用</el-radio>
            <el-radio :value="2">停用</el-radio>
          </el-radio-group>
        </el-form-item>
        <el-form-item label="描述">
          <el-input v-model="form.description" :rows="3" placeholder="工艺路线描述" type="textarea"/>
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
import {useRouter} from 'vue-router'
import {ElMessage} from 'element-plus'
import {routesApi} from '@/api/routes'

const router = useRouter()
const list = ref<any[]>([])
const loading = ref(false)
const dialogVisible = ref(false)
const editing = ref<any>(null)
const keyword = ref('')
const statusFilter = ref<number | null>(null)

const pagination = reactive({
  page: 1,
  size: 20,
  total: 0,
})

const form = reactive({
  id: 0,
  routeCode: '',
  routeName: '',
  resourceType: 'ASSEMBLY',
  resourceUrl: '',
  thumbnailUrl: '',
  version: 'v1.0',
  status: 1,
  description: '',
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
    const params: any = {page: pagination.page, size: pagination.size}
    if (keyword.value) params.keyword = keyword.value
    if (statusFilter.value !== null) params.status = statusFilter.value
    const data = await routesApi.list(params)
    list.value = (data as any)?.list ?? (data as any)?.items ?? (Array.isArray(data) ? data : [])
    pagination.total = (data as any)?.total ?? list.value.length
  } catch (e: any) {
    list.value = []
    ElMessage.error(e?.message || '加载失败')
  } finally {
    loading.value = false
  }
}

function openDialog(row?: any) {
  editing.value = row || null
  Object.assign(form, row || {
    id: 0,
    routeCode: '',
    routeName: '',
    resourceType: 'ASSEMBLY',
    resourceUrl: '',
    thumbnailUrl: '',
    version: 'v1.0',
    status: 1,
    description: '',
  })
  dialogVisible.value = true
}

async function submit() {
  try {
    if (editing.value) {
      await routesApi.update(editing.value.id, form)
    } else {
      const {id, ...createData} = form
      console.log('提交创建数据:', createData)
      await routesApi.create(createData)
    }
    ElMessage.success('保存成功')
    dialogVisible.value = false
    await load()
  } catch (e: any) {
    console.error('提交失败:', e)
    ElMessage.error(e.message)
  }
}

async function remove(row: any) {
  try {
    await routesApi.remove(row.id)
    await load()
    ElMessage.success('已删除')
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

function goDetail(row: any) {
  router.push(`/routes/${row.routeCode}`)
}

onMounted(load)
</script>