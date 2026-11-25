export interface ApiResult<T = unknown> {
    code: number
    message: string
    data: T | null
}

export interface DictItem {
    id: number
    code: string
    label: string
    value?: string | number
    description?: string
    enabled?: number
    createdAt?: string
    updatedAt?: string
    categoryCode?: string
    categoryName?: string
    brandCode?: string
    brandName?: string
    modelCode?: string
    modelName?: string
    protocolCode?: string
    protocolName?: string
    portCode?: string
    portName?: string
    itemCode?: string
    itemName?: string
    dataType?: string
    readWrite?: string
    unit?: string
    precision?: number
    minValue?: number | null
    maxValue?: number | null
    addressRule?: string
    collectionCycle?: number
}

export interface UserInfo {
    id: number
    username: string
    displayName: string
    roles: string[]
}

export interface Device {
    id: number
    code: string
    name: string
    protocol?: string
    portCode?: string
    connectionParams?: Record<string, any>
    status?: number
    categoryCode?: string
    categoryName?: string
    brandCode?: string
    modelCode?: string
    createdAt?: string
    updatedAt?: string
}

export interface Point {
    id: number
    deviceId: number
    name: string
    code?: string
    address: string
    dataType: string
    scale?: number
    offset?: number
    unit?: string
    collectionCycle?: number
    intervalMs?: number
    alarmEnabled?: boolean
    alarmHigh?: number
    alarmLow?: number
    enabled?: boolean
    createdAt?: string
    updatedAt?: string
}

export interface Alarm {
    id: number | string
    level: number
    state: number
    deviceCode?: string
    deviceName?: string
    deviceId?: number
    pointAddress?: string
    pointName?: string
    pointId?: number
    message: string
    value?: number | string
    unit?: string
    triggeredAt: string | number
    acknowledgedAt?: string | number
    recoveredAt?: string | number
    acknowledgedBy?: string
}

export interface EquipmentConfig {
    id: number
    configCode: string
    equipmentCode: string
    equipmentName?: string
    configStatus?: number
    runStatus?: number
    location?: string
    remark?: string
    createdAt?: string
    updatedAt?: string
}

export interface RouteConfig {
    id: number
    configId: number
    routeCode: string
    routeName?: string
    routeSeq: number
    routeParams?: Record<string, any>
}

export interface RouteStep {
    stepCode: string
    stepName: string
    stepSeq?: number
    durationTime?: number
    stepParams?: Record<string, any>
}

export interface DeviceBinding {
    id: number
    routeConfigId: number
    stepCode: string
    stepName?: string
    deviceCode: string
    deviceName?: string
    deviceConnection?: Record<string, any>
    portCode?: string
    protocolCode?: string
    collections?: CollectionConfig[]
}

export interface CollectionConfig {
    id: number
    deviceBindingId?: number
    deviceCode?: string
    itemCode: string
    itemName?: string
    dataType?: string
    readWrite?: string
    collectionCycle?: number
    addressOverride?: string
    enabled?: number | boolean
}

export interface EquipmentFunction {
    id: number
    configId?: number
    functionCode: string
    functionName?: string
    enabled?: boolean
    functionParams?: Record<string, any>
}

export interface PaginationParams {
    page?: number
    size?: number
    keyword?: string
}

export interface PagedResult<T> {
    list: T[]
    total: number
    page: number
    size: number
}

export interface RouteExpandRoute extends RouteConfig {
    bindings: DeviceBinding[]
}

export interface RouteExpandResult {
    config: EquipmentConfig
    routes: RouteExpandRoute[]
    functions: EquipmentFunction[]
}

export interface RealTimePointData {
    value: number
    quality?: string
    ts?: number
    unit?: string
    error?: string
}

// ===== WebSocket 帧类型（后端直接透传，字段为 snake_case，不经过 axios 拦截器）=====

export interface WsDataSample {
    device_code: string
    point_id: number
    value: number
    quality?: string
}

export interface WsDataFrame {
    type: 'data'
    ts: number
    count: number
    samples: WsDataSample[]
}

export interface WsDeviceStatusFrame {
    type: 'device_status'
    device_code: string
    status: number
    reason?: string | null
    ts: number
}

export interface WsAlarmEvent {
    device_code: string
    point_address?: string
    point_name?: string
    level: number
    state: number
    value?: number
    triggered_at?: number
    recovered_at?: number
    message?: string
}

export interface WsAlarmFrame {
    type: 'alarm'
    events: WsAlarmEvent[]
}

export interface WsControlFinishedFrame {
    type: 'control_finished'
    command_id: number
    device_code: string
    point_id: number
    success: boolean
    status?: string
    message?: string
    ts: number
}

export type HistoryBucket = 'raw' | '1min' | '5min' | '1h' | '1d'
export type HistoryAgg = HistoryBucket
export type HistoryFunc = 'avg' | 'min' | 'max' | 'sum' | 'count'

// 后端 /data/history 返回经 axios 拦截器 camelCase 化后的字段
export interface HistoryRawRow {
    pointId?: number
    ts: number
    value: number
    quality?: string
    rawValue?: string | number | null
}

export interface HistoryAggRow {
    pointId?: number
    ts: number
    value: number | null
    sampleCount: number
    lastQuality?: string
}

export interface HistoryResult {
    pointId: number
    startTs: number
    endTs: number
    bucket: HistoryBucket
    func: HistoryFunc
    items: (HistoryRawRow | HistoryAggRow)[]
    total: number
}

export interface ControlResult {
    commandId: number
    success: boolean
    message: string
}

export interface CollectorStatus {
    revision?: number
    deviceCount?: number
    pointCount?: number
    queueDepth?: number
    uptimeSec?: number
    [key: string]: unknown
}

export interface PingResult {
    status: string
    time?: number
}

// 后端 /api/v1/runtime 返回（经 axios camelCase 化）
export interface RuntimeDevice {
    deviceKey: string
    protocolCode?: string
    status: number
    lastError?: string | null
    pointsOk: number
    pointsFail: number
    snapshotAt: number
    lastSeen?: number | null
}

// 后端 /api/v1/runtime/{device_code}/points 返回（using_points，经 camelCase 化）
export interface RuntimePoint {
    id: number
    deviceCode: string
    usingPointId?: number | null
    itemName: string
    address: string
    dataType: string
    readWrite?: string
    unit?: string
    scale?: number
    offset?: number
    collectionCycle?: number
    cycleMs?: number
    alarmHigh?: number
    alarmLow?: number
    alarmEnabled?: boolean
    enabled?: boolean
    createdAt?: number
    updatedAt?: number
}