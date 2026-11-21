import {httpService} from '@/comm/http'
import type {CollectorStatus, PingResult} from '@/types/api'

export const systemApi = {
    ping: () => httpService.get<unknown, PingResult>('/api/v1/ping'),
    collectorStatus: () => httpService.get<unknown, CollectorStatus>('/api/v1/collector/health'),
}
