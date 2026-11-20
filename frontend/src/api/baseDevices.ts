import {httpService} from '@/comm/http'

export const baseDevicesApi = {
    deviceList: (params?: { page?: number; size?: number; keyword?: string }) =>
        httpService.get('/api/base-devices', {params}),
    deviceGet: (id: number) => httpService.get(`/api/base-devices/${id}`),
    deviceGetByCode: (code: string) => httpService.get(`/api/base-devices/code/${code}`),
    deviceCreate: (body: any) => httpService.post('/api/base-devices', body),
    deviceUpdate: (id: number, body: any) => httpService.put(`/api/base-devices/${id}`, body),
    deviceRemove: (id: number) => httpService.delete(`/api/base-devices/${id}`),

    itemList: (params?: { page?: number; size?: number; keyword?: string }) =>
        httpService.get('/api/base-devices/items', {params}),
    itemGet: (id: number) => httpService.get(`/api/base-devices/items/${id}`),
    itemCreate: (body: any) => httpService.post('/api/base-devices/items', body),
    itemUpdate: (id: number, body: any) => httpService.put(`/api/base-devices/items/${id}`, body),
    itemRemove: (id: number) => httpService.delete(`/api/base-devices/items/${id}`),
}