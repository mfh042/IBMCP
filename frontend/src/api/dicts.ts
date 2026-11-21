import {httpService} from '@/comm/http'

export const dictsApi = {
    categories: {
        list: (params?: { page?: number; size?: number; keyword?: string }) =>
            httpService.get('/api/v1/static/device_categories', {params}),
        all: () => httpService.get('/api/v1/static/device_categories/all'),
        get: (id: number) => httpService.get(`/api/v1/static/device_categories/${id}`),
        create: (body: any) => httpService.post('/api/v1/static/device_categories', body),
        update: (id: number, body: any) => httpService.put(`/api/v1/static/device_categories/${id}`, body),
        remove: (id: number) => httpService.delete(`/api/v1/static/device_categories/${id}`),
    },
    brands: {
        list: (params?: { page?: number; size?: number; keyword?: string }) =>
            httpService.get('/api/v1/static/brands', {params}),
        all: () => httpService.get('/api/v1/static/brands/all'),
        get: (id: number) => httpService.get(`/api/v1/static/brands/${id}`),
        create: (body: any) => httpService.post('/api/v1/static/brands', body),
        update: (id: number, body: any) => httpService.put(`/api/v1/static/brands/${id}`, body),
        remove: (id: number) => httpService.delete(`/api/v1/static/brands/${id}`),
    },
    ports: {
        list: (params?: { page?: number; size?: number; keyword?: string }) =>
            httpService.get('/api/v1/static/ports', {params}),
        all: () => httpService.get('/api/v1/static/ports/all'),
        get: (id: number) => httpService.get(`/api/v1/static/ports/${id}`),
        create: (body: any) => httpService.post('/api/v1/static/ports', body),
        update: (id: number, body: any) => httpService.put(`/api/v1/static/ports/${id}`, body),
        remove: (id: number) => httpService.delete(`/api/v1/static/ports/${id}`),
    },
    protocols: {
        list: (params?: { page?: number; size?: number; keyword?: string }) =>
            httpService.get('/api/v1/static/protocols', {params}),
        all: () => httpService.get('/api/v1/static/protocols/all'),
        get: (id: number) => httpService.get(`/api/v1/static/protocols/${id}`),
        create: (body: any) => httpService.post('/api/v1/static/protocols', body),
        update: (id: number, body: any) => httpService.put(`/api/v1/static/protocols/${id}`, body),
        remove: (id: number) => httpService.delete(`/api/v1/static/protocols/${id}`),
    },
}
