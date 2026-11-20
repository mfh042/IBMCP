import {httpService} from '@/comm/http'

export const dictsApi = {
    categories: {
        list: (params?: { page?: number; size?: number; keyword?: string }) =>
            httpService.get('/api/dicts/categories', {params}),
        all: () => httpService.get('/api/dicts/categories/all'),
        get: (id: number) => httpService.get(`/api/dicts/categories/${id}`),
        create: (body: any) => httpService.post('/api/dicts/categories', body),
        update: (id: number, body: any) => httpService.put(`/api/dicts/categories/${id}`, body),
        remove: (id: number) => httpService.delete(`/api/dicts/categories/${id}`),
    },
    brands: {
        list: (params?: { page?: number; size?: number; keyword?: string }) =>
            httpService.get('/api/dicts/brands', {params}),
        all: () => httpService.get('/api/dicts/brands/all'),
        get: (id: number) => httpService.get(`/api/dicts/brands/${id}`),
        create: (body: any) => httpService.post('/api/dicts/brands', body),
        update: (id: number, body: any) => httpService.put(`/api/dicts/brands/${id}`, body),
        remove: (id: number) => httpService.delete(`/api/dicts/brands/${id}`),
    },
    ports: {
        list: (params?: { page?: number; size?: number; keyword?: string }) =>
            httpService.get('/api/dicts/ports', {params}),
        all: () => httpService.get('/api/dicts/ports/all'),
        get: (id: number) => httpService.get(`/api/dicts/ports/${id}`),
        create: (body: any) => httpService.post('/api/dicts/ports', body),
        update: (id: number, body: any) => httpService.put(`/api/dicts/ports/${id}`, body),
        remove: (id: number) => httpService.delete(`/api/dicts/ports/${id}`),
    },
    protocols: {
        list: (params?: { page?: number; size?: number; keyword?: string }) =>
            httpService.get('/api/dicts/protocols', {params}),
        all: () => httpService.get('/api/dicts/protocols/all'),
        get: (id: number) => httpService.get(`/api/dicts/protocols/${id}`),
        create: (body: any) => httpService.post('/api/dicts/protocols', body),
        update: (id: number, body: any) => httpService.put(`/api/dicts/protocols/${id}`, body),
        remove: (id: number) => httpService.delete(`/api/dicts/protocols/${id}`),
    },
}