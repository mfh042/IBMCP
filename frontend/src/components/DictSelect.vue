<template>
  <el-select
      :model-value="modelValue"
      :placeholder="placeholder"
      class="dict-select"
      clearable
      filterable
      @update:model-value="handleChange"
  >
    <el-option
        v-for="item in items"
        :key="item.id"
        :label="getLabel(item)"
        :value="getValue(item)"
    />
  </el-select>
</template>

<script lang="ts" setup>
import {computed, watch} from 'vue'
import {useDictionariesStore} from '@/stores/dictionaries'

const props = defineProps<{
  modelValue?: string | number | null
  dictKey: 'categories' | 'brands' | 'ports' | 'protocols' | 'dataItems'
  placeholder?: string
}>()

const emit = defineEmits<{
  'update:modelValue': [value: string | number | null]
  'change': [value: string | number | null]
}>()

function handleChange(value: string | number | null) {
  emit('update:modelValue', value)
  emit('change', value)
}

const dictStore = useDictionariesStore()

const items = computed(() => {
  const map: Record<string, any[]> = {
    categories: dictStore.categories,
    brands: dictStore.brands,
    ports: dictStore.ports,
    protocols: dictStore.protocols,
    dataItems: dictStore.dataItems,
  }
  return map[props.dictKey] || []
})

const fieldMap: Record<string, { code: string; label: string }> = {
  categories: {code: 'categoryCode', label: 'categoryName'},
  brands: {code: 'brandCode', label: 'brandName'},
  ports: {code: 'portCode', label: 'portName'},
  protocols: {code: 'protocolCode', label: 'protocolName'},
  dataItems: {code: 'itemCode', label: 'itemName'},
}

function getValue(item: any): string {
  const fields = fieldMap[props.dictKey]
  return item[fields.code] ?? item.code ?? item.value ?? item.id
}

function getLabel(item: any): string {
  const fields = fieldMap[props.dictKey]
  return item[fields.label] ?? item.label ?? item.name ?? item[fields.code] ?? item.code ?? String(item.id)
}

watch(
    () => dictStore.loaded,
    (loaded) => {
      if (!loaded) dictStore.loadAll()
    },
    {immediate: true},
)
</script>