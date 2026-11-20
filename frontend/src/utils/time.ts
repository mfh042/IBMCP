export function formatTime(unixSec: number | string | undefined | null, fmt = 'YYYY-MM-DD HH:mm:ss'): string {
    if (!unixSec) return '-'
    const d = typeof unixSec === 'string' ? new Date(unixSec) : new Date(Number(unixSec) * 1000)
    if (isNaN(d.getTime())) return '-'
    const pad = (n: number) => n.toString().padStart(2, '0')
    return fmt
        .replace('YYYY', d.getFullYear().toString())
        .replace('MM', pad(d.getMonth() + 1))
        .replace('DD', pad(d.getDate()))
        .replace('HH', pad(d.getHours()))
        .replace('mm', pad(d.getMinutes()))
        .replace('ss', pad(d.getSeconds()))
}

export function nowSec(): number {
    return Math.floor(Date.now() / 1000)
}

export function toUnixSec(dateStr: string | Date): number {
    const d = typeof dateStr === 'string' ? new Date(dateStr) : dateStr
    return Math.floor(d.getTime() / 1000)
}