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
import {httpService} from '@/comm/http'
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

      for (const b of r.bindings || []) {
        wizard.deviceBindings.push({
          id: b.id,
          routeCode: r.routeCode,
          stepId: b.stepId || 0,
          stepCode: b.stepCode || '',
          stepName: b.stepName || '',
          stepSeq: b.stepSeq || 0,
          deviceCode: b.deviceCode || '',
          deviceName: b.deviceName || '',
          deviceConnectionStr: b.deviceConnection || '',
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
        functionParamsStr: f.functionParams || '',
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

async function submit() {
  submitting.value = true
  try {
    // Sync device bindings from Step3 table
    step3Ref.value?.syncDeviceBindings()

    // Update config
    await equipmentConfigApi.update(configId, {
      configCode: wizard.configCode,
      equipmentCode: wizard.equipmentCode,
      equipmentName: wizard.equipmentName,
      location: wizard.location,
      remark: wizard.remark,
      configStatus: 1, // Set to active/published after editing
    })

    // Update functions
    for (const fb of wizard.functionBindings) {
      if (fb.id) {
        await equipmentConfigApi.updateFunction(fb.id, {
          enabled: fb.enabled ? 1 : 0,
          functionParams: fb.functionParamsStr || '{}',
        })
      }
    }

    // Update device bindings
    const routeConfigs = await equipmentConfigApi.routesOf(configId)
    const routeList = (routeConfigs as any)?.items ?? (Array.isArray(routeConfigs) ? routeConfigs : [])

    for (const routeConfig of routeList) {
      const routeCode = routeConfig.routeCode
      const routeConfigId = routeConfig.id
      const deviceBindings = wizard.deviceBindings.filter((d: any) => d.routeCode === routeCode)

      console.log('[Submit] Route:', routeCode, 'deviceBindings:', deviceBindings.length)

      for (const db of deviceBindings) {
        console.log('[Submit] Processing binding:', db.stepCode, 'id:', db.id, 'collections:', db.collections?.length || 0)

        if (db.id) {
          // Update existing binding
          await httpService.put(`/api/running/devices/${db.id}`, {
            stepId: db.stepId || 0,
            stepCode: db.stepCode || '',
            stepName: db.stepName,
            stepSeq: db.stepSeq || 0,
            deviceCode: db.deviceCode,
            deviceName: db.deviceName,
            deviceConnection: db.deviceConnectionStr,
            portCode: db.portCode,
            protocolCode: db.protocolCode,
          })

          // Also save collections for existing bindings
          if (db.collections && db.collections.length > 0) {
            console.log('[Submit] Saving collections for existing binding:', db.id, 'count:', db.collections.length)

            // First, get existing collections
            const existingCollections: any = await httpService.get(`/api/running/devices/${db.id}/collections`)
            const existingList = existingCollections?.items || existingCollections?.list || existingCollections || []
            console.log('[Submit] Existing collections:', existingList.length)

            for (const col of db.collections) {
              if (!col.itemCode) continue

              // Find if this collection already exists
              const existing = existingList.find((ec: any) => ec.itemCode === col.itemCode)

              const collectionBody = {
                itemCode: col.itemCode,
                itemName: col.itemName,
                deviceCode: db.deviceCode,
                collectionCycle: col.collectionCycle,
                addressOverride: col.addressOverride,
                dataType: col._dataType || col.dataType || '',
                readWrite: col._readWrite || col.readWrite || '',
                enabled: col.enabled ? 1 : 0,
                remark: col.remark || '',
              }

              if (existing?.id) {
                // Update existing collection
                console.log('[Submit] Updating collection:', existing.id, col.itemCode)
                await httpService.put(`/api/running/collections/${existing.id}`, collectionBody)
              } else {
                // Create new collection
                console.log('[Submit] Creating collection:', col.itemCode)
                await httpService.post(`/api/running/devices/${db.id}/collections`, {
                  ...collectionBody,
                  createdBy: 'admin',
                })
              }
            }
          }
        } else {
          // Create new binding
          const deviceRes: any = await httpService.post(`/api/running/routes/${routeConfigId}/devices`, {
            stepId: db.stepId || 0,
            stepCode: db.stepCode || '',
            stepName: db.stepName,
            stepSeq: db.stepSeq || 0,
            deviceCode: db.deviceCode,
            deviceName: db.deviceName,
            deviceConnection: db.deviceConnectionStr,
            portCode: db.portCode,
            protocolCode: db.protocolCode,
            enabled: 1,
            createdBy: 'admin',
          })
          const deviceBindingId = deviceRes?.id ?? deviceRes?.data?.id

          if (deviceBindingId && db.collections) {
            console.log('[Submit] Saving collections for new binding:', deviceBindingId, 'count:', db.collections.length)
            for (const col of db.collections) {
              if (col.itemCode) {
                await httpService.post(`/api/running/devices/${deviceBindingId}/collections`, {
                  itemCode: col.itemCode,
                  itemName: col.itemName,
                  deviceCode: db.deviceCode,
                  collectionCycle: col.collectionCycle,
                  addressOverride: col.addressOverride,
                  dataType: col._dataType || '',
                  readWrite: col._readWrite || '',
                  enabled: col.enabled ? 1 : 0,
                  remark: col.remark || '',
                  createdBy: 'admin',
                })
              }
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