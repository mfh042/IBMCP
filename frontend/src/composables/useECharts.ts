import * as echarts from 'echarts'

export function useECharts(el: HTMLElement | null, opts?: any) {
    let chart: any = null

    function init(option?: any) {
        if (!el) return
        chart = echarts.init(el)
        chart.setOption(option || opts || {})
        window.addEventListener('resize', resize)
    }

    function setOption(option: any) {
        chart?.setOption(option, {notMerge: false})
    }

    function resize() {
        chart?.resize()
    }

    function dispose() {
        window.removeEventListener('resize', resize)
        chart?.dispose()
        chart = null
    }

    return {init, setOption, resize, dispose}
}