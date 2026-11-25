import {defineStore} from 'pinia'
import {computed, reactive, ref} from 'vue'
import {wsService} from '@/comm/ws'
import type {ControlResult, RealTimePointData} from '@/types/api'

export const useWsStore = defineStore('ws', () => {
    // 键：采集计划点位主键（running_device_collection.id = using_points.using_point_id = WS data 帧 point_id）
    const realTimeData = reactive<Record<number, RealTimePointData>>({})
    // 键：device_code（字符串），后端运行时无数字主键
    const deviceStatus = reactive<Record<string, number>>({})
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

    // 后端 data 帧：{"type":"data","ts":<int>,"count":<int>,"samples":[{device_code,point_id,value,quality}...]}
    // WS 帧不经过 axios 拦截器，字段保持 snake_case。
    function onDataPayload(payload: any) {
        if (!payload || payload.type !== 'data') return
        const samples = Array.isArray(payload.samples) ? payload.samples : []
        const ts = Number(payload.ts)
        for (const s of samples) {
            if (!s) continue
            const pointId = Number(s.point_id)
            if (!Number.isFinite(pointId)) continue
            realTimeData[pointId] = {
                value: s.value,
                quality: s.quality,
                ts: Number.isFinite(ts) ? ts : undefined,
                error: s.error,
            }
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
            const code = payload.device_code
            if (typeof code === 'string' && code) deviceStatus[code] = Number(payload.status)
        })
    }

    function subscribeControl() {
        if (controlSubscribed.value) return
        controlSubscribed.value = true
        wsService.subscribe('control', (payload) => {
            if (!payload || payload.type !== 'control_finished') return
            lastControlResult.value = {
                commandId: payload.command_id,
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