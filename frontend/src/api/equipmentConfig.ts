import {httpService} from '@/comm/http'

export const equipmentConfigApi = {
    list: (params?: { page?: number; size?: number; keyword?: string; customerCode?: string; configStatus?: number }) =>
        httpService.get('/api/running/configs', {params}),
    get: (id: number) => httpService.get(`/api/running/configs/${id}`),
    getByCode: (code: string) => httpService.get(`/api/running/configs/code/${code}`),
    create: (body: any) => httpService.post('/api/running/configs', body),
    update: (id: number, body: any) => httpService.put(`/api/running/configs/${id}`, body),
    remove: (id: number) => httpService.delete(`/api/running/configs/${id}`),
    expand: (id: number) => httpService.get(`/api/running/configs/${id}/expand`),

    routesOf: (configId: number) => httpService.get(`/api/running/configs/${configId}/routes`),
    bindRoute: (configId: number, body: any) => httpService.post(`/api/running/configs/${configId}/routes`, body),
    updateRoute: (id: number, body: any) => httpService.put(`/api/running/routes/${id}`, body),
    removeRoute: (id: number) => httpService.delete(`/api/running/routes/${id}`),

    functionsOf: (configId: number) => httpService.get(`/api/running/configs/${configId}/functions`),
    bindFunction: (configId: number, body: any) => httpService.post(`/api/running/configs/${configId}/functions`, body),
    updateFunction: (id: number, body: any) => httpService.put(`/api/running/functions/${id}`, body),
    removeFunction: (id: number) => httpService.delete(`/api/running/functions/${id}`),
}