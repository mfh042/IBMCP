<template>
  <router-view/>
</template>

<script lang="ts" setup>
import {useCommStore} from '@/stores/comm'
import {wsService} from '@/comm/ws'

const commStore = useCommStore()

commStore.setWsConnected(wsService.isConnected)
wsService.onConnectionChange((connected) => commStore.setWsConnected(connected))
</script>