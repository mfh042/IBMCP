import {httpService} from '@/comm/http'

export const authApi = {
    login: (body: { username: string; password: string }) =>
        httpService.post<any>('/api/v1/auth/login', body),
    refresh: (token: string) =>
        httpService.post<any>('/api/v1/auth/refresh', {refresh_token: token}),
    verify: () => httpService.get<any>('/api/v1/auth/verify'),

    listUsers: (params?: { page?: number; size?: number; keyword?: string }) =>
        httpService.get('/api/v1/auth/users', {params}),
    getUser: (id: number) => httpService.get(`/api/v1/auth/users/${id}`),
    createUser: (body: any) => httpService.post('/api/v1/auth/users', body),
    updateUser: (id: number, body: any) => httpService.put(`/api/v1/auth/users/${id}`, body),
    deleteUser: (id: number) => httpService.delete(`/api/v1/auth/users/${id}`),
    updatePassword: (id: number, body: { oldPassword: string; newPassword: string }) =>
        httpService.put(`/api/v1/auth/users/${id}/password`, body),
    setUserEnabled: (id: number, enabled: boolean) =>
        httpService.put(`/api/v1/auth/users/${id}/enabled`, {enabled}),
    setUserRoles: (id: number, roles: string[]) =>
        httpService.put(`/api/v1/auth/users/${id}/roles`, {roles}),

    listRoles: () => httpService.get('/api/v1/auth/roles'),
    createRole: (body: any) => httpService.post('/api/v1/auth/roles', body),
    updateRole: (id: number, body: any) => httpService.put(`/api/v1/auth/roles/${id}`, body),
    deleteRole: (id: number) => httpService.delete(`/api/v1/auth/roles/${id}`),
}
