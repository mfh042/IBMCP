<template>
  <div class="realtime-point">
    <div class="point-label">{{ label }}</div>
    <div :class="{ blinking: !loaded }" class="point-value">
      {{ displayValue }}
    </div>
    <div class="point-meta">
      <el-tag v-if="quality" :type="quality === 'Good' ? 'success' : 'danger'" effect="light" size="small">
        {{ quality }}
      </el-tag>
      <span v-if="unit" class="unit">{{ unit }}</span>
    </div>
  </div>
</template>

<script lang="ts" setup>
import {computed, watch} from 'vue'
import {useWsStore} from '@/stores/ws'
import {formatNumber} from '@/utils/format'

const props = defineProps<{
  pointId: number
  label?: string
  decimals?: number
  unit?: string
}>()

const wsStore = useWsStore()

watch(
    () => props.pointId,
    (id) => {
      if (id) wsStore.subscribePoint(id)
      return () => wsStore.unsubscribePoint(id)
    },
    {immediate: true},
)

const pointData = computed(() => wsStore.realTimeData[props.pointId])
const loaded = computed(() => !!pointData.value)
const quality = computed(() => pointData.value?.quality)
const displayValue = computed(() => formatNumber(pointData.value?.value, props.decimals ?? 2))
</script>

<style lang="scss" scoped>
.realtime-point {
  padding: 12px;
  background: #fff;
  border-radius: 6px;
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.06);
  min-width: 120px;
}

.point-label {
  font-size: 12px;
  color: #64748b;
  margin-bottom: 4px;
}

.point-value {
  font-size: 22px;
  font-weight: 700;
  color: #1e293b;
  font-family: 'Consolas', 'Monaco', monospace;
}

.point-value.blinking {
  color: #94a3b8;
}

.point-meta {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-top: 6px;
}

.unit {
  font-size: 12px;
  color: #94a3b8;
}
</style>