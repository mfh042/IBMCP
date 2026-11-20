<template>
  <div class="page-card">
    <div class="page-header">
      <h3>路线绑定详情 #{{ routeId }}</h3>
      <el-button @click="$router.back()">返回</el-button>
    </div>
    <div v-if="!data" v-loading="loading" style="height: 200px"/>
    <template v-else>
      <el-descriptions :column="2" border>
        <el-descriptions-item label="路线编码">{{ data.routeConfig.routeCode }}</el-descriptions-item>
        <el-descriptions-item label="顺序">{{ data.routeConfig.routeSeq }}</el-descriptions-item>
      </el-descriptions>

      <el-divider content-position="left">设备绑定</el-divider>
      <div v-for="b in data.bindings" :key="b.id" class="binding-block">
        <el-descriptions :column="3" border size="small">
          <el-descriptions-item label="工序">{{ b.stepName || b.stepCode }}</el-descriptions-item>
          <el-descriptions-item label="设备">{{ b.deviceName || b.deviceCode }}</el-descriptions-item>
          <el-descriptions-item label="协议">{{ b.protocolCode }}</el-descriptions-item>
        </el-descriptions>
      </div>
    </template>
  </div>
</template>

<script lang="ts" setup>
import {computed, onMounted, ref} from 'vue'
import {useRoute} from 'vue-router'
import {routeBindApi} from '@/api/routeBind'

const route = useRoute()
const routeId = computed(() => Number(route.params.id))
const data = ref<any>(null)
const loading = ref(false)

async function load() {
  loading.value = true
  try {
    data.value = await routeBindApi.expandRoute(routeId.value)
  } finally {
    loading.value = false
  }
}

onMounted(load)
</script>

<style lang="scss" scoped>
.binding-block {
  padding: 8px 12px;
  border: 1px solid #e2e8f0;
  border-radius: 6px;
  margin-bottom: 8px;
}
</style>