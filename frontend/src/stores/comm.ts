import {defineStore} from 'pinia'
import {EnvDetector} from '@/comm/env'
import {CommConfig} from '@/comm/config'

export const useCommStore = defineStore('comm', {
    state: () => ({
        wsConnected: false,
        httpLatency: 0,
        lastError: '' as string,
        runtimeLabel: EnvDetector.runtimeLabel,
        currentApiBase: CommConfig.apiBase || '(相对路径 /api)',
        currentWsUrl: CommConfig.wsUrl,
    }),
    getters: {
        statusTag: (s) =>
            s.wsConnected
                ? {type: 'success' as const, label: '实时连接'}
                : {type: 'danger' as const, label: '已断开'},
    },
    actions: {
        setWsConnected(v: boolean) {
            this.wsConnected = v
        },
        recordLatency(ms: number) {
            this.httpLatency = ms
        },
        setError(msg: string) {
            this.lastError = msg
        },
    },
})