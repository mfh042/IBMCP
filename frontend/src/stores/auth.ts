import {defineStore} from 'pinia'
import type {UserInfo} from '@/types/api'

function tryDecodeJwt(token: string): Record<string, any> | null {
    try {
        const parts = token.split('.')
        if (parts.length < 2) return null
        const pad = parts[1].replace(/-/g, '+').replace(/_/g, '/')
        const body = atob(pad + '==='.slice((pad.length + 3) % 4))
        return JSON.parse(body)
    } catch {
        return null
    }
}

export const useAuthStore = defineStore('auth', {
    state: () => ({
        token: localStorage.getItem('ibmc_token') || '',
        userInfo: (JSON.parse(localStorage.getItem('ibmc_user') || 'null') as UserInfo | null),
    }),
    getters: {
        isLoggedIn: (s) => !!s.token,
        hasRole: (s) => (role: string) => s.userInfo?.roles?.includes(role) ?? false,
    },
    actions: {
        async login(username: string, password: string) {
            const {authApi} = await import('@/api/auth')
            const data = await authApi.login({username, password})
            const payload = tryDecodeJwt(data.token)
            this.token = data.token
            this.userInfo = {
                id: payload?.uid ?? 0,
                username: data.username ?? username,
                displayName: data.displayName ?? '',
                roles: data.roles ?? [],
            }
            localStorage.setItem('ibmc_token', this.token)
            localStorage.setItem('ibmc_user', JSON.stringify(this.userInfo))
        },
        logout() {
            this.token = ''
            this.userInfo = null
            localStorage.removeItem('ibmc_token')
            localStorage.removeItem('ibmc_user')
        },
        restoreFromLocalStorage() {
            this.token = localStorage.getItem('ibmc_token') || ''
            this.userInfo = JSON.parse(localStorage.getItem('ibmc_user') || 'null')
        },
    },
})