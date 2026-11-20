import {CommConfig} from './config'
import {EnvDetector} from './env'
import {IbmcpWsClient} from '@/ws/IbmcpWsClient'

class WsService {
    private client = new IbmcpWsClient()
    private pollTimer: number | null = null

    get isConnected(): boolean {
        return this.client.isConnected
    }

    init() {
        const url = CommConfig.wsUrl
        console.log(`[WsService] connecting → ${url} (${EnvDetector.runtimeLabel})`)
        this.client.connect(url)
        this.startDegradeWatchdog()
    }

    reconnect() {
        this.client.disconnect()
        this.client.connect(CommConfig.wsUrl)
    }

    onConnectionChange(cb: (connected: boolean) => void) {
        return this.client.onConnectionChange(cb)
    }

    subscribe(topic: string, cb: (payload: any) => void) {
        this.client.subscribe(topic, cb)
    }

    unsubscribe(topic: string, cb: (payload: any) => void) {
        this.client.unsubscribe(topic, cb)
    }

    private startDegradeWatchdog() {
        setInterval(() => {
            if (!this.client.isConnected && !this.pollTimer) {
                console.warn('[WsService] WebSocket 断开，降级为 HTTP 轮询')
                this.startPollingFallback()
            }
            if (this.client.isConnected && this.pollTimer) {
                console.log('[WsService] WebSocket 重连成功，停止轮询兜底')
                this.stopPollingFallback()
            }
        }, 5000)
    }

    private startPollingFallback() {
        this.pollTimer = window.setInterval(() => {
        }, 2000)
    }

    private stopPollingFallback() {
        if (this.pollTimer) {
            clearInterval(this.pollTimer)
            this.pollTimer = null
        }
    }
}

export const wsService = new WsService()