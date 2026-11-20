import {createApp} from 'vue'
import {createPinia} from 'pinia'
import ElementPlus from 'element-plus'
import 'element-plus/dist/index.css'
import * as ElementPlusIconsVue from '@element-plus/icons-vue'
import App from './App.vue'
import router from './router'
import {useAuthStore} from './stores/auth'
import {wsService} from './comm/ws'
import './styles/global.scss'

async function bootstrap() {
    const app = createApp(App)

    for (const [key, component] of Object.entries(ElementPlusIconsVue)) {
        app.component(key, component)
    }

    app.use(createPinia())
    app.use(router)
    app.use(ElementPlus)

    app.config.errorHandler = (err, vm, info) => {
        console.error('[Vue Error]', info, err)
    }

    window.addEventListener('unhandledrejection', (e) => {
        if ((e.reason?.message || '').includes('db error')) {
            console.warn('[Backend DB Error (ignored)]', e.reason?.message)
            e.preventDefault()
        } else {
            console.warn('[Unhandled Promise]', e.reason)
        }
    })

    const auth = useAuthStore()
    auth.restoreFromLocalStorage()

    wsService.init()

    app.mount('#app')
}

bootstrap()