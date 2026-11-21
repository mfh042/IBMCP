import {httpService} from '@/comm/http'

export const devicesApi = {
    list: (params?: { page?: number; size?: number; keyword?: string }) =>
        httpService.get('/api/v1/runtime', {params}),
    get: (id: number) => httpService.get(`/api/v1/runtime/${id}`),
    getPoints: (id: number) => httpService.get(`/api/v1/runtime/${id}/points`),
    create: (body: any) => httpService.post('/api/v1/runtime', body),
    update: (id: number, body: any) => httpService.put(`/api/v1/runtime/${id}`, body),
    remove: (id: number) => httpService.delete(`/api/v1/runtime/${id}`),
    control: (id: number, body: { pointId: number; pointName: string; value: string }) =>
        httpService.post(`/api/v1/runtime/${id}/control`, {
            point_id: body.pointId,
            point_name: body.pointName,
            value: body.value,
        }),
    commands: (id: number) => httpService.get(`/api/v1/control/commands/${id}`),
}
