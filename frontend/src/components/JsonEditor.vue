<template>
  <el-input
      :model-value="value"
      :placeholder="placeholder"
      :rows="rows"
      type="textarea"
      @update:model-value="$emit('update:modelValue', $event)"
  />
  <div class="json-actions">
    <el-button link size="small" @click="formatJson">格式化</el-button>
    <el-button link size="small" @click="minifyJson">压缩</el-button>
    <span v-if="!isValid && value" class="json-error">JSON 格式错误</span>
  </div>
</template>

<script lang="ts" setup>
import {computed} from 'vue'

const props = defineProps<{
  modelValue?: string
  rows?: number
  placeholder?: string
}>()
defineEmits<{
  'update:modelValue': [v: string]
}>()

const value = computed({
  get: () => props.modelValue ?? '',
  set: (v) => {
    const emit = defineEmits<{ 'update:modelValue': [v: string] }>()
    emit('update:modelValue', v)
  },
})

const isValid = computed(() => {
  if (!props.modelValue) return true
  try {
    JSON.parse(props.modelValue)
    return true
  } catch {
    return false
  }
})

function formatJson() {
  if (!props.modelValue) return
  try {
    const obj = JSON.parse(props.modelValue)
    const emit = defineEmits<{ 'update:modelValue': [v: string] }>()
    emit('update:modelValue', JSON.stringify(obj, null, 2))
  } catch {
  }
}

function minifyJson() {
  if (!props.modelValue) return
  try {
    const obj = JSON.parse(props.modelValue)
    const emit = defineEmits<{ 'update:modelValue': [v: string] }>()
    emit('update:modelValue', JSON.stringify(obj))
  } catch {
  }
}
</script>

<style lang="scss" scoped>
.json-actions {
  display: flex;
  gap: 8px;
  margin-top: 4px;
  font-size: 12px;
}

.json-error {
  color: #dc2626;
}
</style>