import {httpService} from '@/comm/http'
import type {ControlResult, RuntimeDevice, RuntimePoint} from '@/types/api'

const enc = (v: string) => encodeURIComponent(v)

export const devicesApi = {
    list: (params?: { page?: number; size?: number; status?: number }) =>
        httpService.get<unknown, { items: RuntimeDevice[]; total: number }>('/api/v1/runtime', {params}),
    overview: () => httpService.get('/api/v1/runtime/overview'),
    get: (deviceCode: string) => httpService.get<unknown, RuntimeDevice>(`/api/v1/runtime/${enc(deviceCode)}`),
    getPoints: (deviceCode: string) =>
        httpService.get<unknown, { items: RuntimePoint[]; total: number }>(`/api/v1/runtime/${enc(deviceCode)}/points`),
    getPoint: (deviceCode: string, pointId: number) =>
        httpService.get<unknown, RuntimePoint>(`/api/v1/runtime/${enc(deviceCode)}/points/${pointId}`),
    control: (deviceCode: string, body: { pointId: number; pointName?: string; value: string | number }) =>
        httpService.post<unknown, ControlResult>(`/api/v1/runtime/${enc(deviceCode)}/control`, {
            pointId: body.pointId,
            pointName: body.pointName,
            value: body.value,
        }),
    lastValues: (deviceCode: string, limit?: number) =>
        httpService.get(`/api/v1/runtime/${enc(deviceCode)}/last-values`, {params: {limit}}),
    command: (commandId: number) => httpService.get(`/api/v1/control/commands/${commandId}`),
}