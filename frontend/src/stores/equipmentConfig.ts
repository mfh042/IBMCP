import {defineStore} from 'pinia'
import {ref} from 'vue'
import {equipmentConfigApi} from '@/api/equipmentConfig'
import type {EquipmentConfig, RouteExpandResult} from '@/types/api'

export const useEquipmentConfigStore = defineStore('equipmentConfig', () => {
    const configList = ref<EquipmentConfig[]>([])
    const currentConfig = ref<RouteExpandResult | null>(null)
    const loading = ref(false)

    async function loadList() {
        loading.value = true
        try {
            const data = await equipmentConfigApi.list()
            const raw: any = data
            configList.value = raw?.items ?? raw?.list ?? (Array.isArray(raw) ? raw : [])
        } finally {
            loading.value = false
        }
    }

    async function loadExpand(id: number) {
        currentConfig.value = await equipmentConfigApi.expand(id)
    }

    function clearCurrent() {
        currentConfig.value = null
    }

    return {configList, currentConfig, loading, loadList, loadExpand, clearCurrent}
})