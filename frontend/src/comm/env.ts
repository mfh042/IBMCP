export const EnvDetector = {
    get isTauri(): boolean {
        return !!(window as any).__TAURI__
    },
    get isDev(): boolean {
        return import.meta.env.DEV === true
    },
    get isProductionWeb(): boolean {
        return !this.isTauri && !this.isDev
    },
    get isRemoteBrowser(): boolean {
        const host = location.hostname
        return !this.isTauri && !this.isDev && host !== 'localhost' && host !== '127.0.0.1'
    },
    get runtimeLabel(): string {
        if (this.isTauri) return 'Tauri 桌面壳'
        if (this.isDev) return 'Vite 开发'
        if (this.isRemoteBrowser) return '远程 Web'
        return '本机 Web'
    },
}