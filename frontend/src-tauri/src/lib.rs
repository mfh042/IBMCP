#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_http::init())
        .plugin(tauri_plugin_websocket::init())
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .plugin(tauri_plugin_notification::init())
        .plugin(tauri_plugin_clipboard_manager::init())
        .plugin(tauri_plugin_os::init())
        .invoke_handler(tauri::generate_handler![
            commands::get_api_config,
            commands::set_api_config,
            commands::check_backend_health,
        ])
        .setup(|app| {
            let handle = app.handle();
            if let Some(cfg) = tauri::async_runtime::block_on(commands::load_config(&handle)) {
                *commands::CONFIG.lock().unwrap() = Some(cfg);
            }
            #[cfg(debug_assertions)]
            {
                let window = app.get_webview_window("main").unwrap();
                window.open_devtools();
            }
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}

mod commands {
    use serde::{Deserialize, Serialize};
    use std::sync::Mutex;
    use tauri::Manager;

    #[derive(Serialize, Deserialize, Clone)]
    pub struct ApiConfig {
        pub api_addr: String,
        pub ws_addr: String,
    }

    impl Default for ApiConfig {
        fn default() -> Self {
            // 与 backend/config.json 中 listeners 端口保持一致（0.0.0.0:8000）
            Self {
                api_addr: "http://127.0.0.1:8000".to_string(),
                ws_addr: "ws://127.0.0.1:8000/ws/push".to_string(),
            }
        }
    }

    pub(crate) static CONFIG: Mutex<Option<ApiConfig>> = Mutex::new(None);

    fn config_path(app: &tauri::AppHandle) -> Result<std::path::PathBuf, String> {
        let dir = app.path().app_config_dir().map_err(|e| e.to_string())?;
        Ok(dir.join("api_config.json"))
    }

    pub(crate) async fn load_config(app: &tauri::AppHandle) -> Option<ApiConfig> {
        let path = config_path(app).ok()?;
        let bytes = tokio::fs::read(&path).await.ok()?;
        serde_json::from_slice(&bytes).ok()
    }

    async fn save_config(app: &tauri::AppHandle, cfg: &ApiConfig) -> Result<(), String> {
        let path = config_path(app)?;
        if let Some(parent) = path.parent() {
            tokio::fs::create_dir_all(parent)
                .await
                .map_err(|e| e.to_string())?;
        }
        let json = serde_json::to_vec_pretty(cfg).map_err(|e| e.to_string())?;
        tokio::fs::write(&path, json)
            .await
            .map_err(|e| e.to_string())
    }

    #[tauri::command]
    pub fn get_api_config() -> ApiConfig {
        let guard = CONFIG.lock().unwrap();
        guard.clone().unwrap_or_default()
    }

    #[tauri::command]
    pub async fn set_api_config(
        app: tauri::AppHandle,
        api_addr: String,
        ws_addr: String,
    ) -> Result<(), String> {
        let cfg = ApiConfig { api_addr, ws_addr };
        save_config(&app, &cfg).await?;
        *CONFIG.lock().unwrap() = Some(cfg);
        Ok(())
    }

    #[tauri::command]
    pub async fn check_backend_health(url: String) -> Result<bool, String> {
        let base = url.trim_end_matches('/');
        // 后端公开健康端点：/api/v1/ping（见 backend/config.json -> custom_config.public_paths）
        let target = format!("{}/api/v1/ping", base);
        let client = reqwest::Client::builder()
            .timeout(std::time::Duration::from_secs(3))
            .build()
            .map_err(|e| e.to_string())?;
        match client.get(&target).send().await {
            Ok(resp) => Ok(resp.status().is_success()),
            Err(e) => Err(e.to_string()),
        }
    }
}