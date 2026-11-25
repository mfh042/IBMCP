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

    function recomputeActive() {
        activeCount.value = alarmList.filter((a) => a.state === 1).length
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
            recomputeActive()
        }
    }

    // 后端 alarm 帧：{"type":"alarm","events":[{device_code,point_address,point_name,level,state,value,triggered_at|recovered_at}...]}
    // WS 帧不经过 axios 拦截器，字段保持 snake_case；事件无 DB 主键，用 (device_code, point_address) 匹配。
    function subscribeWs() {
        if (subscribed) return
        subscribed = true
        wsService.subscribe('alarm', (payload) => {
            if (!payload || payload.type !== 'alarm') return
            const events = Array.isArray(payload.events) ? payload.events : []
            for (const ev of events) {
                if (!ev) continue
                const code = ev.device_code
                const addr = ev.point_address
                const state = Number(ev.state)
                if (state === 1) {
                    const item: Alarm = {
                        id: `${code}::${addr ?? ''}::${ev.triggered_at ?? ''}`,
                        level: Number(ev.level),
                        state: 1,
                        deviceCode: code,
                        pointAddress: addr,
                        pointName: ev.point_name,
                        message: ev.message ?? '',
                        value: ev.value,
                        triggeredAt: ev.triggered_at,
                    }
                    const idx = alarmList.findIndex((a) => a.deviceCode === code && a.pointAddress === addr)
                    if (idx >= 0) alarmList.splice(idx, 1, item)
                    else alarmList.unshift(item)
                } else if (state === 3) {
                    const idx = alarmList.findIndex((a) => a.deviceCode === code && a.pointAddress === addr)
                    if (idx >= 0) alarmList.splice(idx, 1)
                }
            }
            recomputeActive()
        })
    }

    return {activeCount, alarmList, incrementActive, decrementActive, loadActive, markState, subscribeWs}
})