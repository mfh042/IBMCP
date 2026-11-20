import {defineStore} from 'pinia'
import {reactive, ref} from 'vue'
import {alarmsApi} from '@/api/alarms'
import {wsService} from '@/comm/ws'
import type {Alarm} from '@/types/api'

export const useAlarmsStore = defineStore('alarms', () => {
    const activeCount = ref(0)
    const alarmList = reactive<Alarm[]>([])
    let subscribed = false

    function incrementActive() {
        activeCount.value++
    }

    function decrementActive() {
        if (activeCount.value > 0) activeCount.value--
    }

    async function loadActive() {
        const data: any = await alarmsApi.list({state: 1})
        const list: Alarm[] = (data?.items ?? data?.list ?? (Array.isArray(data) ? data : [])) as Alarm[]
        alarmList.splice(0, alarmList.length, ...list)
        activeCount.value = alarmList.length
    }

    function markState(alarmId: number, state: number) {
        const target = alarmList.find((a) => Number(a.id) === alarmId)
        if (target) {
            if (state >= 2) {
                const idx = alarmList.indexOf(target)
                if (idx >= 0) alarmList.splice(idx, 1)
            } else {
                target.state = state
            }
        }
    }

    function subscribeWs() {
        if (subscribed) return
        subscribed = true
        wsService.subscribe('alarm', (payload) => {
            if (!payload) return
            const alarmId = Number(payload.alarmId)
            if (payload.type === 'alarm_triggered') {
                incrementActive()
                alarmList.unshift({
                    id: alarmId,
                    level: payload.level,
                    state: 1,
                    deviceCode: payload.deviceCode,
                    pointAddress: payload.pointAddress,
                    pointName: payload.pointName,
                    message: payload.message,
                    value: payload.value,
                    triggeredAt: payload.triggeredAt,
                } as Alarm)
            } else if (payload.type === 'alarm_acknowledged') {
                decrementActive()
                markState(alarmId, 2)
            } else if (payload.type === 'alarm_recovered') {
                decrementActive()
                markState(alarmId, 3)
            }
        })
    }

    return {activeCount, alarmList, incrementActive, decrementActive, loadActive, markState, subscribeWs}
})