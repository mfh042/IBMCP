<template>
  <div>
    <h4 style="margin-bottom: 12px">工艺路线绑定</h4>
    <div v-for="rb in wizard.routeBindings" :key="rb.routeConfigId || rb.routeCode" class="route-binding">
      <div class="route-row">
        <span class="route-label">路线</span>
        <el-select v-model="rb.routeCode" filterable placeholder="选择工艺路线" style="width: 300px"
                   @change="onRouteChange(rb, $event)">
          <el-option v-for="r in routes" :key="r.routeCode" :label="r.routeCode" :value="r.routeCode"/>
        </el-select>
        <el-button v-if="rb.routeCode" link size="small" style="margin-left: 8px" type="primary"
                   @click="viewRouteDetail(rb)">查看明细
        </el-button>
      </div>
      <el-button link type="danger" @click="removeRoute(rb)">移除该路线</el-button>
    </div>
    <el-button v-if="wizard.routeBindings.length < 5" link type="primary" @click="addRoute">+ 添加路线</el-button>

    <!-- 工艺路线明细弹窗 -->
    <el-dialog v-model="detailVisible" title="工艺路线明细" width="900px">
      <el-descriptions v-if="currentRoute" :column="3" border>
        <el-descriptions-item label="路线编码">{{ currentRoute.routeCode }}</el-descriptions-item>
        <el-descriptions-item label="路线名称">{{ currentRoute.routeName }}</el-descriptions-item>
        <el-descriptions-item label="路线版本">{{ currentRoute.version || '-' }}</el-descriptions-item>
        <el-descriptions-item label="资源类型">{{ currentRoute.resourceType || '-' }}</el-descriptions-item>
        <el-descriptions-item label="状态">
          <el-tag :type="currentRoute.status === 1 ? 'success' : 'info'" size="small">
            {{ currentRoute.status === 1 ? '启用' : '停用' }}
          </el-tag>
        </el-descriptions-item>
        <el-descriptions-item label="工步数量">{{ (currentRouteSteps || []).length }}</el-descriptions-item>
        <el-descriptions-item :span="3" label="资源URL">{{ currentRoute.resourceUrl || '-' }}</el-descriptions-item>
        <el-descriptions-item :span="3" label="缩略图URL">{{ currentRoute.thumbnailUrl || '-' }}</el-descriptions-item>
        <el-descriptions-item :span="3" label="描述">{{ currentRoute.description || '-' }}</el-descriptions-item>
      </el-descriptions>
      <el-divider content-position="left">工序列表</el-divider>
      <el-table v-if="currentRouteSteps.length > 0" :data="currentRouteSteps" border max-height="300" size="small">
        <el-table-column label="顺序" prop="stepSeq" width="70"/>
        <el-table-column label="工序编码" prop="stepCode" width="120"/>
        <el-table-column label="工序名称" prop="stepName" width="120"/>
        <el-table-column label="关联设备" prop="deviceCode" width="120"/>
        <el-table-column label="时长(秒)" prop="durationTime" width="90"/>
        <el-table-column label="工作中心" prop="workCenter" width="100"/>
        <el-table-column label="关键工序" width="90">
          <template #default="{ row }">
            <el-tag :type="row.isCritical ? 'danger' : 'info'" size="small">{{ row.isCritical ? '是' : '否' }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="状态" width="70">
          <template #default="{ row }">
            <el-tag :type="row.status === 1 ? 'success' : 'info'" size="small">{{
                row.status === 1 ? '启用' : '停用'
              }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="描述" min-width="120" prop="description"/>
      </el-table>
      <el-empty v-else description="暂无工序"/>
    </el-dialog>
  </div>
</template>

<script lang="ts" setup>
import {onMounted, ref} from 'vue'
import {routesApi} from '@/api/routes'

const props = defineProps<{ wizard: any }>()
defineEmits<{ 'update:wizard': [w: any] }>()

const routes = ref<any[]>([])
const detailVisible = ref(false)
const currentRoute = ref<any>(null)
const currentRouteSteps = ref<any[]>([])

async function loadRoutes() {
  try {
    const data = await routesApi.list({page: 1, size: 9999})
    routes.value = (data as any)?.items ?? (Array.isArray(data) ? data : [])
  } catch {
  }
}

async function onRouteChange(rb: any, code: string) {
  const route = routes.value.find((r) => r.routeCode === code)
  if (route && rb) {
    rb.routeName = route.routeName || ''
    rb.routeId = route.id || 0
  }
}

async function viewRouteDetail(rb: any) {
  if (!rb || !rb.routeCode) return
  const route = routes.value.find((r) => r.routeCode === rb.routeCode)
  if (!route) return

  currentRoute.value = route
  try {
    const detail: any = await routesApi.get(route.id)
    currentRouteSteps.value = detail?.steps || []
  } catch {
    currentRouteSteps.value = []
  }
  detailVisible.value = true
}

function addRoute() {
  props.wizard.routeBindings.push({
    routeConfigId: 0,
    routeId: 0,
    routeCode: '',
    routeName: '',
    routeSeq: 10 + props.wizard.routeBindings.length * 10
  })
}

function removeRoute(rb: any) {
  const idx = props.wizard.routeBindings.indexOf(rb)
  if (idx > -1) {
    props.wizard.routeBindings.splice(idx, 1)
  }
}

onMounted(loadRoutes)
</script>

<style lang="scss" scoped>
.route-binding {
  padding: 12px;
  border: 1px dashed #cbd5e1;
  border-radius: 6px;
  margin-bottom: 12px;
}

.route-row {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 8px;
}

.route-label {
  width: 60px;
  text-align: right;
  font-size: 14px;
  color: #606266;
  flex-shrink: 0;
}
</style>