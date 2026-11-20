import {EnvDetector} from './env'

const LS_API_ADDR = 'ibmcp.apiAddr'
const LS_WS_ADDR = 'ibmcp.wsAddr'

function getUserOverride(): { api?: string; ws?: string } {
    return {
        api: localStorage.getItem(LS_API_ADDR) || undefined,
        ws: localStorage.getItem(LS_WS_ADDR) || undefined,
    }
}

export const CommConfig = {
    get apiBase(): string {
        const env = import.meta.env.VITE_API_BASE as string | undefined
        if (env) return env

        const override = getUserOverride()
        if (override.api) return override.api

        if (EnvDetector.isTauri) {
            return 'http://127.0.0.1:8080'
        }
        return ''
    },

    get wsUrl(): string {
        const env = import.meta.env.VITE_WS_URL as string | undefined
        if (env) return env

        const override = getUserOverride()
        if (override.ws) return override.ws

        if (EnvDetector.isTauri) {
            return 'ws://127.0.0.1:8080/ws/push'
        }
        const proto = location.protocol === 'https:' ? 'wss:' : 'ws:'
        return `${proto}//${location.host}/ws/push`
    },

    setUserApiAddr(addr: string | null) {
        if (addr) localStorage.setItem(LS_API_ADDR, addr)
        else localStorage.removeItem(LS_API_ADDR)
    },
    setUserWsAddr(addr: string | null) {
        if (addr) localStorage.setItem(LS_WS_ADDR, addr)
        else localStorage.removeItem(LS_WS_ADDR)
    },
}