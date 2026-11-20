<template>
  <el-form label-width="120px">
    <el-form-item label="装备静态数据">
      <el-select v-model="wizard.equipmentCode" filterable placeholder="选择装备" style="width: 400px"
                 @change="onEquipmentChange">
        <el-option v-for="e in equipments" :key="e.equipmentCode" :label="`${e.equipmentCode} - ${e.equipmentName}`"
                   :value="e.equipmentCode"/>
      </el-select>
    </el-form-item>
    <el-form-item label="配置编码">
      <el-input v-model="wizard.configCode" placeholder="如 CFG-2026-001" style="width: 400px"/>
    </el-form-item>
    <el-form-item label="部署位置">
      <el-input v-model="wizard.location" placeholder="选择装备后自动填充" style="width: 400px"/>
    </el-form-item>
    <el-form-item label="备注">
      <el-input v-model="wizard.remark" :rows="3" style="width: 400px" type="textarea"/>
    </el-form-item>
  </el-form>
</template>

<script lang="ts" setup>
import {onMounted, ref} from 'vue'
import {equipmentsApi} from '@/api/equipments'

const props = defineProps<{ wizard: any }>()
defineEmits<{ 'update:wizard': [w: any] }>()

const equipments = ref<any[]>([])

async function loadEquipments() {
  try {
    const data = await equipmentsApi.all()
    equipments.value = Array.isArray(data) ? data : []
  } catch {
  }
}

function onEquipmentChange(code: string) {
  const eq = equipments.value.find((e) => e.equipmentCode === code)
  if (props.wizard && eq) {
    props.wizard.equipmentName = eq.equipmentName || ''
    props.wizard.location = eq.location || ''
  }
}

onMounted(loadEquipments)
</script>