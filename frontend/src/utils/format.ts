export function formatNumber(value: number | undefined | null, decimals = 2): string {
    if (value === undefined || value === null || isNaN(value as number)) return '-'
    return Number(value).toFixed(decimals)
}

export function formatWithUnit(value: number | undefined | null, unit?: string, decimals = 2): string {
    if (value === undefined || value === null || isNaN(value as number)) return '-'
    const num = Number(value).toFixed(decimals)
    return unit ? `${num} ${unit}` : num
}

export function formatComma(value: number | undefined | null): string {
    if (value === undefined || value === null || isNaN(value as number)) return '-'
    return Number(value).toLocaleString('en-US')
}