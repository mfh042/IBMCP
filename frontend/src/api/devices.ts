import {httpService} from '@/comm/http'

export const devicesApi = {
    list: (params?: { page?: number; size?: number; keyword?: string }) =>
        httpService.get('/api/devices', {params}),
    get: (id: number) => httpService.get(`/api/devices/${id}`),
    getPoints: (id: number) => httpService.get(`/api/devices/${id}/points`),
    create: (body: any) => httpService.post('/api/devices', body),
    update: (id: number, body: any) => httpService.put(`/api/devices/${id}`, body),
    remove: (id: number) => httpService.delete(`/api/devices/${id}`),
    control: (id: number, body: { pointId: number; pointName: string; value: string }) =>
        httpService.post(`/api/devices/${id}/control`, body),
    commands: (id: number) => httpService.get(`/api/devices/${id}/commands`),
}