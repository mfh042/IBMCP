<template>
  <div class="page-card config-wizard">
    <div class="page-header">
      <h3>装备配置向导</h3>
      <el-button @click="handleCancel">取消</el-button>
    </div>

    <el-steps :active="wizard.step - 1" finish-status="success" simple style="margin-bottom: 24px">
      <el-step title="装备实例"/>
      <el-step title="路线绑定"/>
      <el-step title="设备绑定"/>
      <el-step title="采集项配置"/>
      <el-step title="功能启用"/>
    </el-steps>

    <el-card shadow="never">
      <Step1_EquipmentSelect v-if="wizard.step === 1" :wizard="wizard"/>
      <Step2_RouteBind v-else-if="wizard.step === 2" :wizard="wizard"/>
      <Step3_DeviceBind v-else-if="wizard.step === 3" ref="step3Ref" :wizard="wizard"/>
      <Step4_Collection v-else-if="wizard.step === 4" :wizard="wizard"/>
      <Step5_FunctionEnable v-else-if="wizard.step === 5" :wizard="wizard"/>
    </el-card>

    <div class="wizard-actions">
      <el-button v-if="wizard.step > 1" @click="wizard.step--">上一步</el-button>
      <el-button v-if="wizard.step < 5" type="primary" @click="nextStep">下一步</el-button>
      <el-button v-else :loading="submitting" type="success" @click="submit">完成配置</el-button>
    </div>
  </div>
</template>

<script lang="ts" setup>
import {reactive, ref} from 'vue'
import {useRouter} from 'vue-router'
import {ElMessage} from 'element-plus'
import {equipmentConfigApi} from '@/api/equipmentConfig'
import {routeBindApi} from '@/api/routeBind'
import Step1_EquipmentSelect from './wizard-steps/Step1_EquipmentSelect.vue'
import Step2_RouteBind from './wizard-steps/Step2_RouteBind.vue'
import Step3_DeviceBind from './wizard-steps/Step3_DeviceBind.vue'
import Step4_Collection from './wizard-steps/Step4_Collection.vue'
import Step5_FunctionEnable from './wizard-steps/Step5_FunctionEnable.vue'

const router = useRouter()
const submitting = ref(false)
const step3Ref = ref<any>(null)

const wizard = reactive({
  step: 1,
  configId: 0,
  configCode: '',
  equipmentCode: '',
  equipmentName: '',
  location: '',
  remark: '',
  routeBindings: [] as any[],
  functionBindings: [] as any[],
  deviceBindings: [] as any[],
})

function nextStep() {
  if (wizard.step >= 5) return
  // Sync device bindings before moving to Step 4
  if (wizard.step === 3) {
    step3Ref.value?.syncDeviceBindings()
  }
  wizard.step++
}

function toJsonText(val: any): string {
  if (val === null || val === undefined || val === '') return '{}'
  if (typeof val === 'object') return JSON.stringify(val)
  try { JSON.parse(val); return val } catch { return '{}' }
}

async function submit() {
  submitting.value = true
  try {
    // Sync device bindings from Step3 table
    step3Ref.value?.syncDeviceBindings()

    // Step 1: Create config（config_status / run_status 由服务端托管，禁止直传）
    const configBody = {
      configCode: wizard.configCode,
      equipmentCode: wizard.equipmentCode,
      equipmentName: wizard.equipmentName,
      location: wizard.location,
      remark: wizard.remark,
    }
    const configRes: any = await equipmentConfigApi.create(configBody)
    const configId = configRes?.id ?? configRes?.data?.id

    if (!configId) {
      ElMessage.error('配置创建失败，未返回ID')
      return
    }

    // Step 2: Bind routes
    for (const rb of wizard.routeBindings) {
      await equipmentConfigApi.bindRoute(configId, {
        routeCode: rb.routeCode,
        routeName: rb.routeName,
        routeSeq: rb.routeSeq,
        enabled: 1,
      })
    }

    // Step 3: Bind functions（function_params 为 JSONB，统一序列化为合法 JSON 文本）
    for (const fb of wizard.functionBindings) {
      if (fb.enabled) {
        await equipmentConfigApi.bindFunction(configId, {
          functionCode: fb.functionCode,
          functionName: fb.functionName,
          functionType: fb.functionType,
          enabled: 1,
          functionParams: toJsonText(fb.functionParamsStr),
        })
      }
    }

    // Step 4: 取已创建的路线配置，逐条绑定设备 + 采集项
    const routeConfigs = await equipmentConfigApi.routesOf(configId)
    const routeList = (routeConfigs as any)?.items ?? (Array.isArray(routeConfigs) ? routeConfigs : [])

    for (const routeConfig of routeList) {
      const routeCode = routeConfig.routeCode
      const routeConfigId = routeConfig.id
      const deviceBindings = wizard.deviceBindings.filter((d: any) => d.routeCode === routeCode)

      for (const db of deviceBindings) {
        // running_route_device 无 step_id / created_by 列；device_connection 为 JSONB
        const deviceRes: any = await routeBindApi.bindDevice(routeConfigId, {
          stepCode: db.stepCode || '',
          stepName: db.stepName,
          stepSeq: db.stepSeq || 0,
          deviceCode: db.deviceCode,
          deviceName: db.deviceName,
          deviceConnection: toJsonText(db.deviceConnectionStr),
          portCode: db.portCode,
          protocolCode: db.protocolCode,
          enabled: 1,
        })
        const deviceBindingId = deviceRes?.id ?? deviceRes?.data?.id

        if (deviceBindingId && db.collections) {
          for (const col of db.collections) {
            await routeBindApi.bindCollection(deviceBindingId, {
              itemCode: col.itemCode,
              itemName: col.itemName,
              deviceCode: db.deviceCode,
              collectionCycle: col.collectionCycle,
              addressOverride: col.addressOverride,
              enabled: col.enabled ? 1 : 0,
            })
          }
        }
      }
    }

    ElMessage.success('配置提交成功')
    router.push('/equipment-config')
  } catch (e: any) {
    ElMessage.error(e.message || '配置提交失败')
  } finally {
    submitting.value = false
  }
}

function handleCancel() {
  router.push('/equipment-config')
}
</script>

<style lang="scss" scoped>
.wizard-actions {
  margin-top: 20px;
  display: flex;
  justify-content: flex-end;
  gap: 8px;
}
</style>