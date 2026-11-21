import {httpService} from '@/comm/http'

export const alarmsApi = {
    list: (params?: { page?: number; size?: number; state?: number }) =>
        httpService.get('/api/v1/alarms/list', {params}),
    ack: (id: number) => httpService.put(`/api/v1/alarms/${id}/ack`),
    recover: (id: number) => httpService.put(`/api/v1/alarms/${id}/recover`),
}
