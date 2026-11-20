import {defineStore} from 'pinia'
import {computed, reactive, ref} from 'vue'
import {wsService} from '@/comm/ws'
import type {ControlResult, RealTimePointData} from '@/types/api'

export const useWsStore = defineStore('ws', () => {
    const realTimeData = reactive<Record<number, RealTimePointData>>({})
    const deviceStatus = reactive<Record<number, number>>({})
    const listenedPoints = reactive<Set<number>>(new Set())
    const dataSubscribed = ref(false)
    const controlSubscribed = ref(false)
    const lastControlResult = ref<ControlResult | null>(null)
    const connectionTick = ref(0)

    const isConnected = computed(() => {
        void connectionTick.value
        return wsService.isConnected
    })

    wsService.onConnectionChange(() => {
        connectionTick.value++
    })

    function onDataPayload(payload: any) {
        if (!payload || payload.type !== 'data') return
        const pointId = Number(payload.pointId)
        if (!Number.isFinite(pointId)) return
        realTimeData[pointId] = {
            value: payload.value,
            quality: payload.quality,
            ts: payload.ts,
            error: payload.error,
        }
    }

    function ensureDataSubscription() {
        if (dataSubscribed.value) return
        dataSubscribed.value = true
        wsService.subscribe('data', onDataPayload)
    }

    function subscribePoint(pointId: number) {
        ensureDataSubscription()
        listenedPoints.add(pointId)
    }

    function unsubscribePoint(pointId: number) {
        listenedPoints.delete(pointId)
        delete realTimeData[pointId]
    }

    function subscribeDevice() {
        wsService.subscribe('device', (payload) => {
            if (!payload || payload.type !== 'device_status') return
            const deviceId = Number(payload.deviceId)
            if (Number.isFinite(deviceId)) deviceStatus[deviceId] = Number(payload.status)
        })
    }

    function subscribeControl() {
        if (controlSubscribed.value) return
        controlSubscribed.value = true
        wsService.subscribe('control', (payload) => {
            if (!payload || payload.type !== 'control_finished') return
            lastControlResult.value = {
                commandId: payload.commandId,
                success: !!payload.success,
                message: payload.message ?? '',
            }
        })
    }

    function getPointData(pointId: number) {
        return realTimeData[pointId] ?? null
    }

    return {
        realTimeData,
        deviceStatus,
        listenedPoints,
        dataSubscribed,
        controlSubscribed,
        lastControlResult,
        isConnected,
        subscribePoint,
        unsubscribePoint,
        subscribeDevice,
        subscribeControl,
        getPointData,
    }
})