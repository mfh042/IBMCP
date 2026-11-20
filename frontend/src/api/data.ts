import {httpService} from '@/comm/http'
import type {HistoryAgg, HistoryResult} from '@/types/api'

export const dataApi = {
    history: (params: { pointId: number; start?: number; end?: number; agg?: HistoryAgg }) =>
        httpService.get<unknown, HistoryResult>('/api/history', {params}),
}