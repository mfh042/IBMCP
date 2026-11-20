<template>
  <el-tree
      :data="treeData"
      :props="{ label: 'label', children: 'children' }"
      default-expand-all
      highlight-current
      node-key="id"
      @node-click="handleNodeClick"
  />
</template>

<script lang="ts" setup>
import {ref, watch} from 'vue'
import {equipmentConfigApi} from '@/api/equipmentConfig'

const props = defineProps<{
  configId?: number | null
}>()

const emit = defineEmits<{
  select: [node: any]
}>()

const treeData = ref<any[]>([])

async function loadTree() {
  if (!props.configId) {
    treeData.value = []
    return
  }
  try {
    const data: any = await equipmentConfigApi.expand(props.configId)
    const nodes: any[] = []
    const routes = data?.routes || []
    routes.forEach((r: any) => {
      const children: any[] = []
      const bindings = r.bindings || []
      bindings.forEach((b: any) => {
        const pts: any[] = (b.collections || []).map((c: any) => ({
          id: `pt-${c.id}`,
          label: c.itemName || c.itemCode,
          pointId: c.id,
          collectionId: c.id,
          deviceBindingId: b.id,
          isPoint: true,
        }))
        children.push({
          id: `dev-${b.id}`,
          label: b.deviceName || b.deviceCode,
          deviceBindingId: b.id,
          isDevice: true,
          children: pts,
        })
      })
      nodes.push({
        id: `route-${r.id}`,
        label: r.routeName || r.routeCode,
        routeConfigId: r.id,
        isRoute: true,
        children,
      })
    })
    treeData.value = nodes
  } catch {
    treeData.value = []
  }
}

function handleNodeClick(data: any) {
  emit('select', data)
}

watch(() => props.configId, loadTree, {immediate: true})
</script>