import {httpService} from '@/comm/http'

export const routeBindApi = {
    getRouteDevices: (routeConfigId: number) =>
        httpService.get('/api/v1/config/devices', {params: {route_id: routeConfigId}}),
    expandRoute: (routeConfigId: number) =>
        httpService.get(`/api/v1/config/routes/${routeConfigId}/expand`),
    bindDevice: (routeConfigId: number, body: any) =>
        httpService.post('/api/v1/config/devices', {route_config_id: routeConfigId, ...body}),
    getDeviceBinding: (id: number) =>
        httpService.get(`/api/v1/config/devices/${id}`),
    updateDeviceBinding: (id: number, body: any) =>
        httpService.put(`/api/v1/config/devices/${id}`, body),
    removeDeviceBinding: (id: number) =>
        httpService.delete(`/api/v1/config/devices/${id}`),

    collectionsOfDevice: (deviceBindingId: number) =>
        httpService.get('/api/v1/config/points', {params: {binding_id: deviceBindingId}}),
    getCollection: (id: number) =>
        httpService.get(`/api/v1/config/points/${id}`),
    bindCollection: (deviceBindingId: number, body: any) =>
        httpService.post('/api/v1/config/points', {binding_id: deviceBindingId, ...body}),
    updateCollection: (id: number, body: any) =>
        httpService.put(`/api/v1/config/points/${id}`, body),
    removeCollection: (id: number) =>
        httpService.delete(`/api/v1/config/points/${id}`),
}
