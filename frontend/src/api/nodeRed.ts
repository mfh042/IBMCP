import {httpService} from '@/comm/http'

export interface NodeRedStatus {
    container: string
    editorReachable: boolean
    updatedAt?: number
}

export const nodeRedApi = {
    getStatus: () => httpService.get<unknown, NodeRedStatus>('/api/v1/node-red/status'),
    start: () => httpService.post<unknown, NodeRedStatus>('/api/v1/node-red/start'),
    stop: () => httpService.post<unknown, NodeRedStatus>('/api/v1/node-red/stop'),
    restart: () => httpService.post<unknown, NodeRedStatus>('/api/v1/node-red/restart'),
}
