<template>
  <el-container class="main-layout">
    <el-aside class="aside" width="220px">
      <div class="logo">
        <img src="/logo.jpg" alt="IBMCP" class="logo-img"/>
        <div class="logo-text-group">
          <span class="logo-text">IBMCP</span>
          <span class="logo-sub">工业黑盒集控平台</span>
        </div>
      </div>
      <el-menu
          :default-active="activeMenu"
          active-text-color="#ffffff"
          background-color="#1f2937"
          class="side-menu"
          router
          text-color="#cbd5e1"
      >
        <el-sub-menu v-if="hasRole('admin')" index="system">
          <template #title>
            <el-icon>
              <User/>
            </el-icon>
            <span>系统管理</span>
          </template>
          <el-menu-item index="/system/users">用户管理</el-menu-item>
          <el-menu-item index="/system/roles">角色管理</el-menu-item>
          <el-menu-item v-if="isTauri" index="/system/api-settings">API 设置</el-menu-item>
        </el-sub-menu>

        <el-sub-menu v-if="hasAny(['admin','engineer'])" index="base">
          <template #title>
            <el-icon>
              <DataAnalysis/>
            </el-icon>
            <span>基础数据</span>
          </template>
          <el-sub-menu index="dict-sub">
            <template #title>静态字典</template>
            <el-menu-item index="/dictionaries/categories">设备类别</el-menu-item>
            <el-menu-item index="/dictionaries/brands">品牌型号</el-menu-item>
            <el-menu-item index="/dictionaries/ports">端口</el-menu-item>
            <el-menu-item index="/dictionaries/protocols">协议</el-menu-item>
            <el-menu-item index="/dictionaries/data-items">开放数据项</el-menu-item>
            <el-menu-item index="/dictionaries/base-devices">设备主数据</el-menu-item>
            <el-menu-item index="/dictionaries/equipment-functions">装备功能</el-menu-item>
          </el-sub-menu>
          <el-menu-item index="/routes">工艺路线</el-menu-item>
          <el-menu-item index="/equipments">装备主数据</el-menu-item>
        </el-sub-menu>

        <el-sub-menu v-if="hasAny(['admin','engineer'])" index="config">
          <template #title>
            <el-icon>
              <Tools/>
            </el-icon>
            <span>运行配置</span>
          </template>
          <el-menu-item index="/equipment-config">装备运行实例</el-menu-item>
          <el-menu-item index="/equipment-config/wizard">配置向导</el-menu-item>
          <el-menu-item index="/node-red/editor">Node-RED 流编辑器</el-menu-item>
        </el-sub-menu>

        <el-sub-menu v-if="hasAny(['admin','engineer','operator'])" index="devices">
          <template #title>
            <el-icon>
              <Cpu/>
            </el-icon>
            <span>设备管理</span>
          </template>
          <el-menu-item index="/devices">设备列表</el-menu-item>
          <el-menu-item index="/alarms">活动报警</el-menu-item>
          <el-menu-item index="/alarms/history">报警历史</el-menu-item>
        </el-sub-menu>

        <el-sub-menu v-if="hasAny(['admin','engineer','operator','viewer'])" index="monitoring">
          <template #title>
            <el-icon>
              <Monitor/>
            </el-icon>
            <span>实时监控</span>
          </template>
          <el-menu-item index="/monitoring/dashboard">数据看板</el-menu-item>
          <el-menu-item index="/monitoring/trend">历史趋势</el-menu-item>
        </el-sub-menu>
      </el-menu>
    </el-aside>

    <el-container class="right-container">
      <el-header class="header">
        <div class="header-left">
          <el-tag :type="commStore.statusTag.type" effect="dark" size="small">
            {{ commStore.statusTag.label }} · {{ commStore.runtimeLabel }}
          </el-tag>
          <span v-if="commStore.httpLatency" class="latency">{{ commStore.httpLatency }}ms</span>
        </div>

        <div class="header-right">
          <el-badge :hidden="alarmsStore.activeCount === 0" :value="alarmsStore.activeCount" class="alarm-badge">
            <el-button :icon="Bell" link @click="goAlarms">活动报警</el-button>
          </el-badge>
          <el-dropdown>
            <el-button link>
              <el-icon>
                <UserFilled/>
              </el-icon>
              {{ auth.userInfo?.displayName || auth.userInfo?.username || '未登录' }}
            </el-button>
            <template #dropdown>
              <el-dropdown-menu>
                <el-dropdown-item @click="handleLogout">
                  <el-icon>
                    <SwitchButton/>
                  </el-icon>
                  退出登录
                </el-dropdown-item>
              </el-dropdown-menu>
            </template>
          </el-dropdown>
        </div>
      </el-header>

      <div class="alarm-banner-wrap">
        <AlarmBanner/>
      </div>

      <el-main class="main-content">
        <router-view v-slot="{ Component }">
          <transition mode="out-in" name="fade">
            <component :is="Component"/>
          </transition>
        </router-view>
      </el-main>
    </el-container>
  </el-container>
