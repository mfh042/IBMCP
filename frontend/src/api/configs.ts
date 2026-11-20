import {httpService} from '@/comm/http'

export const configsApi = {
    upsertDevice: (body: any) => httpService.post('/api/config/devices', body),
    upsertPoint: (body: any) => httpService.post('/api/config/points', body),
    removePoint: (id: number) => httpService.delete(`/api/config/points/${id}`),
    listDevicePoints: (deviceId: number) => httpService.get(`/api/devices/${deviceId}/points`),
}