<template>
  <div class="page-card config-wizard">
    <div class="page-header">
      <h3>编辑配置 #{{ configId }}</h3>
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
      <el-button v-else :loading="submitting" type="success" @click="submit">保存修改</el-button>
    </div>
  </div>
</template>

<script lang="ts" setup>
import {onMounted, reactive, ref} from 'vue'
import {useRoute, useRouter} from 'vue-router'
import {ElMessage} from 'element-plus'
import {equipmentConfigApi} from '@/api/equipmentConfig'
import {routesApi} from '@/api/routes'
import {routeBindApi} from '@/api/routeBind'
import Step1_EquipmentSelect from './wizard-steps/Step1_EquipmentSelect.vue'
import Step2_RouteBind from './wizard-steps/Step2_RouteBind.vue'
import Step3_DeviceBind from './wizard-steps/Step3_DeviceBind.vue'
import Step4_Collection from './wizard-steps/Step4_Collection.vue'
import Step5_FunctionEnable from './wizard-steps/Step5_FunctionEnable.vue'

const route = useRoute()
const router = useRouter()
const submitting = ref(false)
const step3Ref = ref<any>(null)
const configId = Number(route.params.id)

const wizard = reactive({
  step: 1,
  configId: configId,
  configCode: '',
  equipmentCode: '',
  equipmentName: '',
  location: '',
  remark: '',
  routeBindings: [] as any[],
  functionBindings: [] as any[],
  deviceBindings: [] as any[],
})

