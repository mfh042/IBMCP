import {httpService} from '@/comm/http'
import type {HistoryAgg, HistoryResult} from '@/types/api'

export const dataApi = {
    history: (params: { pointId: number; start?: number; end?: number; agg?: HistoryAgg }) =>
        httpService.get<unknown, HistoryResult>('/api/v1/data/history', {
            params: {
                point_id: params.pointId,
                start_ts: params.start,
                end_ts: params.end,
                bucket: params.agg ?? 'raw',
                func: 'avg',
            },
        }),
}
