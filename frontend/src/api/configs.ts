import {httpService} from '@/comm/http'

export const configsApi = {
    upsertDevice: (body: any) => httpService.post('/api/v1/config/devices', body),
    upsertPoint: (body: any) => httpService.post('/api/v1/config/points', body),
    removePoint: (id: number) => httpService.delete(`/api/v1/config/points/${id}`),
    listDevicePoints: (deviceId: number) => httpService.get(`/api/v1/runtime/${deviceId}/points`),
}
