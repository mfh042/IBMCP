import {invoke} from '@tauri-apps/api/core'

export interface ApiConfig {
    api_addr: string
    ws_addr: string
}

export const tauriCommands = {
    async getApiConfig(): Promise<ApiConfig> {
        return invoke<ApiConfig>('get_api_config')
    },

    async setApiConfig(apiAddr: string, wsAddr: string): Promise<void> {
        return invoke<void>('set_api_config', {apiAddr, wsAddr})
    },

    async checkBackendHealth(url: string): Promise<boolean> {
        return invoke<boolean>('check_backend_health', {url})
    },
}