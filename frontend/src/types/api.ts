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
    id: number
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
    ts: number
    unit?: string
    error?: string
}

export interface RealtimeDataPayload {
    type: 'data'
    deviceId: number
    pointId: number
    value: number
    quality?: string
    ts: number
    error?: string
}

export interface DeviceStatusPayload {
    type: 'device_status'
    deviceId: number
    status: number
    ts: number
}

export type HistoryAgg = 'raw' | '1min' | '5min' | '1h' | '1d'

export interface HistoryRawRow {
    pointId?: number
    timestamp: number
    value: number
    quality?: string
}

export interface HistoryAggRow {
    timestamp: number
    avg: number
    max: number
    min: number
    count: number
}

export interface HistoryResult {
    rows: (HistoryRawRow | HistoryAggRow)[]
    agg: HistoryAgg
    pointId: number
}

export interface ControlResult {
    commandId: number
    success: boolean
    message: string
}

export interface CollectorStatus {
    running: boolean
    cycles: number
    pointsRead: number
    pointsFailed: number
    pointsWritten: number
    devicesOnline: number
    devicesOffline: number
    lastCycleAt: number
    drivers: string[]
}

export interface PingResult {
    service: string
    status: string
}