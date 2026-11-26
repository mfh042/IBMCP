<template>
  <div class="nr-editor">
    <div class="nr-toolbar">
      <div class="nr-title">
        <el-icon :size="18"><Connection/></el-icon>
        <span class="nr-name">Node-RED 流编辑器</span>
        <el-tag :type="statusTagType" effect="dark" size="small" class="nr-status">
          {{ statusLabel }}
        </el-tag>
      </div>
      <div class="nr-actions" v-if="isAdmin">
        <el-button :icon="VideoPlay" type="success" :loading="busy"
                   :disabled="busy || containerRunning" @click="start">启动</el-button>
        <el-button :icon="VideoPause" type="danger" :loading="busy"
                   :disabled="busy || !containerRunning" @click="stop">停止</el-button>
        <el-button :icon="RefreshRight" type="primary" :loading="busy"
                   :disabled="busy" @click="restart">重启</el-button>
      </div>
    </div>

    <div class="nr-body">
      <iframe v-if="editorReady" :key="iframeKey" src="/nodered/" class="nr-frame"/>
      <div v-else class="nr-placeholder">
        <el-icon :size="56" class="nr-ph-icon">
          <component :is="containerRunning ? Loading : WarningFilled"/>
        </el-icon>
        <p class="nr-ph-title">Node-RED 编辑器未就绪</p>
        <p class="nr-ph-desc">
          {{ containerRunning ? '编辑器正在启动，请稍候，状态将自动刷新…' : 'Node-RED 容器未运行' }}
        </p>
        <el-button v-if="isAdmin" type="primary" :icon="VideoPlay" :loading="busy" @click="start">
          启动 Node-RED
        </el-button>
        <p v-else class="nr-ph-hint">请联系管理员启动 Node-RED 服务</p>
      </div>
    </div>
  </div>
</template>

<script lang="ts" setup>
import {computed, onMounted, onUnmounted, ref} from 'vue'
import {ElMessage} from 'element-plus'
import {Connection, Loading, RefreshRight, VideoPause, VideoPlay, WarningFilled} from '@element-plus/icons-vue'
import {useAuthStore} from '@/stores/auth'
import {nodeRedApi, type NodeRedStatus} from '@/api/nodeRed'

const auth = useAuthStore()
const isAdmin = computed(() => auth.hasRole('admin'))

const status = ref<NodeRedStatus | null>(null)
const busy = ref(false)
const iframeKey = ref(0)
const prevReady = ref(false)
let timer: number | null = null

const containerRunning = computed(() => status.value?.container === 'running')
const editorReady = computed(() => !!status.value?.editorReachable)

const statusTagType = computed<'success' | 'warning' | 'danger' | 'info'>(() => {
    if (!status.value) return 'info'
    if (status.value.container === 'running') return editorReady.value ? 'success' : 'warning'
    return 'danger'
})

const statusLabel = computed(() => {
    if (!status.value) return '检查中…'
    const c = status.value.container
    if (c === 'running') return editorReady.value ? '运行中 · 编辑器可用' : '运行中 · 编辑器启动中'
    if (c === 'exited' || c === 'stopped') return '已停止'
    return c || '未知'
})

async function refresh() {
    try {
        const s = await nodeRedApi.getStatus()
        status.value = s
        if (s.editorReachable && !prevReady.value) iframeKey.value++
        prevReady.value = s.editorReachable
    } catch (e) {
        // 轮询失败静默，下一轮再试
    }
}

async function runAction(fn: () => Promise<NodeRedStatus>, label: string) {
    busy.value = true
    try {
        const s = await fn()
        status.value = s
        if (s.editorReachable && !prevReady.value) iframeKey.value++
        prevReady.value = s.editorReachable
        ElMessage.success(`${label}成功`)
        // 启动/重启后编辑器需要几秒就绪，主动轮询直到可用
        for (let i = 0; i < 12; i++) {
            await new Promise((r) => setTimeout(r, 1000))
            await refresh()
            if (editorReady.value) break
        }
    } catch (e: any) {
        ElMessage.error(e?.message || `${label}失败`)
    } finally {
        busy.value = false
    }
}

const start = () => runAction(nodeRedApi.start, '启动')
const stop = () => runAction(nodeRedApi.stop, '停止')
const restart = () => runAction(nodeRedApi.restart, '重启')

onMounted(() => {
    refresh()
    timer = window.setInterval(refresh, 5000)
})

onUnmounted(() => {
    if (timer) window.clearInterval(timer)
})
</script>

<style lang="scss" scoped>
.nr-editor {
    height: 100%;
    display: flex;
    flex-direction: column;
    background: #fff;
    border-radius: 6px;
    overflow: hidden;
}

.nr-toolbar {
    flex-shrink: 0;
    height: 48px;
    padding: 0 16px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    border-bottom: 1px solid #e2e8f0;
    background: #f8fafc;
}

.nr-title {
    display: flex;
    align-items: center;
    gap: 8px;
}

.nr-name {
    font-size: 14px;
    font-weight: 600;
    color: #1f2937;
}

.nr-status {
    margin-left: 4px;
}

.nr-actions {
    display: flex;
    gap: 8px;
}

.nr-body {
    flex: 1;
    min-height: 0;
    position: relative;
}

.nr-frame {
    width: 100%;
    height: 100%;
    border: none;
    display: block;
}

.nr-placeholder {
    position: absolute;
    inset: 0;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 8px;
    color: #64748b;
    background: #f8fafc;
}

.nr-ph-icon {
    color: #94a3b8;
    margin-bottom: 4px;
}

.nr-ph-title {
    font-size: 16px;
    font-weight: 600;
    color: #334155;
    margin: 0;
}

.nr-ph-desc {
    font-size: 13px;
    margin: 0 0 8px 0;
}

.nr-ph-hint {
    font-size: 12px;
    color: #94a3b8;
}
</style>
