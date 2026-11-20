import {defineStore} from 'pinia'
import {dictsApi} from '@/api/dicts'
import {baseDevicesApi} from '@/api/baseDevices'
import type {DictItem} from '@/types/api'

export const useDictionariesStore = defineStore('dictionaries', {
    state: () => ({
        categories: [] as DictItem[],
        brands: [] as DictItem[],
        ports: [] as DictItem[],
        protocols: [] as DictItem[],
        dataItems: [] as DictItem[],
        loaded: false,
    }),
    actions: {
        async loadAll() {
            const [cats, brands, ports, protos] = await Promise.all([
                dictsApi.categories.all(),
                dictsApi.brands.all(),
                dictsApi.ports.all(),
                dictsApi.protocols.all(),
            ])
            const itemsResp = await baseDevicesApi.itemList({page: 1, size: 9999})
            const unwrap = <T, >(resp: any): T[] => resp?.items ?? resp?.list ?? (Array.isArray(resp) ? resp : [])
            this.categories = unwrap<DictItem>(cats)
            this.brands = unwrap<DictItem>(brands)
            this.ports = unwrap<DictItem>(ports)
            this.protocols = unwrap<DictItem>(protos)
            this.dataItems = unwrap<DictItem>(itemsResp)
            this.loaded = true
        },
    },
})