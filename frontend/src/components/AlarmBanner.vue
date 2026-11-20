<template>
  <el-scrollbar v-if="visible" class="alarm-banner" height="40px">
    <div class="banner-content">
      <template v-if="activeList.length === 0">
        <el-tag effect="light" size="small" type="success">✓ 无活动报警</el-tag>
      </template>
      <template v-else>
        <div class="marquee" @click="goAlarms">
          <el-tag
              v-for="item in activeList"
              :key="item.id"
              :class="{ emergency: item.level === 4 }"
              :type="levelTagType(item.level)"
              class="alarm-tag"
              effect="dark"
              size="small"
          >
            {{ levelText(item.level) }} · {{ item.deviceName || '未知设备' }} · {{ item.pointName || '' }} ·
            {{ item.message }}
          </el-tag>
        </div>
      </template>
    </div>
  </el-scrollbar>
</template>

<script lang="ts" setup>
import {computed} from 'vue'
import {useRouter} from 'vue-router'
import {useAlarmsStore} from '@/stores/alarms'

const router = useRouter()
const alarmsStore = useAlarmsStore()

const visible = computed(() => true)
const activeList = computed(() => alarmsStore.alarmList.slice(0, 10))

function levelTagType(level: number) {
  switch (level) {
    case 1:
      return 'info'
    case 2:
      return 'warning'
    case 3:
      return 'danger'
    case 4:
      return 'danger'
    default:
      return 'info'
  }
}

function levelText(level: number) {
  switch (level) {
    case 1:
      return '提示'
    case 2:
      return '一般'
    case 3:
      return '严重'
    case 4:
      return '紧急'
    default:
      return '未知'
  }
}

function goAlarms() {
  router.push('/alarms')
}
</script>

<style lang="scss" scoped>
.alarm-banner {
  background: linear-gradient(90deg, #fef2f2, #fff7ed);
  border-bottom: 1px solid #fecaca;
}

.banner-content {
  padding: 6px 16px;
}

.marquee {
  display: flex;
  gap: 8px;
  align-items: center;
  cursor: pointer;
}

.alarm-tag.emergency {
  animation: pulse 1s infinite;
}

@keyframes pulse {
  0%, 100% {
    opacity: 1;
  }
  50% {
    opacity: 0.6;
  }
}
</style>