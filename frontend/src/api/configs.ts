import {httpService} from '@/comm/http'

const enc = (v: string) => encodeURIComponent(v)

export const configsApi = {
    upsertDevice: (body: any) => {
        if (body && body.id) {
            const {id, ...rest} = body
            return httpService.put(`/api/v1/config/devices/${id}`, rest)
        }
        return httpService.post('/api/v1/config/devices', body)
    },
    upsertPoint: (body: any) => {
        if (body && body.id) {
            const {id, ...rest} = body
            return httpService.put(`/api/v1/config/points/${id}`, rest)
        }
        return httpService.post('/api/v1/config/points', body)
    },
    removePoint: (id: number) => httpService.delete(`/api/v1/config/points/${id}`),
    listDevicePoints: (deviceCode: string) =>
        httpService.get(`/api/v1/runtime/${enc(deviceCode)}/points`),
}