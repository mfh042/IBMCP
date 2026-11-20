type PayloadHandler = (payload: any) => void

export type IbmcpTopic = 'data' | 'device' | 'alarm' | 'control'

const HEARTBEAT_INTERVAL = 30000
const MAX_MISSED_PONG = 2
const RECONNECT_BASE_DELAY = 1000
const RECONNECT_MAX_DELAY = 30000

export class IbmcpWsClient {
    private ws: WebSocket | null = null
    private subscriptions = new Map<string, Set<PayloadHandler>>()
    private reconnectTimer: number | null = null
    private heartbeatTimer: number | null = null
    private missedPongs = 0
    private url = ''
    private reconnectDelay = RECONNECT_BASE_DELAY
    private manuallyClosed = false
    private deviceStatusHandlers = new Set<PayloadHandler>()
    private connectionHandlers = new Set<(connected: boolean) => void>()

    get isConnected(): boolean {
        return this.ws?.readyState === WebSocket.OPEN
    }

    onConnectionChange(cb: (connected: boolean) => void) {
        this.connectionHandlers.add(cb)
        return () => this.connectionHandlers.delete(cb)
    }

    connect(url: string) {
        this.url = url
        this.reconnectDelay = RECONNECT_BASE_DELAY
        this.manuallyClosed = false
        this.openSocket()
    }

    disconnect() {
        this.manuallyClosed = true
        this.stopHeartbeat()
        this.clearReconnectTimer()
        if (this.ws) {
            this.ws.onopen = null
            this.ws.onmessage = null
            this.ws.onclose = null
            this.ws.onerror = null
            this.ws.close()
            this.ws = null
        }
    }

    subscribe(topic: IbmcpTopic | string, cb: PayloadHandler) {
        const handlers = this.subscriptions.get(topic)
        if (handlers) {
            if (handlers.has(cb)) return
            handlers.add(cb)
        } else {
            this.subscriptions.set(topic, new Set([cb]))
            this.send({action: 'subscribe', topic})
        }
    }

    unsubscribe(topic: IbmcpTopic | string, cb: PayloadHandler) {
        const handlers = this.subscriptions.get(topic)
        if (!handlers) return
        handlers.delete(cb)
        if (handlers.size === 0) {
            this.subscriptions.delete(topic)
            this.send({action: 'unsubscribe', topic})
        }
    }

    private notifyConnection(connected: boolean) {
        this.connectionHandlers.forEach((cb) => {
            try {
                cb(connected)
            } catch (e) {
                console.error('[IbmcpWsClient] connection handler error:', e)
            }
        })
    }

    private openSocket() {
        if (!this.url) return
        try {
            this.ws = new WebSocket(this.url)
        } catch (e) {
            console.error('[IbmcpWsClient] invalid ws url:', this.url, e)
            this.scheduleReconnect()
            return
        }

        this.ws.onopen = () => {
            console.log('[IbmcpWsClient] connected →', this.url)
            this.reconnectDelay = RECONNECT_BASE_DELAY
            this.missedPongs = 0
            this.clearReconnectTimer()
            this.startHeartbeat()
            this.resubscribeAll()
            this.notifyConnection(true)
        }

        this.ws.onmessage = (e) => {
            this.onMessage(e.data)
        }

        this.ws.onclose = () => {
            console.warn('[IbmcpWsClient] closed')
            this.stopHeartbeat()
            this.notifyConnection(false)
            if (!this.manuallyClosed) this.scheduleReconnect()
        }

        this.ws.onerror = () => {
            this.ws?.close()
        }
    }

    private send(obj: Record<string, any>) {
        if (this.isConnected) {
            this.ws!.send(JSON.stringify(obj))
        }
    }

    private onMessage(raw: string) {
        let envelope: any
        try {
            envelope = JSON.parse(raw)
        } catch {
            console.warn('[IbmcpWsClient] non-JSON message:', raw)
            return
        }
        if (!envelope || typeof envelope !== 'object') return

        if (envelope.type === 'pong') {
            this.missedPongs = 0
            return
        }
        if (envelope.type === 'hello') {
            console.log('[IbmcpWsClient] hello:', envelope.service ?? 'ibmcp')
            return
        }
        if (envelope.type === 'subscribed' || envelope.type === 'unsubscribed') {
            return
        }
        if (envelope.error) {
            console.warn('[IbmcpWsClient] server error:', envelope.error)
            return
        }

        const topic = envelope.topic as string | undefined
        if (!topic) return

        let payload = envelope.payload ?? null
        if (typeof payload === 'string') {
            try {
                payload = JSON.parse(payload)
            } catch {
                console.warn('[IbmcpWsClient] invalid payload string:', topic)
                return
            }
        }
        if (!payload || typeof payload !== 'object') return

        this.dispatch(topic, payload)

        if (topic === 'device' && payload.type === 'device_status') {
            this.deviceStatusHandlers.forEach((cb) => {
                try {
                    cb(payload)
                } catch (e) {
                    console.error('[IbmcpWsClient] device handler error:', e)
                }
            })
        }
    }

    private dispatch(topic: string, payload: any) {
        const handlers = this.subscriptions.get(topic)
        if (!handlers) return
        handlers.forEach((cb) => {
            try {
                cb(payload)
            } catch (e) {
                console.error('[IbmcpWsClient] handler error:', e)
            }
        })
    }

    private scheduleReconnect() {
        if (this.reconnectTimer) return
        if (!this.url) return
        const delay = this.reconnectDelay
        this.reconnectTimer = window.setTimeout(() => {
            this.reconnectTimer = null
            console.log(`[IbmcpWsClient] reconnecting in ${delay}ms...`)
            this.openSocket()
            this.reconnectDelay = Math.min(this.reconnectDelay * 2, RECONNECT_MAX_DELAY)
        }, delay)
    }

    private clearReconnectTimer() {
        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer)
            this.reconnectTimer = null
        }
    }

    private startHeartbeat() {
        this.stopHeartbeat()
        this.heartbeatTimer = window.setInterval(() => {
            if (!this.isConnected) return
            if (this.missedPongs >= MAX_MISSED_PONG) {
                console.warn('[IbmcpWsClient] heartbeat timeout, reconnecting')
                this.missedPongs = 0
                this.ws?.close()
                return
            }
            this.missedPongs++
            this.send({action: 'ping'})
        }, HEARTBEAT_INTERVAL)
    }

    private stopHeartbeat() {
        if (this.heartbeatTimer) {
            clearInterval(this.heartbeatTimer)
            this.heartbeatTimer = null
        }
        this.missedPongs = 0
    }

    private resubscribeAll() {
        this.subscriptions.forEach((_handlers, topic) => {
            this.send({action: 'subscribe', topic})
        })
    }
}

export const wsClient = new IbmcpWsClient()