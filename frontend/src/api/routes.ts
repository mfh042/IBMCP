import {httpService} from '@/comm/http'

export const routesApi = {
    list: (params?: { page?: number; size?: number; keyword?: string; status?: number }) =>
        httpService.get('/api/v1/static/process_routes', {params}),
    all: () => httpService.get('/api/v1/static/process_routes/all'),
    get: (id: number) => httpService.get(`/api/v1/static/process_routes/${id}`),
    getByCode: (code: string) => httpService.get(`/api/v1/static/process_routes/code/${code}`),
    create: (body: any) => httpService.post('/api/v1/static/process_routes', body),
    update: (id: number, body: any) => httpService.put(`/api/v1/static/process_routes/${id}`, body),
    remove: (id: number) => httpService.delete(`/api/v1/static/process_routes/${id}`),

    stepList: (params?: { page?: number; size?: number; routeCode?: string }) =>
        httpService.get('/api/v1/static/process_steps', {
            params: params?.routeCode ? {page: params.page, size: params.size, route_code: params.routeCode} : params,
        }),
    stepGet: (id: number) => httpService.get(`/api/v1/static/process_steps/${id}`),
    stepCreate: (body: any) => httpService.post('/api/v1/static/process_steps', body),
    stepUpdate: (id: number, body: any) => httpService.put(`/api/v1/static/process_steps/${id}`, body),
    stepRemove: (id: number) => httpService.delete(`/api/v1/static/process_steps/${id}`),
}
