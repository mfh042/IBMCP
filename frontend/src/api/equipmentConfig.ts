import {httpService} from '@/comm/http'

export const equipmentConfigApi = {
    list: (params?: { page?: number; size?: number; keyword?: string; customerCode?: string; configStatus?: number }) =>
        httpService.get('/api/v1/config/equipments', {params}),
    get: (id: number) => httpService.get(`/api/v1/config/equipments/${id}`),
    getByCode: (code: string) => httpService.get(`/api/v1/config/equipments/code/${code}`),
    create: (body: any) => httpService.post('/api/v1/config/equipments', body),
    update: (id: number, body: any) => httpService.put(`/api/v1/config/equipments/${id}`, body),
    remove: (id: number) => httpService.delete(`/api/v1/config/equipments/${id}`),
    expand: (id: number) => httpService.get(`/api/v1/config/equipments/${id}/expand`),

    routesOf: (configId: number) => httpService.get('/api/v1/config/routes', {params: {config_id: configId}}),
    bindRoute: (configId: number, body: any) =>
        httpService.post('/api/v1/config/routes', {config_id: configId, ...body}),
    updateRoute: (id: number, body: any) => httpService.put(`/api/v1/config/routes/${id}`, body),
    removeRoute: (id: number) => httpService.delete(`/api/v1/config/routes/${id}`),

    functionsOf: (configId: number) => httpService.get('/api/v1/config/functions', {params: {config_id: configId}}),
    bindFunction: (configId: number, body: any) =>
        httpService.post('/api/v1/config/functions', {config_id: configId, ...body}),
    updateFunction: (id: number, body: any) => httpService.put(`/api/v1/config/functions/${id}`, body),
    removeFunction: (id: number) => httpService.delete(`/api/v1/config/functions/${id}`),
}
