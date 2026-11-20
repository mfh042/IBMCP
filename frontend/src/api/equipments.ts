import {httpService} from '@/comm/http'

export const equipmentsApi = {
    list: (params?: { page?: number; size?: number; keyword?: string; customerCode?: string }) =>
        httpService.get('/api/equipments', {params}),
    all: () => httpService.get('/api/equipments/all'),
    get: (id: number) => httpService.get(`/api/equipments/${id}`),
    getByCode: (code: string) => httpService.get(`/api/equipments/code/${code}`),
    create: (body: any) => httpService.post('/api/equipments', body),
    update: (id: number, body: any) => httpService.put(`/api/equipments/${id}`, body),
    remove: (id: number) => httpService.delete(`/api/equipments/${id}`),

    functionList: (params?: { page?: number; size?: number; keyword?: string }) =>
        httpService.get('/api/equipments/functions', {params}),
    functionAll: () => httpService.get('/api/equipments/functions/all'),
    functionGet: (id: number) => httpService.get(`/api/equipments/functions/${id}`),
    functionCreate: (body: any) => httpService.post('/api/equipments/functions', body),
    functionUpdate: (id: number, body: any) => httpService.put(`/api/equipments/functions/${id}`, body),
    functionRemove: (id: number) => httpService.delete(`/api/equipments/functions/${id}`),
}