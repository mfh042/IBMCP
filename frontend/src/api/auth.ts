import {httpService} from '@/comm/http'

export const authApi = {
    login: (body: { username: string; password: string }) =>
        httpService.post<any>('/api/auth/login', body),
    refresh: (token: string) =>
        httpService.post<any>('/api/auth/refresh', {token}),
    verify: () => httpService.get<any>('/api/auth/verify'),

    listUsers: (params?: { page?: number; size?: number; keyword?: string }) =>
        httpService.get('/api/auth/users', {params}),
    getUser: (id: number) => httpService.get(`/api/auth/users/${id}`),
    createUser: (body: any) => httpService.post('/api/auth/users', body),
    updateUser: (id: number, body: any) => httpService.put(`/api/auth/users/${id}`, body),
    deleteUser: (id: number) => httpService.delete(`/api/auth/users/${id}`),
    updatePassword: (id: number, body: { oldPassword: string; newPassword: string }) =>
        httpService.put(`/api/auth/users/${id}/password`, body),
    setUserEnabled: (id: number, enabled: boolean) =>
        httpService.put(`/api/auth/users/${id}/enabled`, {enabled}),
    setUserRoles: (id: number, roles: string[]) =>
        httpService.put(`/api/auth/users/${id}/roles`, {roles}),

    listRoles: () => httpService.get('/api/auth/roles'),
    createRole: (body: any) => httpService.post('/api/auth/roles', body),
    updateRole: (id: number, body: any) => httpService.put(`/api/auth/roles/${id}`, body),
    deleteRole: (id: number) => httpService.delete(`/api/auth/roles/${id}`),
}