<template>
  <div class="login-page">
    <div class="login-box">
      <div class="login-header">
        <h1>IBMCP</h1>
        <p>工业黑盒集控平台 · 登录</p>
      </div>
      <el-form ref="formRef" :model="form" :rules="rules" label-width="0">
        <el-form-item prop="username">
          <el-input v-model="form.username" :prefix-icon="User" placeholder="用户名" size="large"/>
        </el-form-item>
        <el-form-item prop="password">
          <el-input v-model="form.password" :prefix-icon="Lock" placeholder="密码" show-password size="large"
                    type="password" @keyup.enter="handleLogin"/>
        </el-form-item>
        <el-form-item>
          <el-checkbox v-model="remember">记住我</el-checkbox>
        </el-form-item>
        <el-form-item>
          <el-button :loading="loading" size="large" style="width: 100%" type="primary" @click="handleLogin">
            登录
          </el-button>
        </el-form-item>
      </el-form>
    </div>
  </div>
</template>

<script lang="ts" setup>
import {onMounted, reactive, ref} from 'vue'
import {useRouter} from 'vue-router'
import {ElMessage, type FormInstance, type FormRules} from 'element-plus'
import {Lock, User} from '@element-plus/icons-vue'
import {useAuthStore} from '@/stores/auth'
import {storage} from '@/utils/storage'

const router = useRouter()
const auth = useAuthStore()
const formRef = ref<FormInstance>()
const loading = ref(false)
const remember = ref(true)

const form = reactive({
  username: '',
  password: '',
})

const rules: FormRules = {
  username: [{required: true, message: '请输入用户名', trigger: 'blur'}],
  password: [{required: true, message: '请输入密码', trigger: 'blur'}],
}

onMounted(() => {
  const saved = storage.getRememberedUsername()
  if (saved) form.username = saved
})

async function handleLogin() {
  if (!formRef.value) return
  await formRef.value.validate(async (valid) => {
    if (!valid) return
    loading.value = true
    try {
      await auth.login(form.username, form.password)
      if (remember.value) storage.setRememberedUsername(form.username)
      ElMessage.success('登录成功')
      router.push('/monitoring/dashboard')
    } catch (e: any) {
      ElMessage.error(e.message || '登录失败')
    } finally {
      loading.value = false
    }
  })
}
</script>

<style lang="scss" scoped>
.login-page {
  height: 100vh;
  width: 100vw;
  display: flex;
  align-items: center;
  justify-content: center;
  background: linear-gradient(135deg, #1e3a8a, #3b82f6);
}

.login-box {
  width: 380px;
  padding: 40px;
  background: #fff;
  border-radius: 12px;
  box-shadow: 0 20px 60px rgba(0, 0, 0, 0.2);
}

.login-header {
  text-align: center;
  margin-bottom: 32px;

  h1 {
    font-size: 32px;
    margin: 0 0 8px;
    color: #1e3a8a;
    letter-spacing: 4px;
  }

  p {
    margin: 0;
    color: #64748b;
    font-size: 14px;
  }
}
</style>