export const storage = {
    setToken(token: string) {
        localStorage.setItem('ibmc_token', token)
    },
    getToken(): string {
        return localStorage.getItem('ibmc_token') || ''
    },
    clearToken() {
        localStorage.removeItem('ibmc_token')
    },
    setRememberedUsername(name: string) {
        localStorage.setItem('ibmc_last_user', name)
    },
    getRememberedUsername(): string {
        return localStorage.getItem('ibmc_last_user') || ''
    },
}