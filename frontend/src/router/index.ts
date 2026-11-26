import {createRouter, createWebHashHistory} from 'vue-router'
import {useAuthStore} from '@/stores/auth'

const routes = [
    {path: '/login', name: 'Login', component: () => import('@/views/auth/LoginView.vue')},
    {path: '/403', component: () => import('@/views/NotFound.vue')},
    {
        path: '/',
        component: () => import('@/layouts/MainLayout.vue'),
        redirect: '/monitoring/dashboard',
        meta: {roles: ['admin', 'engineer', 'operator', 'viewer']},
        children: [
            {
                path: 'monitoring/dashboard',
                name: 'Dashboard',
                component: () => import('@/views/monitoring/DashboardView.vue'),
                meta: {title: '实时监控看板', roles: ['admin', 'engineer', 'operator', 'viewer']},
            },
            {
                path: 'monitoring/trend',
                name: 'Trend',
                component: () => import('@/views/monitoring/TrendView.vue'),
                meta: {title: '历史趋势查询', roles: ['admin', 'engineer', 'operator', 'viewer']},
            },
            {
                path: 'devices',
                name: 'DeviceList',
                component: () => import('@/views/devices/DeviceListView.vue'),
                meta: {title: '设备列表', roles: ['admin', 'engineer', 'operator']},
            },
            {
                path: 'devices/:id',
                name: 'DeviceDetail',
                component: () => import('@/views/devices/DeviceDetailView.vue'),
                meta: {title: '设备详情', roles: ['admin', 'engineer', 'operator']},
            },
            {
                path: 'alarms',
                name: 'AlarmList',
                component: () => import('@/views/alarms/AlarmListView.vue'),
                meta: {title: '活动报警', roles: ['admin', 'engineer', 'operator']},
            },
            {
                path: 'alarms/history',
                name: 'AlarmHistory',
                component: () => import('@/views/alarms/AlarmHistoryView.vue'),
                meta: {title: '报警历史', roles: ['admin', 'engineer', 'operator', 'viewer']},
            },
            {
                path: 'equipment-config',
                name: 'ConfigList',
                component: () => import('@/views/equipment-config/ConfigListView.vue'),
                meta: {title: '装备运行实例', roles: ['admin', 'engineer']},
            },
            {
                path: 'equipment-config/wizard',
                name: 'ConfigWizard',
                component: () => import('@/views/equipment-config/ConfigWizard.vue'),
                meta: {title: '配置向导', roles: ['admin', 'engineer']},
            },
            {
                path: 'equipment-config/:id',
                name: 'ConfigDetail',
                component: () => import('@/views/equipment-config/ConfigDetailView.vue'),
                meta: {title: '配置详情', roles: ['admin', 'engineer']},
            },
            {
                path: 'equipment-config/:id/edit',
                name: 'ConfigEdit',
                component: () => import('@/views/equipment-config/ConfigEditView.vue'),
                meta: {title: '编辑配置', roles: ['admin', 'engineer']},
            },
            {
                path: 'node-red/editor',
                name: 'NodeRedEditor',
                component: () => import('@/views/node-red/NodeRedEditor.vue'),
                meta: {title: 'Node-RED 流编辑器', roles: ['admin', 'engineer']},
            },
            {
                path: 'running/routes/:id',
                name: 'RouteBindDetail',
                component: () => import('@/views/running/RouteBindDetail.vue'),
                meta: {title: '路线绑定详情', roles: ['admin', 'engineer']},
            },
            {
                path: 'running/collections/:id',
                name: 'CollectionEdit',
                component: () => import('@/views/running/CollectionEdit.vue'),
                meta: {title: '采集项编辑', roles: ['admin', 'engineer']},
            },
            {
                path: 'dictionaries/categories',
                component: () => import('@/views/dictionaries/CategoryDict.vue'),
                meta: {title: '设备类别字典', roles: ['admin', 'engineer']},
            },
            {
                path: 'dictionaries/brands',
                component: () => import('@/views/dictionaries/BrandDict.vue'),
                meta: {title: '品牌型号字典', roles: ['admin', 'engineer']},
            },
            {
                path: 'dictionaries/ports',
                component: () => import('@/views/dictionaries/PortDict.vue'),
                meta: {title: '端口字典', roles: ['admin', 'engineer']},
            },
            {
                path: 'dictionaries/protocols',
                component: () => import('@/views/dictionaries/ProtocolDict.vue'),
                meta: {title: '协议字典', roles: ['admin', 'engineer']},
            },
            {
                path: 'dictionaries/data-items',
                component: () => import('@/views/dictionaries/DataItemDict.vue'),
                meta: {title: '开放数据项字典', roles: ['admin', 'engineer']},
            },
            {
                path: 'dictionaries/base-devices',
                component: () => import('@/views/dictionaries/BaseDeviceDict.vue'),
                meta: {title: '设备主数据', roles: ['admin', 'engineer']},
            },
            {
                path: 'dictionaries/equipment-functions',
                component: () => import('@/views/dictionaries/EquipmentFuncDict.vue'),
                meta: {title: '装备功能字典', roles: ['admin', 'engineer']},
            },
            {
                path: 'routes',
                name: 'RouteList',
                component: () => import('@/views/routes/RouteListView.vue'),
                meta: {title: '工艺路线', roles: ['admin', 'engineer']},
            },
            {
                path: 'routes/:code',
                name: 'RouteDetail',
                component: () => import('@/views/routes/RouteDetailView.vue'),
                meta: {title: '路线详情', roles: ['admin', 'engineer']},
            },
            {
                path: 'equipments',
                name: 'EquipmentList',
                component: () => import('@/views/equipments/EquipmentListView.vue'),
                meta: {title: '装备主数据', roles: ['admin', 'engineer']},
            },
            {
                path: 'system/users',
                name: 'UserManage',
                component: () => import('@/views/system/UserManage.vue'),
                meta: {title: '用户管理', roles: ['admin']},
            },
            {
                path: 'system/roles',
                name: 'RoleManage',
                component: () => import('@/views/system/RoleManage.vue'),
                meta: {title: '角色管理', roles: ['admin']},
            },
            {
                path: 'system/api-settings',
                component: () => import('@/views/system/ApiSettings.vue'),
                meta: {title: 'API 设置', roles: ['admin']},
            },
        ],
    },
    {path: '/:pathMatch(.*)*', redirect: '/monitoring/dashboard'},
]

const router = createRouter({
    history: createWebHashHistory(),
    routes,
})

router.beforeEach((to, from, next) => {
    if (to.path === '/login') return next()
    const auth = useAuthStore()
    if (!auth.isLoggedIn) return next('/login')
    const roles = to.meta?.roles as string[] | undefined
    if (roles && !roles.some((r: string) => auth.hasRole(r))) {
        return next('/403')
    }
    next()
})

export default router