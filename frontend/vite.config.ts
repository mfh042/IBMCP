import {defineConfig, loadEnv} from 'vite'
import vue from '@vitejs/plugin-vue'
import {fileURLToPath, URL} from 'node:url'

const DEFAULT_API_TARGET = 'http://127.0.0.1:8000'

export default defineConfig(({mode}) => {
    const env = loadEnv(mode, fileURLToPath(new URL('.', import.meta.url)), 'VITE_')
    const API_TARGET = env.VITE_API_TARGET || DEFAULT_API_TARGET
    const WS_TARGET = API_TARGET.replace(/^http/, 'ws')

    return {
        plugins: [vue()],
        resolve: {
            alias: {
                '@': fileURLToPath(new URL('./src', import.meta.url)),
            },
        },
        css: {
            preprocessorOptions: {
                scss: {
                    api: 'modern-compiler',
                },
            },
        },
        optimizeDeps: {
            include: [
                'element-plus/es',
                '@element-plus/icons-vue',
                'echarts',
                'vue-echarts',
                'pinia',
                'axios',
            ],
        },
        server: {
            host: '0.0.0.0',
            port: 5173,
            strictPort: true,
            proxy: {
                '/api': {
                    target: API_TARGET,
                    ws: true,
                    changeOrigin: true,
                },
                '/ws': {
                    target: WS_TARGET,
                    ws: true,
                    changeOrigin: true,
                },
            },
        },
        build: {
            sourcemap: false,
        },
    }
})