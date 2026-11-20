<template>
  <div class="page-card">
    <div class="page-header"><h3>API 服务器设置</h3></div>

    <el-alert
        :closable="false"
        show-icon
        style="margin-bottom: 16px"
        title="Tauri 桌面壳场景下可配置后端服务器地址。Web 形态无需配置，Nginx 自动反代。"
        type="info"
    />

    <el-form :model="form" label-width="140px" style="max-width: 560px">
      <el-form-item label="后端 HTTP 地址">
        <el-input v-model="form.apiBase" placeholder="http://127.0.0.1:8080"/>
      </el-form-item>
      <el-form-item label="WebSocket 地址">
        <el-input v-model="form.wsUrl" placeholder="ws://127.0.0.1:8080/ws/push"/>
      </el-form-item>
      <el-form-item label="当前运行形态">
        <el-tag type="success">{{ runtimeLabel }}</el-tag>
      </el-form-item>
      <el-form-item label="当前 HTTP 基址">
        <code style="font-size: 12px">{{ currentApiBase }}</code>
      </el-form-item>
      <el-form-item>
        <el-button type="primary" @click="save">保存并重连</el-button>
        <el-button @click="reset">自动检测</el-button>
      </el-form-item>
    </el-form>
  </div>
</template>

<script lang="ts" setup>
import {computed, reactive} from 'vue'
import {ElMessage} from 'element-plus'
import {EnvDetector} from '@/comm/env'
import {CommConfig} from '@/comm/config'
import {wsService} from '@/comm/ws'
import {httpService} from '@/comm/http'

const form = reactive({
  apiBase: localStorage.getItem('ibmcp.apiAddr') || 'http://127.0.0.1:8080',
  wsUrl: localStorage.getItem('ibmcp.wsAddr') || 'ws://127.0.0.1:8080/ws/push',
})

const runtimeLabel = computed(() => EnvDetector.runtimeLabel)
const currentApiBase = computed(() => CommConfig.apiBase || '(相对路径 /api)')

function reset() {
  CommConfig.setUserApiAddr(null)
  CommConfig.setUserWsAddr(null)
  form.apiBase = 'http://127.0.0.1:8080'
  form.wsUrl = 'ws://127.0.0.1:8080/ws/push'
  ElMessage.success('已重置为自动检测')
}

function save() {
  CommConfig.setUserApiAddr(form.apiBase)
  CommConfig.setUserWsAddr(form.wsUrl)
  httpService.defaults.baseURL = CommConfig.apiBase
  wsService.reconnect()
  ElMessage.success('保存成功，已重连')
}
</script>