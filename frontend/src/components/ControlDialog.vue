<template>
  <el-dialog
      v-model="visible"
      destroy-on-close
      title="下发控制命令"
      width="420px"
  >
    <el-form :model="form" label-width="80px">
      <el-form-item label="点位">
        <el-select v-model="form.pointId" placeholder="选择点位" style="width: 100%">
          <el-option
              v-for="pt in points"
              :key="pt.id"
              :label="pt.itemName"
              :value="controlPointId(pt)"
          />
        </el-select>
      </el-form-item>
      <el-form-item label="命令值">
        <el-input v-model="form.value" placeholder="输入要下发的值"/>
      </el-form-item>
    </el-form>

    <div v-if="result" class="result-box">
      {{ result }}
    </div>

    <template #footer>
      <el-button @click="visible = false">取消</el-button>
      <el-button :loading="sending" type="primary" @click="handleSend">下发</el-button>
    </template>
  </el-dialog>
</template>

<script lang="ts" setup>
import {reactive, ref, watch} from 'vue'
import {devicesApi} from '@/api/devices'

const props = defineProps<{
  modelValue: boolean
  deviceCode: string
  points: any[]
}>()
const emit = defineEmits<{
  'update:modelValue': [v: boolean]
}>()

const visible = ref(false)
const sending = ref(false)
const result = ref('')
const form = reactive<{ pointId: number | null; value: string }>({
  pointId: null,
  value: '',
})

const controlPointId = (pt: any) => Number(pt?.usingPointId ?? pt?.id ?? 0)

watch(
    () => props.modelValue,
    (v) => {
      visible.value = v
      if (v) {
        result.value = ''
        form.pointId = null
        form.value = ''
      }
    },
)
watch(visible, (v) => emit('update:modelValue', v))

async function handleSend() {
  if (!form.pointId || !form.value) return
  sending.value = true
  result.value = '执行中...'
  try {
    const pt = props.points.find((p: any) => controlPointId(p) === form.pointId)
    const data = await devicesApi.control(props.deviceCode, {
      pointId: form.pointId,
      pointName: pt ? (pt.itemName || pt.name || '') : '',
      value: form.value,
    })
    result.value = data ? `✅ 执行成功` : `❌ 执行失败`
  } catch (e: any) {
    result.value = `❌ 请求异常: ${e.message}`
  } finally {
    sending.value = false
  }
}
</script>

<style lang="scss" scoped>
.result-box {
  margin-top: 12px;
  padding: 8px 12px;
  background: #f1f5f9;
  border-radius: 4px;
  font-size: 13px;
}
</style>