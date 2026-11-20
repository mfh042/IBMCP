<template>
  <div class="page-card">
    <div class="page-header">
      <h3>设备类别字典</h3>
      <el-button type="primary" @click="openDialog()">新增</el-button>
    </div>
    <el-table v-loading="loading" :data="list" stripe>
      <el-table-column label="ID" prop="id" width="70"/>
      <el-table-column label="编码" prop="categoryCode" width="160"/>
      <el-table-column label="类别名称" min-width="180" prop="categoryName"/>
      <el-table-column label="描述" min-width="200" prop="description"/>
      <el-table-column label="操作" width="180">
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

    <el-dialog v-model="dialogVisible" :title="editing ? '编辑' : '新增'" width="460px">
      <el-form :model="form" label-width="100px">
        <el-form-item label="编码">
          <el-input v-model="form.categoryCode"/>
        </el-form-item>
        <el-form-item label="名称">
          <el-input v-model="form.categoryName"/>
        </el-form-item>
        <el-form-item label="描述">
          <el-input v-model="form.description" type="textarea"/>
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
import {dictsApi} from '@/api/dicts'

const list = ref<any[]>([])
const loading = ref(false)
const dialogVisible = ref(false)
const editing = ref<any>(null)
const form = reactive({id: 0, categoryCode: '', categoryName: '', description: ''})

async function load() {
  loading.value = true
  try {
    const data = await dictsApi.categories.list({page: 1, size: 9999})
    list.value = (data as any)?.list ?? (Array.isArray(data) ? data : [])
  } finally {
    loading.value = false
  }
}

function openDialog(row?: any) {
  editing.value = row || null
  Object.assign(form, row || {id: 0, categoryCode: '', categoryName: '', description: ''})
  dialogVisible.value = true
}

async function submit() {
  try {
    if (editing.value) {
      await dictsApi.categories.update(editing.value.id, form)
      ElMessage.success('更新成功')
    } else {
      await dictsApi.categories.create(form)
      ElMessage.success('新增成功')
    }
    dialogVisible.value = false
    await load()
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

async function remove(row: any) {
  try {
    await dictsApi.categories.remove(row.id);
    await load();
    ElMessage.success('已删除')
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

onMounted(load)
</script>