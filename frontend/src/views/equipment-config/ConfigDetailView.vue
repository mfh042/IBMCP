<template>
  <div class="page-card">
    <div class="page-header">
      <h3>配置详情 #{{ configId }}</h3>
      <div>
        <el-button v-if="treeData?.configStatus !== 1" :loading="publishing" type="success" @click="handlePublish">
          发布
        </el-button>
        <el-button v-else :loading="unpublishing" type="warning" @click="handleUnpublish">
          撤回
        </el-button>
        <el-button type="primary" @click="$router.push(`/equipment-config/${configId}/edit`)">编辑</el-button>
        <el-button @click="$router.back()">返回</el-button>
      </div>
    </div>

    <div v-if="!treeData" v-loading="loading" style="height: 200px"/>
    <template v-else>
      <el-descriptions :column="2" border title="装备运行实例">
        <el-descriptions-item label="配置编码">{{ treeData?.configCode || '-' }}</el-descriptions-item>
        <el-descriptions-item label="装备">{{ treeData?.equipmentCode || '-' }} - {{
            treeData?.equipmentName || '-'
          }}
        </el-descriptions-item>
        <el-descriptions-item label="位置">{{ treeData?.location || '-' }}</el-descriptions-item>
        <el-descriptions-item label="配置状态">
          <el-tag size="small">{{ treeData?.configStatus === 1 ? '已下发' : '草稿' }}</el-tag>
        </el-descriptions-item>
        <el-descriptions-item :span="2" label="备注">{{ treeData?.remark || '-' }}</el-descriptions-item>
      </el-descriptions>

      <el-divider content-position="left">工艺路线与设备绑定</el-divider>
      <div v-for="r in (treeData.routes || [])" :key="r.id" class="route-block">
        <h4>路线: {{ r.routeName || r.routeCode }} [seq={{ r.routeSeq }}]</h4>
        <div v-for="b in (r.bindings || [])" :key="b.id" class="binding-block">
          <el-descriptions :column="3" border size="small">
            <el-descriptions-item label="工序">{{ b.stepName || b.stepCode }}</el-descriptions-item>
            <el-descriptions-item label="设备">{{ b.deviceName || b.deviceCode }}</el-descriptions-item>
            <el-descriptions-item label="协议">{{ b.protocolCode }}</el-descriptions-item>
          </el-descriptions>
          <div class="collections">
            <span class="sub-title">采集项：</span>
            <el-tag v-for="c in (b.collections || [])" :key="c.id" class="coll-tag" size="small">
              {{ c.itemName || c.itemCode }} ({{ c.collectionCycle }}ms)
            </el-tag>
          </div>
        </div>
      </div>

      <el-divider content-position="left">装备功能</el-divider>
      <div class="func-list">
        <el-tag v-for="f in (treeData.functions || [])" :key="f.id" :type="f.enabled ? 'success' : 'info'"
                effect="plain" size="large">
          {{ f.functionName || f.functionCode }} {{ f.enabled ? '✓ 启用' : '✗ 禁用' }}
        </el-tag>
      </div>
    </template>
  </div>
</template>

<script lang="ts" setup>
import {computed, onMounted, ref} from 'vue'
import {useRoute} from 'vue-router'
import {ElMessage, ElMessageBox} from 'element-plus'
import {equipmentConfigApi} from '@/api/equipmentConfig'
import {httpService} from '@/comm/http'

const route = useRoute()
const configId = computed(() => Number(route.params.id))
const treeData = ref<any>(null)
const loading = ref(false)
const publishing = ref(false)
const unpublishing = ref(false)

async function load() {
  loading.value = true
  try {
    treeData.value = await equipmentConfigApi.expand(configId.value)
  } finally {
    loading.value = false
  }
}

