import {defineConfig} from 'vite'
import vue from '@vitejs/plugin-vue'
import {fileURLToPath, URL} from 'node:url'

export default defineConfig({
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
                target: 'http://192.168.80.134:8000',
                changeOrigin: true,
            },
            '/ws': {
                target: 'ws://192.168.80.134:8000',
                ws: true,
                changeOrigin: true,
            },
        },
    },
    build: {
        sourcemap: false,
    },
})