</template>

<script lang="ts" setup>
import {computed, onMounted} from 'vue'
import {useRoute, useRouter} from 'vue-router'
import {ElMessageBox} from 'element-plus'
import {Bell, Cpu, DataAnalysis, Monitor, SwitchButton, Tools, User, UserFilled} from '@element-plus/icons-vue'
import {useAuthStore} from '@/stores/auth'
import {useCommStore} from '@/stores/comm'
import {useAlarmsStore} from '@/stores/alarms'
import {EnvDetector} from '@/comm/env'
import AlarmBanner from '@/components/AlarmBanner.vue'

const route = useRoute()
const router = useRouter()
const auth = useAuthStore()
const commStore = useCommStore()
const alarmsStore = useAlarmsStore()

const isTauri = EnvDetector.isTauri
const activeMenu = computed(() => route.path)

function hasRole(role: string) {
  return auth.hasRole(role)
}

function hasAny(roles: string[]) {
  return roles.some((r) => auth.hasRole(r))
}

function goAlarms() {
  router.push('/alarms')
}

async function handleLogout() {
  await ElMessageBox.confirm('确定要退出登录吗？', '提示', {type: 'warning'})
  auth.logout()
  router.push('/login')
}

onMounted(() => {
  alarmsStore.loadActive()
  alarmsStore.subscribeWs()
  wsStore.subscribeDevice()
  wsStore.subscribeControl()
})
</script>

<script lang="ts">
import {useWsStore} from '@/stores/ws'

const wsStore = useWsStore()
export default {name: 'MainLayout'}
</script>

<style lang="scss" scoped>
.main-layout {
  display: flex;
  flex-direction: row;
  height: 100vh;
  width: 100vw;
  overflow: hidden;
}

.aside {
  flex-shrink: 0;
  background-color: #1f2937;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.logo {
  height: 60px;
  flex-shrink: 0;
  display: flex;
  flex-direction: row;
  justify-content: center;
  align-items: center;
  gap: 10px;
  border-bottom: 1px solid #374151;

  .logo-img {
    width: 36px;
    height: 36px;
    object-fit: cover;
    border-radius: 6px;
    flex-shrink: 0;
  }

  .logo-text-group {
    display: flex;
    flex-direction: column;
    justify-content: center;
  }

  .logo-text {
    font-size: 20px;
    font-weight: 700;
    color: #fff;
    letter-spacing: 2px;
    line-height: 1.2;
  }

  .logo-sub {
    font-size: 11px;
    color: #94a3b8;
    line-height: 1.2;
  }
}

.side-menu {
  border-right: none;
  flex: 1;
  overflow-y: auto;
}

.right-container {
  display: flex;
  flex-direction: column;
  flex: 1;
  min-width: 0;
  min-height: 0;
}

.header {
  flex-shrink: 0;
  display: flex;
  align-items: center;
  justify-content: space-between;
  border-bottom: 1px solid #e2e8f0;
  background-color: #fff;
  padding: 0 16px;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 8px;
}

.header-right {
  display: flex;
  align-items: center;
  gap: 16px;
}

.latency {
  font-size: 12px;
  color: #64748b;
}

.alarm-badge {
  :deep(.el-badge__content) {
    top: 8px;
    right: 20px;
  }
}

.alarm-banner-wrap {
  flex-shrink: 0;
}

.main-content {
  flex: 1;
  min-height: 0;
  padding: 16px;
  overflow: auto;
  background-color: #f1f5f9;
  box-sizing: border-box;
}

.fade-enter-active, .fade-leave-active {
  transition: opacity 0.15s ease;
}

.fade-enter-from, .fade-leave-to {
  opacity: 0;
}
</style>