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

    #[derive(Serialize, Deserialize, Clone)]
    pub struct ApiConfig {
        pub api_addr: String,
        pub ws_addr: String,
    }

    static CONFIG: Mutex<Option<ApiConfig>> = Mutex::new(None);

    #[tauri::command]
    pub fn get_api_config() -> ApiConfig {
        let guard = CONFIG.lock().unwrap();
        guard.clone().unwrap_or(ApiConfig {
            api_addr: "http://127.0.0.1:5556".to_string(),
            ws_addr: "ws://127.0.0.1:5556/ws/push".to_string(),
        })
    }

    #[tauri::command]
    pub fn set_api_config(api_addr: String, ws_addr: String) -> Result<(), String> {
        let mut guard = CONFIG.lock().unwrap();
        *guard = Some(ApiConfig { api_addr, ws_addr });
        Ok(())
    }

    #[tauri::command]
    pub async fn check_backend_health(url: String) -> Result<bool, String> {
        let client = reqwest::Client::new();
        match client.get(&format!("{}/health", url)).send().await {
            Ok(resp) => Ok(resp.status().is_success()),
            Err(e) => Err(e.to_string()),
        }
    }
}