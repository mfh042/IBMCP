import {httpService} from '@/comm/http'

export const routeBindApi = {
    getRouteDevices: (routeConfigId: number) =>
        httpService.get(`/api/running/routes/${routeConfigId}/devices`),
    expandRoute: (routeConfigId: number) =>
        httpService.get(`/api/running/routes/${routeConfigId}/expand`),
    bindDevice: (routeConfigId: number, body: any) =>
        httpService.post(`/api/running/routes/${routeConfigId}/devices`, body),
    getDeviceBinding: (id: number) =>
        httpService.get(`/api/running/devices/${id}`),
    updateDeviceBinding: (id: number, body: any) =>
        httpService.put(`/api/running/devices/${id}`, body),
    removeDeviceBinding: (id: number) =>
        httpService.delete(`/api/running/devices/${id}`),

    collectionsOfDevice: (deviceBindingId: number) =>
        httpService.get(`/api/running/devices/${deviceBindingId}/collections`),
    getCollection: (id: number) =>
        httpService.get(`/api/running/collections/${id}`),
    bindCollection: (deviceBindingId: number, body: any) =>
        httpService.post(`/api/running/devices/${deviceBindingId}/collections`, body),
    updateCollection: (id: number, body: any) =>
        httpService.put(`/api/running/collections/${id}`, body),
    removeCollection: (id: number) =>
        httpService.delete(`/api/running/collections/${id}`),
}