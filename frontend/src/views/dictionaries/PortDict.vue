<template>
  <div class="page-card">
    <div class="page-header">
      <h3>端口字典</h3>
      <el-button type="primary" @click="openDialog()">新增</el-button>
    </div>
    <el-table v-loading="loading" :data="list" stripe>
      <el-table-column label="ID" prop="id" width="70"/>
      <el-table-column label="编码" prop="portCode" width="140"/>
      <el-table-column label="名称" prop="portName" width="160"/>
      <el-table-column label="类型" prop="portType" width="120"/>
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
          <el-input v-model="form.portCode"/>
        </el-form-item>
        <el-form-item label="名称">
          <el-input v-model="form.portName"/>
        </el-form-item>
        <el-form-item label="类型">
          <el-select v-model="form.portType" style="width: 100%">
            <el-option label="串口" value="serial"/>
            <el-option label="网口" value="ethernet"/>
            <el-option label="USB" value="usb"/>
          </el-select>
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
const form = reactive({id: 0, portCode: '', portName: '', portType: 'serial', description: ''})

async function load() {
  loading.value = true
  try {
    const data = await dictsApi.ports.list({page: 1, size: 9999})
    list.value = (data as any)?.list ?? (Array.isArray(data) ? data : [])
  } catch (e: any) {
    list.value = [];
    ElMessage.error(e?.message || '加载失败')
  } finally {
    loading.value = false
  }
}

function openDialog(row?: any) {
  editing.value = row || null
  Object.assign(form, row || {id: 0, portCode: '', portName: '', portType: 'serial', description: ''})
  dialogVisible.value = true
}

async function submit() {
  try {
    if (editing.value) await dictsApi.ports.update(editing.value.id, form)
    else await dictsApi.ports.create(form)
    ElMessage.success('保存成功');
    dialogVisible.value = false;
    await load()
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

async function remove(row: any) {
  try {
    await dictsApi.ports.remove(row.id);
    await load();
    ElMessage.success('已删除')
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

onMounted(load)
</script>