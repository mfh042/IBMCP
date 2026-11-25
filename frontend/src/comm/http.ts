import axios from 'axios'
import {CommConfig} from './config'

const IS_DEV = import.meta.env.DEV

// 后端 ResultEnvelopeMiddleware 对所有业务 JSON 统一包膜为 {code,message,data}，
// 仅 /openapi.json /docs /redoc 不包膜（前端不直接请求这些），因此无需特殊 raw 路径。
const RAW_JSON_PATHS: string[] = []

function isRawJsonPath(url?: string): boolean {
    if (!url) return false
    const path = url.split('?')[0]
    return RAW_JSON_PATHS.some((p) => path.endsWith(p))
}

function withApiVersion(url?: string): string | undefined {
    if (!url || !url.startsWith('/api/') || url.startsWith('/api/v1/')) return url
    return `/api/v1${url.slice('/api'.length)}`
}

function snakeToCamelKey(key: string): string {
    return key.replace(/_([a-z])/g, (_, b) => b.toUpperCase())
}

function camelToSnakeKey(key: string): string {
    return key.replace(/([A-Z])/g, (_, c) => '_' + c.toLowerCase())
}

function isPlainObject(v: any): boolean {
    return v !== null && typeof v === 'object' && !Array.isArray(v) && !(v instanceof Date) && !(v instanceof FormData)
}

function convertKeysDeep(obj: any, fn: (k: string) => string): any {
    if (Array.isArray(obj)) {
        return obj.map((v) => convertKeysDeep(v, fn))
    }
    if (isPlainObject(obj)) {
        const out: Record<string, any> = {}
        for (const k of Object.keys(obj)) {
            out[fn(k)] = convertKeysDeep(obj[k], fn)
        }
        return out
    }
    return obj
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
    config.url = withApiVersion(config.url)
    const token = localStorage.getItem('ibmc_token')
    if (token) config.headers.Authorization = `Bearer ${token}`
    if (isPlainObject(config.data)) {
        config.data = convertKeysDeep(config.data, camelToSnakeKey)
    }
    if (isPlainObject(config.params)) {
        config.params = convertKeysDeep(config.params, camelToSnakeKey)
    }
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
            if (payload && typeof payload === 'object') {
                payload = convertKeysDeep(payload, snakeToCamelKey)
                if (Array.isArray(payload.items) && !Array.isArray(payload.list)) {
                    payload = {...payload, list: payload.items}
                }
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