async function handlePublish() {
  try {
    await ElMessageBox.confirm('确定要发布此配置吗？发布后将同步设备和信号点到运行系统。', '发布确认', {
      confirmButtonText: '确定发布',
      cancelButtonText: '取消',
      type: 'success',
    })
    publishing.value = true

    // First get the full config
    const fullConfig: any = await equipmentConfigApi.get(configId.value)
    console.log('[Publish] Full config:', fullConfig)

    // Update config status
    const updateBody = {
      configCode: fullConfig?.configCode || '',
      equipmentCode: fullConfig?.equipmentCode || '',
      equipmentName: fullConfig?.equipmentName || '',
      location: fullConfig?.location || '',
      remark: fullConfig?.remark || '',
      configStatus: 1,
    }

    console.log('[Publish] Updating config with body:', updateBody)
    await httpService.put(`/api/running/configs/${configId.value}`, updateBody)

    // Sync devices and points to running system
    const expandData: any = await equipmentConfigApi.expand(configId.value)
    console.log('[Publish] Expand data for sync:', expandData)

    const routes = expandData?.routes || []
    for (const route of routes) {
      const bindings = route?.bindings || []
      for (const binding of bindings) {
        // Sync device to /api/devices
        const deviceBody = {
          deviceCode: binding.deviceCode,
          deviceName: binding.deviceName,
          protocolCode: binding.protocolCode,
          portCode: binding.portCode,
          deviceConnection: binding.deviceConnection || '',
          stepCode: binding.stepCode,
          stepName: binding.stepName,
          routeCode: route.routeCode,
          routeName: route.routeName,
          enabled: 1,
          createdBy: 'admin',
        }
        console.log('[Publish] Syncing device:', deviceBody)
        await httpService.post('/api/devices', deviceBody)

        // Sync collections (points) to /api/devices/${deviceId}/points
        const collections = binding.collections || []
        if (collections.length > 0) {
          // Get the device ID from the response or use deviceCode to find it
          const deviceList: any = await httpService.get('/api/devices', {params: {keyword: binding.deviceCode}})
          const devices = deviceList?.items || deviceList?.list || deviceList || []
          const targetDevice = devices.find((d: any) => d.deviceCode === binding.deviceCode)

          if (targetDevice?.id) {
            for (const col of collections) {
              if (col.itemCode) {
                const pointBody = {
                  itemCode: col.itemCode,
                  itemName: col.itemName,
                  dataType: col.dataType || col._dataType || '',
                  readWrite: col.readWrite || col._readWrite || '',
                  collectionCycle: col.collectionCycle,
                  addressOverride: col.addressOverride,
                  enabled: col.enabled ? 1 : 0,
                  remark: col.remark || '',
                  createdBy: 'admin',
                }
                console.log('[Publish] Syncing point:', pointBody, 'to device:', targetDevice.id)
                await httpService.post(`/api/devices/${targetDevice.id}/points`, pointBody)
              }
            }
          }
        }
      }
    }

    ElMessage.success('发布成功，设备和信号点已同步')
    await load()
  } catch (e: any) {
    console.error('[Publish] Error:', e)
    if (e !== 'cancel') {
      const errorMsg = e.message || e.toString() || '发布失败'
      ElMessage.error(errorMsg)
    }
  } finally {
    publishing.value = false
  }
}

async function handleUnpublish() {
  try {
    await ElMessageBox.confirm('确定要撤回此配置吗？撤回后配置将不再生效。', '撤回确认', {
      confirmButtonText: '确定撤回',
      cancelButtonText: '取消',
      type: 'warning',
    })
    unpublishing.value = true
    await httpService.put(`/api/running/configs/${configId.value}`, {
      configStatus: 0,
    })
    ElMessage.success('已撤回')
    await load()
  } catch (e: any) {
    if (e !== 'cancel') {
      ElMessage.error(e.message || '撤回失败')
    }
  } finally {
    unpublishing.value = false
  }
}

onMounted(load)
</script>

<style lang="scss" scoped>
.route-block {
  margin-bottom: 16px;

  h4 {
    margin-bottom: 8px;
  }
}

.binding-block {
  padding: 8px 12px;
  border: 1px solid #e2e8f0;
  border-radius: 6px;
  margin-bottom: 8px;
}

.collections {
  margin-top: 8px;
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  align-items: center;
}

.sub-title {
  font-size: 12px;
  color: #64748b;
  margin-right: 8px;
}

.coll-tag {
  margin-right: 4px;
}

.func-list {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}
</style>