async function loadConfig() {
  try {
    const data: any = await equipmentConfigApi.expand(configId)
    const config: any = data?.config || data
    wizard.configCode = config?.configCode || ''
    wizard.equipmentCode = config?.equipmentCode || ''
    wizard.equipmentName = config?.equipmentName || ''
    wizard.location = config?.location || ''
    wizard.remark = config?.remark || ''

    const routes = data?.routes || []
    for (const r of routes) {
      wizard.routeBindings.push({
        routeConfigId: r.id,
        routeId: r.routeId || 0,
        routeCode: r.routeCode,
        routeName: r.routeName,
        routeSeq: r.routeSeq,
        steps: [],
      })

      for (const b of r.devices || []) {
        wizard.deviceBindings.push({
          id: b.id,
          routeCode: r.routeCode,
          stepId: b.stepId || 0,
          stepCode: b.stepCode || '',
          stepName: b.stepName || '',
          stepSeq: b.stepSeq || 0,
          deviceCode: b.deviceCode || '',
          deviceName: b.deviceName || '',
          deviceConnectionStr: b.deviceConnection
            ? (typeof b.deviceConnection === 'string' ? b.deviceConnection : JSON.stringify(b.deviceConnection))
            : '',
          portCode: b.portCode || '',
          protocolCode: b.protocolCode || '',
          collections: (b.collections || []).map((c: any) => ({
            id: c.id,
            itemCode: c.itemCode,
            itemName: c.itemName,
            deviceCode: c.deviceCode,
            dataType: c.dataType || '',
            readWrite: c.readWrite || '',
            collectionCycle: c.collectionCycle,
            addressOverride: c.addressOverride || '',
            enabled: c.enabled === 1,
          })),
        })
      }
    }

    for (const rb of wizard.routeBindings) {
      if (!rb.routeId && rb.routeCode) {
        try {
          const routeDetail: any = await routesApi.getByCode(rb.routeCode)
          if (routeDetail?.id) {
            rb.routeId = routeDetail.id
          }
        } catch {
        }
      }
    }

    const functions = data?.functions || []
    for (const f of functions) {
      wizard.functionBindings.push({
        id: f.id,
        functionCode: f.functionCode,
        functionName: f.functionName,
        functionType: f.functionType,
        description: f.description || '',
        enabled: f.enabled === 1,
        functionParamsStr: f.functionParams
          ? (typeof f.functionParams === 'string' ? f.functionParams : JSON.stringify(f.functionParams))
          : '',
      })
    }
  } catch (e: any) {
    ElMessage.error(e.message || '加载配置失败')
    router.back()
  }
}

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

    // Update config（config_status / run_status 由服务端托管，禁止直传）
    await equipmentConfigApi.update(configId, {
      configCode: wizard.configCode,
      equipmentCode: wizard.equipmentCode,
      equipmentName: wizard.equipmentName,
      location: wizard.location,
      remark: wizard.remark,
    })

    // Update functions（function_params 为 JSONB）
    for (const fb of wizard.functionBindings) {
      if (fb.id) {
        await equipmentConfigApi.updateFunction(fb.id, {
          enabled: fb.enabled ? 1 : 0,
          functionParams: toJsonText(fb.functionParamsStr),
        })
      }
    }

    // Update device bindings + collections
    const routeConfigs = await equipmentConfigApi.routesOf(configId)
    const routeList = (routeConfigs as any)?.items ?? (Array.isArray(routeConfigs) ? routeConfigs : [])

    for (const routeConfig of routeList) {
      const routeCode = routeConfig.routeCode
      const routeConfigId = routeConfig.id
      const deviceBindings = wizard.deviceBindings.filter((d: any) => d.routeCode === routeCode)

      for (const db of deviceBindings) {
        // running_route_device 无 step_id / created_by 列；device_connection 为 JSONB
        const devicePayload = {
          stepCode: db.stepCode || '',
          stepName: db.stepName,
          stepSeq: db.stepSeq || 0,
          deviceCode: db.deviceCode,
          deviceName: db.deviceName,
          deviceConnection: toJsonText(db.deviceConnectionStr),
          portCode: db.portCode,
          protocolCode: db.protocolCode,
        }

        if (db.id) {
          // 更新已有绑定
          await routeBindApi.updateDeviceBinding(db.id, devicePayload)

          // 同步该绑定下的采集项
          if (db.collections && db.collections.length > 0) {
            const existingCollections: any = await routeBindApi.collectionsOfDevice(db.id)
            const existingList = existingCollections?.items || existingCollections?.list || existingCollections || []

            for (const col of db.collections) {
              if (!col.itemCode) continue
              const existing = existingList.find((ec: any) => ec.itemCode === col.itemCode)
              // running_device_collection 无 remark / created_by 列
              const collectionBody = {
                itemCode: col.itemCode,
                itemName: col.itemName,
                deviceCode: db.deviceCode,
                collectionCycle: col.collectionCycle,
                addressOverride: col.addressOverride,
                dataType: col._dataType || col.dataType || '',
                readWrite: col._readWrite || col.readWrite || '',
                enabled: col.enabled ? 1 : 0,
              }
              if (existing?.id) {
                await routeBindApi.updateCollection(existing.id, collectionBody)
              } else {
                await routeBindApi.bindCollection(db.id, collectionBody)
              }
            }
          }
        } else {
          // 新建绑定
          const deviceRes: any = await routeBindApi.bindDevice(routeConfigId, {...devicePayload, enabled: 1})
          const deviceBindingId = deviceRes?.id ?? deviceRes?.data?.id

          if (deviceBindingId && db.collections) {
            for (const col of db.collections) {
              if (!col.itemCode) continue
              await routeBindApi.bindCollection(deviceBindingId, {
                itemCode: col.itemCode,
                itemName: col.itemName,
                deviceCode: db.deviceCode,
                collectionCycle: col.collectionCycle,
                addressOverride: col.addressOverride,
                dataType: col._dataType || col.dataType || '',
                readWrite: col._readWrite || col.readWrite || '',
                enabled: col.enabled ? 1 : 0,
              })
            }
          }
        }
      }
    }

    ElMessage.success('保存成功')
    router.push(`/equipment-config/${configId}`)
  } catch (e: any) {
    ElMessage.error(e.message || '保存失败')
  } finally {
    submitting.value = false
  }
}

function handleCancel() {
  router.back()
}

onMounted(loadConfig)
</script>

<style lang="scss" scoped>
.wizard-actions {
  margin-top: 20px;
  display: flex;
  justify-content: flex-end;
  gap: 8px;
}
</style>