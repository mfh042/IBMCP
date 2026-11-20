import {httpService} from '@/comm/http'

export const alarmsApi = {
    list: (params?: { page?: number; size?: number; state?: number }) =>
        httpService.get('/api/alarms', {params}),
    ack: (id: number) => httpService.put(`/api/alarms/${id}/ack`),
    recover: (id: number) => httpService.put(`/api/alarms/${id}/recover`),
}