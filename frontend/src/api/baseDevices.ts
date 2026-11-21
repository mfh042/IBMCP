import {httpService} from '@/comm/http'

export const baseDevicesApi = {
    deviceList: (params?: { page?: number; size?: number; keyword?: string }) =>
        httpService.get('/api/v1/static/devices', {params}),
    deviceGet: (id: number) => httpService.get(`/api/v1/static/devices/${id}`),
    deviceGetByCode: (code: string) => httpService.get(`/api/v1/static/devices/code/${code}`),
    deviceCreate: (body: any) => httpService.post('/api/v1/static/devices', body),
    deviceUpdate: (id: number, body: any) => httpService.put(`/api/v1/static/devices/${id}`, body),
    deviceRemove: (id: number) => httpService.delete(`/api/v1/static/devices/${id}`),

    itemList: (params?: { page?: number; size?: number; keyword?: string }) =>
        httpService.get('/api/v1/static/data_items', {params}),
    itemGet: (id: number) => httpService.get(`/api/v1/static/data_items/${id}`),
    itemCreate: (body: any) => httpService.post('/api/v1/static/data_items', body),
    itemUpdate: (id: number, body: any) => httpService.put(`/api/v1/static/data_items/${id}`, body),
    itemRemove: (id: number) => httpService.delete(`/api/v1/static/data_items/${id}`),
}
