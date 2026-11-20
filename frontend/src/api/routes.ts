import {httpService} from '@/comm/http'

export const routesApi = {
    list: (params?: { page?: number; size?: number; keyword?: string; status?: number }) =>
        httpService.get('/api/routes', {params}),
    all: () => httpService.get('/api/routes/all'),
    get: (id: number) => httpService.get(`/api/routes/${id}`),
    getByCode: (code: string) => httpService.get(`/api/routes/code/${code}`),
    create: (body: any) => httpService.post('/api/routes', body),
    update: (id: number, body: any) => httpService.put(`/api/routes/${id}`, body),
    remove: (id: number) => httpService.delete(`/api/routes/${id}`),

    stepList: (params?: { page?: number; size?: number; routeCode?: string }) =>
        httpService.get('/api/routes/steps', {params}),
    stepGet: (id: number) => httpService.get(`/api/routes/steps/${id}`),
    stepCreate: (body: any) => httpService.post('/api/routes/steps', body),
    stepUpdate: (id: number, body: any) => httpService.put(`/api/routes/steps/${id}`, body),
    stepRemove: (id: number) => httpService.delete(`/api/routes/steps/${id}`),
}