import {defineStore} from 'pinia'
import {devicesApi} from '@/api/devices'
import type {Device} from '@/types/api'

export const useDevicesStore = defineStore('devices', {
    state: () => ({
        deviceList: [] as Device[],
        currentDevice: null as Device | null,
        loading: false,
    }),
    actions: {
        async loadList(params?: { page?: number; size?: number; keyword?: string }) {
            this.loading = true
            try {
                const data = await devicesApi.list(params)
                this.deviceList = (data as any)?.list ?? (Array.isArray(data) ? data : [])
            } finally {
                this.loading = false
            }
        },
        setCurrent(device: Device | null) {
            this.currentDevice = device
        },
    },
})