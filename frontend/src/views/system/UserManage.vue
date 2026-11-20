<template>
  <div class="page-card">
    <div class="page-header">
      <h3>用户管理</h3>
      <el-button type="primary" @click="openDialog()">新增用户</el-button>
    </div>

    <div class="toolbar">
      <el-input v-model="keyword" clearable placeholder="搜索用户名/显示名" style="width: 240px" @keyup.enter="load"/>
      <el-button type="primary" @click="load">搜索</el-button>
    </div>

    <el-table v-loading="loading" :data="list" stripe>
      <el-table-column label="ID" prop="id" width="70"/>
      <el-table-column label="用户名" prop="username" width="160"/>
      <el-table-column label="显示名" prop="displayName" width="160"/>
      <el-table-column label="角色" min-width="200">
        <template #default="{ row }">
          <el-tag v-for="r in (row.roles || [])" :key="r" size="small" style="margin-right:4px">{{ r }}</el-tag>
        </template>
      </el-table-column>
      <el-table-column label="启用" width="100">
        <template #default="{ row }">
          <el-switch v-model="row.enabled" @change="(v: boolean) => toggleEnabled(row, v)"/>
        </template>
      </el-table-column>
      <el-table-column label="操作" width="280">
        <template #default="{ row }">
          <el-button link size="small" @click="openDialog(row)">编辑</el-button>
          <el-button link size="small" @click="openPwdDialog(row)">修改密码</el-button>
          <el-button link size="small" @click="openRolesDialog(row)">分配角色</el-button>
          <el-popconfirm title="确认删除？" @confirm="remove(row)">
            <template #reference>
              <el-button link size="small" type="danger">删除</el-button>
            </template>
          </el-popconfirm>
        </template>
      </el-table-column>
    </el-table>

    <el-dialog v-model="dialogVisible" :title="editing ? '编辑用户' : '新增用户'" width="460px">
      <el-form :model="form" label-width="90px">
        <el-form-item label="用户名">
          <el-input v-model="form.username"/>
        </el-form-item>
        <el-form-item label="显示名">
          <el-input v-model="form.displayName"/>
        </el-form-item>
        <el-form-item v-if="!editing" label="密码">
          <el-input v-model="form.password" show-password type="password"/>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="dialogVisible = false">取消</el-button>
        <el-button type="primary" @click="submitUser">确定</el-button>
      </template>
    </el-dialog>

    <el-dialog v-model="pwdDialogVisible" title="修改密码" width="420px">
      <el-form :model="pwdForm" label-width="90px">
        <el-form-item label="原密码">
          <el-input v-model="pwdForm.oldPassword" show-password type="password"/>
        </el-form-item>
        <el-form-item label="新密码">
          <el-input v-model="pwdForm.newPassword" show-password type="password"/>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="pwdDialogVisible = false">取消</el-button>
        <el-button type="primary" @click="submitPwd">确定</el-button>
      </template>
    </el-dialog>

    <el-dialog v-model="rolesDialogVisible" title="分配角色" width="460px">
      <el-checkbox-group v-model="selectedRoleIds">
        <el-checkbox v-for="r in allRoles" :key="r.id" :value="r.id">{{ r.name }} - {{ r.description }}</el-checkbox>
      </el-checkbox-group>
      <template #footer>
        <el-button @click="rolesDialogVisible = false">取消</el-button>
        <el-button type="primary" @click="submitRoles">确定</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script lang="ts" setup>
import {onMounted, reactive, ref} from 'vue'
import {ElMessage} from 'element-plus'
import {authApi} from '@/api/auth'

const list = ref<any[]>([])
const allRoles = ref<any[]>([])
const keyword = ref('')
const loading = ref(false)
const dialogVisible = ref(false)
const pwdDialogVisible = ref(false)
const rolesDialogVisible = ref(false)
const editing = ref<any>(null)
const editingUser = ref<any>(null)
const form = reactive({id: 0, username: '', displayName: '', password: ''})
const pwdForm = reactive({oldPassword: '', newPassword: ''})
const selectedRoleIds = ref<number[]>([])

async function load() {
  loading.value = true
  try {
    const data = await authApi.listUsers({keyword: keyword.value || undefined})
    list.value = (data as any)?.items ?? (Array.isArray(data) ? data : [])
  } catch (e: any) {
    list.value = []
    ElMessage.error(e?.message || '加载失败')
  } finally {
    loading.value = false
  }
}

async function loadRoles() {
  try {
    const data = await authApi.listRoles()
    allRoles.value = (data as any)?.list ?? (Array.isArray(data) ? data : [])
  } catch {
  }
}

function openDialog(row?: any) {
  editing.value = row || null
  Object.assign(form, {
    id: row?.id ?? 0,
    username: row?.username ?? '',
    displayName: row?.displayName ?? '',
    password: ''
  })
  dialogVisible.value = true
}

async function submitUser() {
  try {
    if (editing.value) await authApi.updateUser(editing.value.id, form)
    else await authApi.createUser(form)
    ElMessage.success('保存成功');
    dialogVisible.value = false;
    await load()
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

function openPwdDialog(row: any) {
  editingUser.value = row
  pwdForm.oldPassword = '';
  pwdForm.newPassword = ''
  pwdDialogVisible.value = true
}

async function submitPwd() {
  try {
    await authApi.updatePassword(editingUser.value.id, pwdForm)
    ElMessage.success('密码修改成功');
    pwdDialogVisible.value = false
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

function openRolesDialog(row: any) {
  editingUser.value = row
  selectedRoleIds.value = row.roleIds || row.roles?.map((r: any) => (typeof r === 'object' ? r.id : r)) || []
  rolesDialogVisible.value = true
}

async function submitRoles() {
  try {
    await authApi.setUserRoles(editingUser.value.id, selectedRoleIds.value.map((id) => String(id)))
    ElMessage.success('角色已更新');
    rolesDialogVisible.value = false;
    await load()
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

async function toggleEnabled(row: any, v: boolean) {
  try {
    await authApi.setUserEnabled(row.id, v);
    ElMessage.success('已更新')
  } catch (e: any) {
    row.enabled = !v;
    ElMessage.error(e.message)
  }
}

async function remove(row: any) {
  try {
    await authApi.deleteUser(row.id);
    await load();
    ElMessage.success('已删除')
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

onMounted(async () => {
  await load();
  await loadRoles()
})
</script>