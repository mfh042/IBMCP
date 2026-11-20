<template>
  <div>
    <h4 style="margin-bottom: 12px">装备功能启用</h4>
    <table class="func-table">
      <thead>
      <tr>
        <th>功能编码</th>
        <th>功能名称</th>
        <th>说明</th>
        <th style="width: 100px">启用</th>
        <th>功能参数 JSON</th>
      </tr>
      </thead>
      <tbody>
      <tr v-for="(fn, idx) in wizard.functionBindings" :key="idx">
        <td>{{ fn.functionCode }}</td>
        <td>{{ fn.functionName }}</td>
        <td>{{ fn.description }}</td>
        <td>
          <el-switch v-model="fn.enabled"/>
        </td>
        <td style="width: 320px">
          <JsonEditor v-model="fn.functionParamsStr" :rows="3"/>
        </td>
      </tr>
      </tbody>
    </table>
  </div>
</template>

<script lang="ts" setup>
import {onMounted} from 'vue'
import JsonEditor from '@/components/JsonEditor.vue'
import {equipmentsApi} from '@/api/equipments'

const props = defineProps<{ wizard: any }>()
defineEmits<{ 'update:wizard': [w: any] }>()

async function loadFunctions() {
  try {
    const data = await equipmentsApi.functionAll()
    const list = Array.isArray(data) ? data : []
    props.wizard.functionBindings = list.map((f: any) => ({
      id: 0,
      functionCode: f.functionCode,
      functionName: f.functionName || '',
      description: f.description || '',
      enabled: true,
      functionParamsStr: '',
    }))
  } catch {
  }
}

onMounted(loadFunctions)
</script>

<style lang="scss" scoped>
.func-table {
  width: 100%;
  border-collapse: collapse;

  th, td {
    padding: 8px 12px;
    border-bottom: 1px solid #e2e8f0;
    text-align: left;
  }
}
</style>