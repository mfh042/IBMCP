import {httpService} from '@/comm/http'

function listParams(params?: { page?: number; size?: number; keyword?: string; customerCode?: string }) {
    if (!params) return params
    const {customerCode, ...rest} = params
    return customerCode ? {...rest, customer_code: customerCode} : rest
}

export const equipmentsApi = {
    list: (params?: { page?: number; size?: number; keyword?: string; customerCode?: string }) =>
        httpService.get('/api/v1/static/equipments', {params: listParams(params)}),
    all: () => httpService.get('/api/v1/static/equipments/all'),
    get: (id: number) => httpService.get(`/api/v1/static/equipments/${id}`),
    getByCode: (code: string) => httpService.get(`/api/v1/static/equipments/code/${code}`),
    create: (body: any) => httpService.post('/api/v1/static/equipments', body),
    update: (id: number, body: any) => httpService.put(`/api/v1/static/equipments/${id}`, body),
    remove: (id: number) => httpService.delete(`/api/v1/static/equipments/${id}`),

    functionList: (params?: { page?: number; size?: number; keyword?: string }) =>
        httpService.get('/api/v1/static/equipment_functions', {params}),
    functionAll: () => httpService.get('/api/v1/static/equipment_functions/all'),
    functionGet: (id: number) => httpService.get(`/api/v1/static/equipment_functions/${id}`),
    functionCreate: (body: any) => httpService.post('/api/v1/static/equipment_functions', body),
    functionUpdate: (id: number, body: any) => httpService.put(`/api/v1/static/equipment_functions/${id}`, body),
    functionRemove: (id: number) => httpService.delete(`/api/v1/static/equipment_functions/${id}`),
}
