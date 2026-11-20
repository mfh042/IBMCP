import axios from 'axios'
import {CommConfig} from './config'

const IS_DEV = import.meta.env.DEV

const RAW_JSON_PATHS = ['/api/ping', '/api/collector/status']

function isRawJsonPath(url?: string): boolean {
    if (!url) return false
    const path = url.split('?')[0]
    return RAW_JSON_PATHS.some((p) => path.endsWith(p))
}

export const httpService = axios.create({
    baseURL: CommConfig.apiBase,
    timeout: 10000,
}) as unknown as Omit<typeof axios, 'get' | 'post' | 'put' | 'delete' | 'patch'> & {
    get<T = any, R = T>(url: string, config?: any): Promise<R>
    post<T = any, R = T>(url: string, data?: any, config?: any): Promise<R>
    put<T = any, R = T>(url: string, data?: any, config?: any): Promise<R>
    delete<T = any, R = T>(url: string, config?: any): Promise<R>
    patch<T = any, R = T>(url: string, data?: any, config?: any): Promise<R>
}

httpService.interceptors.request.use((config) => {
    const token = localStorage.getItem('ibmc_token')
    if (token) config.headers.Authorization = `Bearer ${token}`
    if (IS_DEV) {
        console.log(`[HTTP→] ${config.method?.toUpperCase()} ${config.baseURL ?? ''}${config.url}`)
    }
    return config
})

httpService.interceptors.response.use(
    (resp) => {
        const raw: any = resp.data
        if (IS_DEV) {
            console.log(`[HTTP←] ${resp.config.url} =>`, raw)
        }

        if (!raw || typeof raw !== 'object') {
            return raw
        }

        if (isRawJsonPath(resp.config.url)) {
            return raw
        }

        const {code, message, data} = raw

        if (code === 0 || code === 200) {
            let payload = data ?? null
            if (payload && typeof payload === 'object' && Array.isArray(payload.items) && !Array.isArray(payload.list)) {
                payload = {...payload, list: payload.items}
            }
            return payload
        }

        if (code === 401) {
            localStorage.removeItem('ibmc_token')
            localStorage.removeItem('ibmc_user')
            if (location.pathname !== '/login') {
                window.location.href = '/login'
            }
            return Promise.reject(new Error(message || '未授权，请重新登录'))
        }

        return Promise.reject(new Error(message || `请求失败 (code=${code})`))
    },
    (err) => {
        if (IS_DEV) {
            console.error('[HTTP Network Error]', err?.config?.url ?? '', err.message)
        }
        if (err.response) {
            const body = err.response.data
            if (body && typeof body === 'object') {
                return Promise.reject(new Error(body.message || `HTTP ${err.response.status}`))
            }
            return Promise.reject(new Error(`HTTP ${err.response.status}`))
        }
        if (err.code === 'ECONNABORTED') {
            return Promise.reject(new Error('请求超时，请检查后端服务'))
        }
        return Promise.reject(new Error(err.message || '网络连接失败'))
    },
)