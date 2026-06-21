mod protocol;
mod transport;

use arboard::Clipboard;
use base64::{engine::general_purpose, Engine as _};
use chrono::Local;
use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use futures_util::{Sink, SinkExt, StreamExt};
use keyring::Entry;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::collections::HashMap;
use std::fs;
use std::io::{BufRead, BufReader, Read, Write};
use std::net::{TcpListener, TcpStream};
use std::path::PathBuf;
use std::process::Command;
use std::sync::mpsc::{self, Receiver, Sender};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;
use tauri::{AppHandle, Emitter, Manager, State};
use tokio_tungstenite::connect_async;
use tokio_tungstenite::tungstenite::client::IntoClientRequest;
use tokio_tungstenite::tungstenite::Message;
use transport::ble_gatt::BleGattTransport;
use transport::usb_serial::{discover_flash_port, UsbSerialTransport};
use transport::{BleDeviceInfo, BoardTransport, TransportKind};
use uuid::Uuid;

use flate2::read::GzDecoder;

const KEYRING_SERVICE: &str = "kiro-keyboard-companion";
const KEYRING_BLE_TOKEN: &str = "ble-pairing-token";
const HOOK_ADDR: &str = "127.0.0.1:47218";
// Two-way streaming, optimized variant (bigmodel_async). It only emits a new
// packet when the recognition result changes, with lower first/last-word
// latency. Verified end-to-end against the live service via the asr_probe.
const DEFAULT_DOUBAO_ENDPOINT: &str =
    "wss://openspeech.bytedance.com/api/v3/sauc/bigmodel_async";
const DEFAULT_DOUBAO_RESOURCE_ID: &str = "volc.bigasr.sauc.duration";
const WS_CONNECT_TIMEOUT: Duration = Duration::from_secs(8);
const WS_SEND_TIMEOUT: Duration = Duration::from_secs(2);
const STOP_WAIT_TIMEOUT: Duration = Duration::from_secs(15);
const OTA_CHUNK_BYTES: usize = 512;

#[derive(Clone, Debug, Serialize, Deserialize)]
struct Settings {
    #[serde(default)]
    serial_port: String,
    #[serde(default)]
    ble_device_id: String,
    #[serde(default)]
    paired_board_id: String,
    flash_port: String,
    doubao_endpoint: String,
    doubao_resource_id: String,
    doubao_language: String,
    #[serde(default)]
    doubao_api_key_file: String,
    paste_after_transcribe: bool,
    // Preferred audio input device name (empty = system default).
    #[serde(default)]
    audio_input_device: String,
    // Voice engine: "third_party" routes voice through the companion ASR provider;
    // "system" leaves recognition to the OS (board sends the HID Control double-tap).
    #[serde(default = "default_voice_engine")]
    voice_engine: String,
    #[serde(default = "default_asr_provider")]
    asr_provider: String,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            serial_port: String::new(),
            ble_device_id: String::new(),
            paired_board_id: String::new(),
            flash_port: String::new(),
            doubao_endpoint: DEFAULT_DOUBAO_ENDPOINT.to_string(),
            doubao_resource_id: DEFAULT_DOUBAO_RESOURCE_ID.to_string(),
            doubao_language: String::new(),
            doubao_api_key_file: String::new(),
            paste_after_transcribe: true,
            audio_input_device: String::new(),
            voice_engine: default_voice_engine(),
            asr_provider: default_asr_provider(),
        }
    }
}

fn default_voice_engine() -> String {
    "third_party".to_string()
}

fn default_asr_provider() -> String {
    "doubao".to_string()
}

fn normalize_voice_settings(settings: &mut Settings) {
    if settings.voice_engine == "doubao" {
        settings.voice_engine = "third_party".to_string();
        if settings.asr_provider.trim().is_empty() {
            settings.asr_provider = "doubao".to_string();
        }
    }
    if settings.voice_engine != "system" && settings.voice_engine != "third_party" {
        settings.voice_engine = default_voice_engine();
    }
    if settings.asr_provider.trim().is_empty() {
        settings.asr_provider = default_asr_provider();
    }
}

fn is_third_party_voice_engine(engine: &str) -> bool {
    engine == "third_party" || engine == "doubao"
}

#[derive(Clone, Debug, Serialize)]
struct CompanionStatus {
    device_connected: bool,
    serial_port: String,
    transport: TransportKind,
    device_name: String,
    endpoint: String,
    recording: bool,
    busy: bool,
    paired: bool,
    authenticated: bool,
    pairing_code: String,
    last_transcript: String,
    last_partial: String,
    last_error: String,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
struct KeyBinding {
    label: String,
    action_type: String,
    key: String,
    modifiers: Vec<String>,
}

#[derive(Debug, Deserialize, Serialize)]
struct BoardButtonEvent {
    #[serde(rename = "type")]
    message_type: String,
    key: String,
    action: String,
    held_ms: Option<u64>,
    selected_agent: Option<u8>,
    voice_recording: Option<bool>,
    voice_editing: Option<bool>,
    voice_intent: Option<String>,
}

#[derive(Debug, Deserialize, Serialize)]
struct HookPayload {
    #[serde(rename = "type")]
    message_type: String,
    agent_name: Option<String>,
    state: Option<String>,
    session_id: Option<String>,
    cwd: Option<String>,
}

struct RecordingHandle {
    stop_tx: tokio::sync::oneshot::Sender<()>,
    done_rx: Receiver<Result<String, String>>,
    target_bundle_id: Option<String>,
}

struct AppStateInner {
    settings: Settings,
    logs: Vec<String>,
    transport: Option<Box<dyn BoardTransport>>,
    transport_kind: TransportKind,
    endpoint: String,
    device_name: String,
    connection_id: u64,
    missed_pongs: u8,
    recording: Option<RecordingHandle>,
    busy: bool,
    authenticated: bool,
    pairing_code: String,
    last_transcript: String,
    last_partial: String,
    last_error: String,
    pending_requests: HashMap<String, Sender<serde_json::Value>>,
}

struct AppState {
    inner: Mutex<AppStateInner>,
}

type SharedState = Arc<AppState>;

fn app_data_dir(_app: &AppHandle) -> Result<PathBuf, String> {
    // Store all config under ~/.ki-board/ (user-visible, survives reinstall).
    let home = std::env::var("HOME").map_err(|_| "HOME not set")?;
    Ok(PathBuf::from(home).join(".ki-board"))
}

fn settings_path(app: &AppHandle) -> Result<PathBuf, String> {
    Ok(app_data_dir(app)?.join("settings.json"))
}

fn load_settings(app: &AppHandle) -> Settings {
    let Ok(path) = settings_path(app) else {
        return Settings::default();
    };
    let Ok(raw) = fs::read_to_string(path) else {
        return Settings::default();
    };
    let mut settings: Settings = serde_json::from_str(&raw).unwrap_or_default();
    normalize_voice_settings(&mut settings);
    settings
}

fn save_settings_to_disk(app: &AppHandle, settings: &Settings) -> Result<(), String> {
    let dir = app_data_dir(app)?;
    fs::create_dir_all(&dir).map_err(|err| format!("create app data dir failed: {err}"))?;
    let mut normalized = settings.clone();
    normalize_voice_settings(&mut normalized);
    let raw = serde_json::to_string_pretty(&normalized).map_err(|err| err.to_string())?;
    fs::write(settings_path(app)?, raw).map_err(|err| format!("write settings failed: {err}"))
}

fn log_event(
    app: &AppHandle,
    level: &str,
    category: &str,
    channel: &str,
    message: impl Into<String>,
) {
    let message = format!(
        "[{}] [{:<5}] [{:<10}] [{:<8}] {}",
        timestamp_label(),
        level,
        category,
        channel,
        message.into()
    );
    if let Some(state) = app.try_state::<SharedState>() {
        if let Ok(mut inner) = state.inner.lock() {
            inner.logs.insert(0, message.clone());
            inner.logs.truncate(120);
        }
    }
    let _ = app.emit("companion-log", message);
}

fn log_error(app: &AppHandle, category: &str, channel: &str, message: impl Into<String>) {
    log_event(app, "ERROR", category, channel, message);
}

fn timestamp_label() -> String {
    Local::now().format("%H:%M:%S%.3f").to_string()
}

fn emit_voice(app: &AppHandle, event: &str, text: impl Into<String>) {
    let _ = app.emit(
        "voice-event",
        serde_json::json!({"event": event, "text": text.into()}),
    );
}

fn play_sound(name: &str) {
    let path = format!("/System/Library/Sounds/{name}.aiff");
    std::process::Command::new("afplay")
        .arg(&path)
        .spawn()
        .ok(); // fire-and-forget, don't block
}

fn frontmost_bundle_id() -> Option<String> {
    #[cfg(target_os = "macos")]
    {
        let output = Command::new("osascript")
            .arg("-e")
            .arg("tell application \"System Events\" to get bundle identifier of first application process whose frontmost is true")
            .output()
            .ok()?;
        if !output.status.success() {
            return None;
        }
        let bundle_id = String::from_utf8_lossy(&output.stdout).trim().to_string();
        if bundle_id.is_empty() || bundle_id == "com.kiro.keyboard.companion" {
            None
        } else {
            Some(bundle_id)
        }
    }
    #[cfg(not(target_os = "macos"))]
    {
        None
    }
}

fn activate_app(bundle_id: &str) {
    #[cfg(target_os = "macos")]
    {
        let script = format!(
            "tell application id \"{}\" to activate",
            bundle_id.replace('"', "\\\"")
        );
        let _ = Command::new("osascript").arg("-e").arg(script).output();
    }
    #[cfg(not(target_os = "macos"))]
    {
        let _ = bundle_id;
    }
}

fn hide_main_window_inner(app: &AppHandle) {
    if let Some(win) = app.get_webview_window("main") {
        let _ = win.hide();
    }
}

#[tauri::command]
fn hide_main_window(app: AppHandle) -> Result<(), String> {
    hide_main_window_inner(&app);
    Ok(())
}

fn write_board_json_from_arc(
    state: &SharedState,
    payload: &serde_json::Value,
) -> Result<(), String> {
    let mut inner = state.inner.lock().map_err(|_| "state lock poisoned")?;
    let transport = inner.transport.as_mut().ok_or("device is not connected")?;
    transport.write_json(payload)
}

fn get_ble_token() -> Option<String> {
    Entry::new(KEYRING_SERVICE, KEYRING_BLE_TOKEN)
        .ok()?
        .get_password()
        .ok()
        .filter(|value| !value.trim().is_empty())
}

fn set_ble_token(token: &str) -> Result<(), String> {
    Entry::new(KEYRING_SERVICE, KEYRING_BLE_TOKEN)
        .map_err(|err| format!("keyring unavailable: {err}"))?
        .set_password(token)
        .map_err(|err| format!("keyring save failed: {err}"))
}

fn clear_ble_token() {
    if let Ok(entry) = Entry::new(KEYRING_SERVICE, KEYRING_BLE_TOKEN) {
        let _ = entry.delete_credential();
    }
}

fn get_doubao_api_key(state: &SharedState) -> Option<String> {
    let inner = state.inner.lock().ok()?;
    let value = inner.settings.doubao_api_key_file.clone();
    if value.trim().is_empty() { None } else { Some(value) }
}

fn board_request(
    state: &SharedState,
    payload: serde_json::Value,
    timeout: Duration,
) -> Result<serde_json::Value, String> {
    let request_id = payload
        .get("request_id")
        .and_then(|value| value.as_str())
        .ok_or("board request missing request_id")?
        .to_string();
    let (tx, rx) = mpsc::channel();
    {
        let mut inner = state.inner.lock().map_err(|_| "state lock poisoned")?;
        inner.pending_requests.insert(request_id.clone(), tx);
    }
    if let Err(err) = write_board_json_from_arc(state, &payload) {
        if let Ok(mut inner) = state.inner.lock() {
            inner.pending_requests.remove(&request_id);
        }
        return Err(err);
    }
    match rx.recv_timeout(timeout) {
        Ok(value) => Ok(value),
        Err(err) => {
            if let Ok(mut inner) = state.inner.lock() {
                inner.pending_requests.remove(&request_id);
            }
            Err(format!("board response timed out: {err}"))
        }
    }
}

fn handle_board_line(app: &AppHandle, state: &SharedState, line: &str) {
    if !line.starts_with('{') {
        return;
    }
    let Ok(value) = serde_json::from_str::<serde_json::Value>(line) else {
        return;
    };
    let Some(message_type) = value.get("type").and_then(|v| v.as_str()) else {
        return;
    };
    if let Some(request_id) = value.get("request_id").and_then(|v| v.as_str()) {
        if let Ok(mut inner) = state.inner.lock() {
            if let Some(tx) = inner.pending_requests.remove(request_id) {
                let _ = tx.send(value);
                return;
            }
        }
    }
    // Pairing / authentication messages (BLE). Handled here, never logged raw
    // (pair_ok carries the secret token).
    match message_type {
        "pair_code" => {
            let code = value.get("code").and_then(|v| v.as_str()).unwrap_or("");
            let board_id = value.get("board_id").and_then(|v| v.as_str()).unwrap_or("");
            let name = value.get("name").and_then(|v| v.as_str()).unwrap_or("Kiro KB");
            log_event(app, "INFO", "pairing", "ble", format!("code={code} compare_on_board=true"));
            if let Ok(mut inner) = state.inner.lock() {
                inner.pairing_code = code.to_string();
            }
            let _ = app.emit(
                "pair-event",
                serde_json::json!({"event":"code","code":code,"board_id":board_id,"name":name}),
            );
            return;
        }
        "pair_ok" => {
            let token = value.get("token").and_then(|v| v.as_str()).unwrap_or("");
            let board_id = value
                .get("board_id")
                .and_then(|v| v.as_str())
                .unwrap_or("")
                .to_string();
            if !token.is_empty() {
                if let Err(err) = set_ble_token(token) {
                    log_error(app, "pairing", "ble", format!("token save failed: {err}"));
                }
            }
            let settings_snapshot = {
                if let Ok(mut inner) = state.inner.lock() {
                    inner.settings.paired_board_id = board_id.clone();
                    inner.authenticated = true;
                    inner.pairing_code.clear();
                    Some(inner.settings.clone())
                } else {
                    None
                }
            };
            if let Some(settings) = settings_snapshot {
                let _ = save_settings_to_disk(app, &settings);
            }
            log_event(app, "INFO", "pairing", "ble", "paired with board");
            let _ = app.emit(
                "pair-event",
                serde_json::json!({"event":"ok","board_id":board_id}),
            );
            return;
        }
        "pair_failed" => {
            let reason = value.get("reason").and_then(|v| v.as_str()).unwrap_or("failed");
            log_error(app, "pairing", "ble", format!("failed reason={reason}"));
            if let Ok(mut inner) = state.inner.lock() {
                inner.pairing_code.clear();
            }
            let _ = app.emit(
                "pair-event",
                serde_json::json!({"event":"failed","reason":reason}),
            );
            return;
        }
        "auth_ok" => {
            if let Ok(mut inner) = state.inner.lock() {
                inner.authenticated = true;
            }
            let _ = app.emit("pair-event", serde_json::json!({"event":"auth_ok"}));
            return;
        }
        "auth_required" => {
            if let Ok(mut inner) = state.inner.lock() {
                inner.authenticated = false;
            }
            log_event(app, "WARN", "pairing", "ble", "auth_required");
            let _ = app.emit("pair-event", serde_json::json!({"event":"auth_required"}));
            return;
        }
        "unpaired" => {
            clear_ble_token();
            let settings_snapshot = {
                if let Ok(mut inner) = state.inner.lock() {
                    inner.settings.paired_board_id.clear();
                    inner.authenticated = false;
                    Some(inner.settings.clone())
                } else {
                    None
                }
            };
            if let Some(settings) = settings_snapshot {
                let _ = save_settings_to_disk(app, &settings);
            }
            log_event(app, "INFO", "pairing", "ble", "board unpaired");
            let _ = app.emit("pair-event", serde_json::json!({"event":"unpaired"}));
            return;
        }
        _ => {}
    }
    if message_type == "pong" {
        if let Ok(mut inner) = state.inner.lock() {
            inner.missed_pongs = 0;
        }
        return;
    }
    if message_type == "hello_ack" {
        let channel = value.get("channel").and_then(|v| v.as_str()).unwrap_or("-");
        log_event(app, "INFO", "board", "rx", format!("hello_ack channel={channel}"));
        return;
    }
    if message_type != "button_event" {
        // Suppress routine heartbeat/status chatter from the log so it stays
        // readable. These arrive frequently and carry no actionable info here.
        if !matches!(message_type, "pong" | "ble_status" | "hello_ack") {
            log_event(app, "INFO", "board", "rx", line);
        }
        return;
    }
    let Ok(event) = serde_json::from_value::<BoardButtonEvent>(value) else {
        return;
    };
    log_event(
        app,
        "INFO",
        "command",
        "board",
        format!(
            "button {} {} intent={} rec={} edit={}",
            event.key,
            event.action,
            event.voice_intent.as_deref().unwrap_or(""),
            event.voice_recording.unwrap_or(false),
            event.voice_editing.unwrap_or(false)
        ),
    );
    if let Err(err) = handle_button_event(app, state, event) {
        set_last_error(state, err.clone());
        log_error(app, "command", "board", err);
    }
}

fn handle_button_event(
    app: &AppHandle,
    state: &SharedState,
    event: BoardButtonEvent,
) -> Result<(), String> {
    let engine = state
        .inner
        .lock()
        .map_err(|_| "state lock poisoned")?
        .settings
        .voice_engine
        .clone();
    if is_third_party_voice_engine(&engine) {
        if let Some(intent) = event.voice_intent.as_deref() {
            match intent {
                "voice_start" => return start_recording_inner(app.clone(), state),
                "voice_commit_send" => {
                    return stop_recording_and_transcribe_inner(app.clone(), state, true);
                }
                "voice_commit_edit" => {
                    return stop_recording_and_transcribe_inner(app.clone(), state, false);
                }
                "voice_cancel" => return cancel_recording_inner(app.clone(), state),
                "voice_send_edit" => return press_return_key(),
                "voice_delete" => return press_backspace_key(),
                "voice_exit_edit" => return Ok(()),
                _ => {}
            }
        }
    }

    match (event.key.as_str(), event.action.as_str()) {
        ("middle", "short") => {
            if engine == "system" {
                // System dictation is driven entirely on the board via the HID
                // Control double-tap; the companion stays out of the way.
                return Ok(());
            }
            if event.voice_editing.unwrap_or(false) {
                // Board is showing the check mark as "send": it owns Enter.
                return Ok(());
            }
            let recording = state
                .inner
                .lock()
                .map_err(|_| "state lock poisoned")?
                .recording
                .is_some();
            if recording || event.voice_recording.unwrap_or(false) {
                stop_recording_and_transcribe_inner(app.clone(), state, false)
            } else {
                start_recording_inner(app.clone(), state)
            }
        }
        ("left", _) => {
            if event.voice_editing.unwrap_or(false) {
                return Ok(());
            }
            let should_commit = {
                let inner = state.inner.lock().map_err(|_| "state lock poisoned")?;
                is_third_party_voice_engine(&inner.settings.voice_engine) &&
                    (inner.recording.is_some() || event.voice_recording.unwrap_or(false))
            };
            if should_commit {
                stop_recording_and_transcribe_inner(app.clone(), state, false)
            } else {
                cancel_recording_inner(app.clone(), state)
            }
        }
        ("right", _) => {
            if event.voice_editing.unwrap_or(false) {
                // Board is showing backspace as "delete character": it owns HID Backspace.
                return Ok(());
            }
            let should_cancel = {
                let inner = state.inner.lock().map_err(|_| "state lock poisoned")?;
                is_third_party_voice_engine(&inner.settings.voice_engine) &&
                    (inner.recording.is_some() || event.voice_recording.unwrap_or(false))
            };
            if should_cancel {
                cancel_recording_inner(app.clone(), state)
            } else {
                // Normal agent switching is owned by the board's USB HID path.
                // The companion only logs the button event; sending another CGEvent
                // here is less reliable and can duplicate the hardware keypress.
                Ok(())
            }
        }
        _ => Ok(()),
    }
}

fn set_last_error(state: &SharedState, message: String) {
    if let Ok(mut inner) = state.inner.lock() {
        inner.last_error = message;
        inner.busy = false;
    }
}

fn mark_recording_error(state: &SharedState, message: String) {
    if let Ok(mut inner) = state.inner.lock() {
        inner.busy = false;
        inner.last_error = message;
        // Keep `recording` intact so the owner of `done_rx` can still receive
        // the worker result. Dropping it here disconnects Stop from the worker.
    }
}

fn write_voice_state(state: &SharedState, voice_state: &str) {
    let payload = serde_json::json!({
        "type": "voice_state",
        "state": voice_state,
    });
    let _ = write_board_json_from_arc(state, &payload);
}

#[tauri::command]
fn get_settings(state: State<SharedState>) -> Result<Settings, String> {
    Ok(state
        .inner
        .lock()
        .map_err(|_| "state lock poisoned")?
        .settings
        .clone())
}

#[tauri::command]
fn save_settings(
    app: AppHandle,
    state: State<SharedState>,
    mut settings: Settings,
) -> Result<(), String> {
    normalize_voice_settings(&mut settings);
    save_settings_to_disk(&app, &settings)?;
    let engine = settings.voice_engine.clone();
    let provider = settings.asr_provider.clone();
    state
        .inner
        .lock()
        .map_err(|_| "state lock poisoned")?
        .settings = settings;
    // Best-effort: keep the board's voice-engine mode in sync (no-op if offline).
    let _ = write_board_json_from_arc(state.inner(), &protocol::voice_engine_payload(&engine, &provider));
    log_event(&app, "INFO", "config", "tx", format!("voice_engine={engine} asr_provider={provider}"));
    Ok(())
}

#[tauri::command]
fn save_doubao_api_key(app: AppHandle, state: State<SharedState>, api_key: String) -> Result<(), String> {
    {
        let mut inner = state.inner.lock().map_err(|_| "state lock poisoned")?;
        inner.settings.doubao_api_key_file = api_key.clone();
    }
    let settings = state.inner.lock().map_err(|_| "state lock poisoned")?.settings.clone();
    save_settings_to_disk(&app, &settings)?;
    log_event(&app, "INFO", "config", "-", "saved Doubao API key");
    Ok(())
}

#[tauri::command]
fn has_doubao_api_key(state: State<SharedState>) -> Result<bool, String> {
    Ok(get_doubao_api_key(state.inner()).is_some())
}

#[tauri::command]
fn get_status(state: State<SharedState>) -> Result<CompanionStatus, String> {
    let inner = state.inner.lock().map_err(|_| "state lock poisoned")?;
    Ok(CompanionStatus {
        device_connected: inner.transport.is_some(),
        serial_port: if inner.transport_kind == TransportKind::Usb {
            inner.endpoint.clone()
        } else {
            String::new()
        },
        transport: inner.transport_kind,
        device_name: inner.device_name.clone(),
        endpoint: inner.endpoint.clone(),
        recording: inner.recording.is_some(),
        busy: inner.busy,
        paired: !inner.settings.paired_board_id.trim().is_empty(),
        authenticated: inner.authenticated,
        pairing_code: inner.pairing_code.clone(),
        last_transcript: inner.last_transcript.clone(),
        last_partial: inner.last_partial.clone(),
        last_error: inner.last_error.clone(),
    })
}

#[tauri::command]
fn list_serial_ports() -> Result<Vec<transport::usb_serial::SerialPortInfo>, String> {
    transport::usb_serial::list_serial_ports()
}

#[tauri::command]
fn list_audio_input_devices() -> Result<Vec<String>, String> {
    let host = cpal::default_host();
    let devices = host
        .input_devices()
        .map_err(|err| format!("enumerate input devices failed: {err}"))?;
    let mut names = Vec::new();
    for device in devices {
        if let Ok(name) = device.name() {
            if !name.trim().is_empty() && !names.contains(&name) {
                names.push(name);
            }
        }
    }
    Ok(names)
}

#[tauri::command]
fn list_ble_devices() -> Result<Vec<BleDeviceInfo>, String> {
    transport::ble_gatt::list_ble_devices()
}

#[tauri::command]
fn get_logs(state: State<SharedState>) -> Result<Vec<String>, String> {
    Ok(state
        .inner
        .lock()
        .map_err(|_| "state lock poisoned")?
        .logs
        .clone())
}

#[tauri::command]
fn clear_logs(state: State<SharedState>) -> Result<(), String> {
    state
        .inner
        .lock()
        .map_err(|_| "state lock poisoned")?
        .logs
        .clear();
    Ok(())
}

#[tauri::command]
fn test_log_event(app: AppHandle) -> Result<(), String> {
    log_event(&app, "INFO", "test", "-", "test log event from tauri backend");
    Ok(())
}

#[tauri::command]
fn connect_device(
    app: AppHandle,
    state: State<SharedState>,
    port_name: Option<String>,
) -> Result<(), String> {
    connect_device_inner(&app, state.inner(), port_name)
}

fn connect_device_inner(
    app: &AppHandle,
    state: &SharedState,
    port_name: Option<String>,
) -> Result<(), String> {
    let settings = state
        .inner
        .lock()
        .map_err(|_| "state lock poisoned")?
        .settings
        .clone();
    let selected_port = port_name
        .filter(|p| !p.is_empty())
        .or_else(|| {
            if settings.serial_port.trim().is_empty() {
                None
            } else {
                Some(settings.serial_port.clone())
            }
        });
    {
        let mut inner = state.inner.lock().map_err(|_| "state lock poisoned")?;
        if let Some(mut existing) = inner.transport.take() {
            existing.disconnect();
        }
        inner.transport_kind = TransportKind::None;
        inner.endpoint.clear();
        inner.device_name.clear();
        inner.connection_id = inner.connection_id.wrapping_add(1);
        inner.pending_requests.clear();
    }
    let (line_tx, line_rx) = mpsc::channel::<String>();
    let transport: Box<dyn BoardTransport> =
        match UsbSerialTransport::connect(selected_port.clone(), line_tx.clone()) {
            Ok(t) => Box::new(t),
            Err(usb_err) => {
                log_error(&app, "transport", "usb", format!("auto-connect failed: {usb_err}"));
                Box::new(BleGattTransport::connect(
                    non_empty(settings.ble_device_id.clone()),
                    line_tx,
                )?)
            }
        };
    let kind = transport.kind();
    let endpoint = transport.endpoint();
    let connection_id = {
        let mut inner = state.inner.lock().map_err(|_| "state lock poisoned")?;
        inner.connection_id = inner.connection_id.wrapping_add(1);
        inner.connection_id
    };
    {
        let mut inner = state.inner.lock().map_err(|_| "state lock poisoned")?;
        inner.transport_kind = kind;
        inner.endpoint = endpoint.clone();
        inner.device_name = match kind {
            TransportKind::Usb => "ki-board USB".to_string(),
            TransportKind::Ble => "ki-board BLE".to_string(),
            TransportKind::None => String::new(),
        };
        inner.transport = Some(transport);
        inner.last_error.clear();
        inner.missed_pongs = 0;
    }

    let shared = state.clone();
    {
        // USB CDC is physically trusted (no auth). BLE must authenticate with the
        // shared token from pairing.
        let mut inner = shared.inner.lock().map_err(|_| "state lock poisoned")?;
        inner.authenticated = kind == TransportKind::Usb;
    }
    write_board_json_from_arc(&shared, &protocol::hello_payload())?;
    let _ = write_board_json_from_arc(&shared, &protocol::legacy_hello_payload());
    if kind == TransportKind::Ble {
        if let Some(token) = get_ble_token() {
            let _ = write_board_json_from_arc(&shared, &protocol::auth_payload(&token));
        }
    }
    // Sync the active voice engine so the board knows whether to emit the
    // system-dictation HID (Control double-tap) on the middle key.
    let engine = {
        let inner = shared.inner.lock().map_err(|_| "state lock poisoned")?;
        inner.settings.voice_engine.clone()
    };
    let provider = {
        let inner = shared.inner.lock().map_err(|_| "state lock poisoned")?;
        inner.settings.asr_provider.clone()
    };
    let _ = write_board_json_from_arc(&shared, &protocol::voice_engine_payload(&engine, &provider));
    // Auto-sync HID output based on actual transport kind.
    let hid_mode = if kind == TransportKind::Ble { "ble" } else { "usb" };
    let _ = write_board_json_from_arc(&shared, &protocol::set_hid_output_payload(hid_mode));
    spawn_board_reader(app.clone(), shared.clone(), line_rx, connection_id);
    let channel = match kind {
        TransportKind::Usb => "usb",
        TransportKind::Ble => "ble",
        TransportKind::None => "-",
    };
    log_event(&app, "INFO", "transport", channel, format!("connected endpoint={endpoint}"));
    Ok(())
}

#[tauri::command]
fn disconnect_device(state: State<SharedState>) -> Result<(), String> {
    let mut inner = state.inner.lock().map_err(|_| "state lock poisoned")?;
    if let Some(mut transport) = inner.transport.take() {
        transport.disconnect();
    }
    inner.transport_kind = TransportKind::None;
    inner.endpoint.clear();
    inner.device_name.clear();
    inner.connection_id = inner.connection_id.wrapping_add(1);
    inner.authenticated = false;
    inner.pending_requests.clear();
    Ok(())
}

#[tauri::command]
fn start_pairing(state: State<SharedState>) -> Result<(), String> {
    // The board must already be in its on-device pairing window. We send a
    // pair_request; the board replies pair_code (-> "pair-event" code) and the
    // user confirms on the board (-> pair_ok).
    let client = hostname_label();
    write_board_json_from_arc(state.inner(), &protocol::pair_request_payload(&client))
}

#[tauri::command]
fn forget_device(app: AppHandle, state: State<SharedState>) -> Result<(), String> {
    // Best-effort tell the board to drop its binding (ignore errors if offline).
    let _ = write_board_json_from_arc(state.inner(), &protocol::unpair_payload());
    clear_ble_token();
    let settings_snapshot = {
        let mut inner = state.inner.lock().map_err(|_| "state lock poisoned")?;
        inner.settings.paired_board_id.clear();
        inner.authenticated = false;
        inner.settings.clone()
    };
    save_settings_to_disk(&app, &settings_snapshot)?;
    log_event(&app, "INFO", "pairing", "tx", "forgot paired device");
    Ok(())
}

#[tauri::command]
fn set_hid_output(state: State<SharedState>, mode: String) -> Result<(), String> {
    let hid_mode = if mode == "ble" { "ble" } else { "usb" };
    write_board_json_from_arc(state.inner(), &protocol::set_hid_output_payload(hid_mode))
}

fn hostname_label() -> String {
    std::env::var("HOSTNAME")
        .ok()
        .or_else(|| {
            std::process::Command::new("scutil")
                .arg("--get")
                .arg("ComputerName")
                .output()
                .ok()
                .and_then(|out| String::from_utf8(out.stdout).ok())
                .map(|s| s.trim().to_string())
                .filter(|s| !s.is_empty())
        })
        .unwrap_or_else(|| "Mac".to_string())
}

fn spawn_board_reader(
    app: AppHandle,
    state: SharedState,
    line_rx: Receiver<String>,
    connection_id: u64,
) {
    thread::spawn(move || {
        while let Ok(line) = line_rx.recv() {
            let connected = state.inner.lock().map(|inner| {
                inner.transport.is_some() && inner.connection_id == connection_id
            });
            if !connected.unwrap_or(false) {
                break;
            }
            handle_board_line(&app, &state, &line);
        }
    });
}

fn spawn_connection_manager(app: AppHandle, state: SharedState) {
    thread::spawn(move || loop {
        thread::sleep(Duration::from_millis(5000));
        let connected = state
            .inner
            .lock()
            .map(|inner| inner.transport.is_some())
            .unwrap_or(false);

        if !connected {
            match connect_device_inner(&app, &state, None) {
                Ok(()) => {
                    log_event(&app, "INFO", "transport", "-", "auto-connected");
                    // Reset pong counter for fresh connection.
                    if let Ok(mut inner) = state.inner.lock() {
                        inner.missed_pongs = 0;
                    }
                }
                Err(err) => {
                    log_error(&app, "transport", "auto", format!("auto-connect failed: {err}"));
                }
            }
            continue;
        }

        // Connected — do heartbeat.
        let kind = state
            .inner
            .lock()
            .map(|inner| inner.transport_kind)
            .unwrap_or(TransportKind::None);

        // Send ping first, then increment missed counter.
        let _ = write_board_json_from_arc(&state, &protocol::ping_payload());
        let _ = write_board_json_from_arc(&state, &protocol::legacy_ping_payload());

        // Wait a moment for pong to arrive and reset the counter.
        thread::sleep(Duration::from_millis(2000));

        let missed = state
            .inner
            .lock()
            .map(|inner| inner.missed_pongs)
            .unwrap_or(0);

        if missed >= 3 {
            log_event(&app, "WARN", "ping", "-", "3 pongs missed, disconnecting");
            if let Ok(mut inner) = state.inner.lock() {
                if let Some(mut transport) = inner.transport.take() {
                    transport.disconnect();
                }
                inner.transport_kind = TransportKind::None;
                inner.endpoint.clear();
                inner.device_name.clear();
                inner.missed_pongs = 0;
            }
            continue;
        }

        // Increment missed_pongs; will be reset to 0 when pong arrives.
        if let Ok(mut inner) = state.inner.lock() {
            inner.missed_pongs = inner.missed_pongs.saturating_add(1);
        }

        let channel = match kind {
            TransportKind::Usb => "usb",
            TransportKind::Ble => "ble",
            TransportKind::None => "-",
        };
        log_event(&app, "DEBUG", "ping", channel, "heartbeat sent");
    });
}

fn non_empty(value: String) -> Option<String> {
    if value.trim().is_empty() {
        None
    } else {
        Some(value)
    }
}

#[tauri::command]
fn get_keymap(state: State<SharedState>) -> Result<Vec<KeyBinding>, String> {
    let request_id = Uuid::new_v4().to_string();
    let response = board_request(
        state.inner(),
        serde_json::json!({"type":"get_keymap","request_id": request_id}),
        Duration::from_secs(2),
    )?;
    if !response
        .get("ok")
        .and_then(|v| v.as_bool())
        .unwrap_or(false)
    {
        return Err(response
            .get("error")
            .and_then(|v| v.as_str())
            .unwrap_or("keymap request failed")
            .to_string());
    }
    serde_json::from_value(
        response
            .get("keys")
            .cloned()
            .unwrap_or_else(|| serde_json::json!([])),
    )
    .map_err(|err| format!("keymap response parse failed: {err}"))
}

#[tauri::command]
fn set_keymap(state: State<SharedState>, keys: Vec<KeyBinding>) -> Result<(), String> {
    let request_id = Uuid::new_v4().to_string();
    let response = board_request(
        state.inner(),
        serde_json::json!({"type":"set_keymap","request_id": request_id, "keys": keys}),
        Duration::from_secs(3),
    )?;
    if response
        .get("ok")
        .and_then(|v| v.as_bool())
        .unwrap_or(false)
    {
        Ok(())
    } else {
        Err(response
            .get("error")
            .and_then(|v| v.as_str())
            .unwrap_or("set keymap failed")
            .to_string())
    }
}

#[tauri::command]
fn flash_firmware(
    app: AppHandle,
    state: State<SharedState>,
    firmware_path: String,
    port_name: Option<String>,
) -> Result<String, String> {
    let firmware = PathBuf::from(firmware_path.trim());
    if !firmware.exists() {
        return Err("firmware file does not exist".to_string());
    }
    let port = port_name
        .filter(|port| !port.is_empty())
        .or_else(discover_flash_port)
        .ok_or("CH340 flash serial port not found")?;

    log_event(&app, "INFO", "flash", "ch340", format!("start firmware={} port={port}", firmware.display()));
    {
        let mut inner = state.inner.lock().map_err(|_| "state lock poisoned")?;
        if let Some(mut transport) = inner.transport.take() {
            transport.disconnect();
        }
        inner.transport_kind = TransportKind::None;
        inner.endpoint.clear();
        inner.device_name.clear();
        inner.connection_id = inner.connection_id.wrapping_add(1);
        inner.pending_requests.clear();
    }
    let output = Command::new("python3")
        .args([
            "-m",
            "esptool",
            "--chip",
            "esp32s3",
            "--port",
            &port,
            "--baud",
            "921600",
            "write_flash",
            "0x10000",
        ])
        .arg(&firmware)
        .output()
        .map_err(|err| format!("failed to launch esptool: {err}"))?;
    let mut text = String::new();
    text.push_str(&String::from_utf8_lossy(&output.stdout));
    text.push_str(&String::from_utf8_lossy(&output.stderr));
    for line in text.lines() {
        log_event(&app, "INFO", "flash", "ch340", line);
    }
    if output.status.success() {
        thread::sleep(Duration::from_secs(2));
        if let Err(err) = connect_device_inner(&app, state.inner(), None) {
            log_error(&app, "transport", "usb", format!("reconnect after flash failed: {err}"));
        }
        Ok(text)
    } else {
        Err(format!("esptool failed: {text}"))
    }
}

fn requested_ota_transport(mode: &str) -> Result<Option<TransportKind>, String> {
    match mode {
        "current" | "" => Ok(None),
        "usb" => Ok(Some(TransportKind::Usb)),
        "ble" => Ok(Some(TransportKind::Ble)),
        other => Err(format!("unknown OTA transport mode: {other}")),
    }
}

fn ensure_ota_transport(
    app: &AppHandle,
    state: &SharedState,
    transport_mode: &str,
) -> Result<TransportKind, String> {
    let requested = requested_ota_transport(transport_mode)?;
    if let Some(kind) = requested {
        let already_connected = {
            let inner = state.inner.lock().map_err(|_| "state lock poisoned")?;
            inner.transport.is_some() && inner.transport_kind == kind
        };
        if !already_connected {
            let settings_snapshot = {
                let inner = state.inner.lock().map_err(|_| "state lock poisoned")?;
                inner.settings.clone()
            };
            let _ = save_settings_to_disk(app, &settings_snapshot);
            connect_device_inner(&app, &state, None)?;
        }
    }

    let kind = {
        let inner = state.inner.lock().map_err(|_| "state lock poisoned")?;
        if inner.transport.is_none() {
            return Err("device is not connected".to_string());
        }
        inner.transport_kind
    };
    if kind == TransportKind::None {
        return Err("device is not connected".to_string());
    }
    Ok(kind)
}

fn wait_for_ota_auth(state: &SharedState, kind: TransportKind) -> Result<(), String> {
    if kind == TransportKind::Usb {
        return Ok(());
    }
    for _ in 0..60 {
        let authenticated = {
            state
                .inner
                .lock()
                .map_err(|_| "state lock poisoned")?
                .authenticated
        };
        if authenticated {
            return Ok(());
        }
        thread::sleep(Duration::from_millis(50));
    }
    Err("board is not authenticated; pair the BLE board first".to_string())
}

fn expect_ota_ok(response: serde_json::Value) -> Result<serde_json::Value, String> {
    if response
        .get("ok")
        .and_then(|value| value.as_bool())
        .unwrap_or(false)
    {
        Ok(response)
    } else {
        Err(response
            .get("error")
            .and_then(|value| value.as_str())
            .unwrap_or("OTA request failed")
            .to_string())
    }
}

#[tauri::command]
fn ota_flash_firmware(
    app: AppHandle,
    state: State<SharedState>,
    firmware_path: String,
    transport_mode: String,
) -> Result<String, String> {
    let firmware = PathBuf::from(firmware_path.trim());
    if !firmware.exists() {
        return Err("firmware file does not exist".to_string());
    }
    let image = fs::read(&firmware).map_err(|err| format!("read firmware failed: {err}"))?;
    if image.is_empty() {
        return Err("firmware file is empty".to_string());
    }
    let sha256 = format!("{:x}", Sha256::digest(&image));
    let kind = ensure_ota_transport(&app, state.inner(), &transport_mode)?;
    wait_for_ota_auth(state.inner(), kind)?;

    log_event(
        &app,
        "INFO",
        "ota",
        "-",
        format!(
            "OTA start via {:?}: {} ({} bytes)",
            kind,
            firmware.display(),
            image.len()
        ),
    );

    let begin_id = Uuid::new_v4().to_string();
    expect_ota_ok(board_request(
        state.inner(),
        serde_json::json!({
            "type": "ota_begin",
            "request_id": begin_id,
            "size": image.len(),
            "sha256": sha256,
        }),
        Duration::from_secs(15),
    )?)?;

    let total = image.len();
    let mut offset = 0usize;
    let mut next_log_percent = 0usize;
    while offset < total {
        let end = (offset + OTA_CHUNK_BYTES).min(total);
        let chunk = &image[offset..end];
        let request_id = Uuid::new_v4().to_string();
        let encoded = general_purpose::STANDARD.encode(chunk);
        let response = expect_ota_ok(board_request(
            state.inner(),
            serde_json::json!({
                "type": "ota_chunk",
                "request_id": request_id,
                "offset": offset,
                "data_b64": encoded,
            }),
            Duration::from_secs(10),
        )?)?;
        offset = response
            .get("offset")
            .and_then(|value| value.as_u64())
            .map(|value| value as usize)
            .unwrap_or(end);
        let percent = (offset * 100) / total;
        if percent >= next_log_percent || offset == total {
            log_event(&app, "INFO", "ota", "-", format!("OTA progress: {percent}%"));
            next_log_percent = (percent + 5).min(100);
        }
    }

    let end_id = Uuid::new_v4().to_string();
    expect_ota_ok(board_request(
        state.inner(),
        serde_json::json!({"type": "ota_end", "request_id": end_id}),
        Duration::from_secs(20),
    )?)?;
    log_event(&app, "INFO", "ota", "-", "OTA complete; board is rebooting");
    Ok("OTA complete; board is rebooting".to_string())
}

#[tauri::command]
fn start_recording(app: AppHandle, state: State<SharedState>) -> Result<(), String> {
    start_recording_inner(app, state.inner())
}

fn start_recording_inner(app: AppHandle, state: &SharedState) -> Result<(), String> {
    {
        let inner = state.inner.lock().map_err(|_| "state lock poisoned")?;
        if inner.recording.is_some() {
            return Ok(());
        }
        if inner.busy {
            return Err("transcription is already running".to_string());
        }
    }
    let target_bundle_id = frontmost_bundle_id();

    let settings = state
        .inner
        .lock()
        .map_err(|_| "state lock poisoned")?
        .settings
        .clone();
    if settings.asr_provider != "doubao" {
        return Err(format!(
            "ASR provider '{}' is not implemented yet.",
            settings.asr_provider
        ));
    }
    let asr_provider = settings.asr_provider.clone();
    log_event(
        &app,
        "INFO",
        "voice",
        "asr",
        format!(
            "start requested provider={asr_provider} endpoint={} resource={} device={}",
            settings.doubao_endpoint,
            settings.doubao_resource_id,
            if settings.audio_input_device.trim().is_empty() {
                "default"
            } else {
                settings.audio_input_device.as_str()
            }
        ),
    );
    let api_key = get_doubao_api_key(state)
        .ok_or("Doubao X-Api-Key missing. Paste it into the Voice page and click Save Voice.")?;
    log_event(&app, "INFO", "voice", "asr", "api key available");
    let (stop_tx, stop_rx) = tokio::sync::oneshot::channel::<()>();
    let (done_tx, done_rx) = mpsc::channel();
    let thread_app = app.clone();
    let thread_state = state.clone();
    thread::spawn(move || {
        let result = tauri::async_runtime::block_on(stream_doubao_asr(
            thread_app.clone(),
            settings,
            api_key,
            stop_rx,
        ));
        if let Err(err) = &result {
            mark_recording_error(&thread_state, err.clone());
            emit_voice(&thread_app, "error", err.clone());
            log_error(&thread_app, "voice", "asr", err.clone());
        }
        let _ = done_tx.send(result);
    });

    {
        let mut inner = state.inner.lock().map_err(|_| "state lock poisoned")?;
        inner.recording = Some(RecordingHandle {
            stop_tx,
            done_rx,
            target_bundle_id,
        });
        inner.last_error.clear();
        inner.last_partial.clear();
    }
    write_voice_state(state, "recording");
    // Sound + overlay for all recording paths (board button or global shortcut).
    // Emit this only after validation has succeeded and recording state exists.
    hide_main_window_inner(&app);
    play_sound("Tink");
    let _ = app.emit("voice-recording-start", ());
    log_event(&app, "INFO", "voice", "asr", format!("recording started provider={asr_provider}"));
    Ok(())
}

#[tauri::command]
fn stop_recording_and_transcribe(app: AppHandle, state: State<SharedState>) -> Result<(), String> {
    stop_recording_and_transcribe_inner(app, state.inner(), false)
}

fn stop_recording_and_transcribe_inner(
    app: AppHandle,
    state: &SharedState,
    submit_after_paste: bool,
) -> Result<(), String> {
    let recording = {
        let mut inner = state.inner.lock().map_err(|_| "state lock poisoned")?;
        let Some(recording) = inner.recording.take() else {
            return Ok(());
        };
        inner.busy = true;
        recording
    };

    // Send the stop signal to the recording thread. It will send the final
    // audio frame, wait for the server response, paste if configured, and emit
    // a voice-event with the result. We do NOT block the UI thread waiting.
    let _ = recording.stop_tx.send(());
    play_sound("Pop");
    write_voice_state(state, "transcribing");

    let state_clone = state.clone();
    let app_clone = app.clone();
    thread::spawn(move || {
        let transcript = match recording.done_rx.recv_timeout(STOP_WAIT_TIMEOUT) {
            Ok(result) => result,
            Err(_) => {
                Err("Timed out while stopping transcription.".to_string())
            }
        };
        match transcript {
            Ok(text) => {
                {
                    if let Ok(mut inner) = state_clone.inner.lock() {
                        inner.busy = false;
                        inner.last_transcript = text.clone();
                        inner.last_partial.clear();
                        inner.last_error.clear();
                    }
                }
                emit_voice(&app_clone, "final", &text);
                log_event(
                    &app_clone,
                    "INFO",
                    "voice",
                    "asr",
                    format!("final transcript chars={}", text.chars().count()),
                );
                thread::sleep(Duration::from_millis(250));
                // Let the overlay show the final transcript briefly, then close
                // it so focus returns to the user's app before paste.
                let _ = app_clone.emit("voice-recording-stop", ());
                close_voice_overlay(&app_clone);
                thread::sleep(Duration::from_millis(120));
                if let Some(bundle_id) = recording.target_bundle_id.as_deref() {
                    activate_app(bundle_id);
                    thread::sleep(Duration::from_millis(180));
                }

                let paste = state_clone
                    .inner
                    .lock()
                    .map(|inner| inner.settings.paste_after_transcribe)
                    .unwrap_or(false);
                if paste && !text.trim().is_empty() {
                    let _ = paste_text(&text);
                    if submit_after_paste {
                        thread::sleep(Duration::from_millis(80));
                        let _ = press_return_key();
                    }
                }
                write_voice_state(&state_clone, "idle");
            }
            Err(err) => {
                if let Ok(mut inner) = state_clone.inner.lock() {
                    inner.busy = false;
                    inner.last_error = err.clone();
                }
                emit_voice(&app_clone, "error", &err);
                log_error(&app_clone, "voice", "asr", err);
                write_voice_state(&state_clone, "idle");
            }
        }
    });
    Ok(())
}

fn close_voice_overlay(app: &AppHandle) {
    if let Some(win) = app.get_webview_window("voice-overlay") {
        let _ = win.close();
        let _ = win.destroy();
    }
}

#[tauri::command]
fn cancel_recording(app: AppHandle, state: State<SharedState>) -> Result<(), String> {
    cancel_recording_inner(app, state.inner())
}

fn cancel_recording_inner(app: AppHandle, state: &SharedState) -> Result<(), String> {
    let recording = {
        let mut inner = state.inner.lock().map_err(|_| "state lock poisoned")?;
        inner.busy = false;
        inner.recording.take()
    };
    if let Some(recording) = recording {
        let _ = recording.stop_tx.send(());
        // Don't block waiting — just drop the handle; the thread will exit.
        play_sound("Basso");
        log_event(&app, "INFO", "voice", "asr", "recording canceled");
    }
    write_voice_state(state, "idle");
    let _ = app.emit("voice-recording-stop", ());
    close_voice_overlay(&app);
    Ok(())
}

async fn stream_doubao_asr(
    app: AppHandle,
    settings: Settings,
    api_key: String,
    stop_rx: tokio::sync::oneshot::Receiver<()>,
) -> Result<String, String> {
    if settings.doubao_endpoint.trim().is_empty() {
        return Err("Doubao endpoint is not configured".to_string());
    }
    if settings.doubao_resource_id.trim().is_empty() {
        return Err("Doubao resource id is not configured".to_string());
    }

    let request_id = Uuid::new_v4().to_string();
    let mut request = settings
        .doubao_endpoint
        .as_str()
        .into_client_request()
        .map_err(|err| format!("Doubao websocket request failed: {err}"))?;
    request.headers_mut().insert(
        "X-Api-Key",
        api_key.parse().map_err(|_| "invalid X-Api-Key header")?,
    );
    request.headers_mut().insert(
        "X-Api-Resource-Id",
        settings
            .doubao_resource_id
            .parse()
            .map_err(|_| "invalid resource id header")?,
    );
    request.headers_mut().insert(
        "X-Api-Request-Id",
        request_id
            .parse()
            .map_err(|_| "invalid request id header")?,
    );
    request
        .headers_mut()
        .insert("X-Api-Sequence", "-1".parse().unwrap());

    let (ws, _) = tokio::time::timeout(WS_CONNECT_TIMEOUT, connect_async(request))
        .await
        .map_err(|_| "Doubao websocket connect timed out".to_string())?
        .map_err(|err| format!("Doubao websocket connect failed: {err}"))?;
    log_event(&app, "INFO", "voice", "asr", "websocket connected provider=doubao");
    let (mut sink, mut stream) = ws.split();

    let (audio_tx, mut audio_rx) = tokio::sync::mpsc::unbounded_channel::<Vec<i16>>();
    let audio_stream = start_audio_stream(audio_tx, &settings.audio_input_device)?;
    audio_stream
        .play()
        .map_err(|err| format!("start audio stream failed: {err}"))?;
    log_event(&app, "INFO", "voice", "audio", "input stream started");

    let mut full_request = serde_json::json!({
        "user": {
            "uid": "kiro-keyboard-companion",
            "platform": std::env::consts::OS,
            "app_version": "0.2.0"
        },
        "audio": {
            "format": "pcm",
            "codec": "raw",
            "rate": 16000,
            "bits": 16,
            "channel": 1
        },
        "request": {
            "model_name": "bigmodel",
            "enable_itn": true,
            "show_utterances": true,
            "result_type": "full"
        }
    });
    if !settings.doubao_language.trim().is_empty() {
        full_request["audio"]["language"] = serde_json::Value::String(settings.doubao_language);
    }
    let payload = serde_json::to_vec(&full_request).map_err(|err| err.to_string())?;
    // full client request carries sequence 1 (POS_SEQUENCE). Audio frames then
    // continue from sequence 2.
    send_ws_binary(
        &mut sink,
        build_full_request_frame(1, &payload),
        "send full request",
    )
    .await?;

    let mut final_text = String::new();
    let mut sequence: i32 = 2;
    const SAMPLES_PER_PACKET: usize = 16_000 / 1000 * 200; // 200ms @ 16kHz mono
    let mut pending: Vec<i16> = Vec::with_capacity(SAMPLES_PER_PACKET);
    let mut final_deadline: Option<tokio::time::Instant> = None;
    let mut stop_rx = stop_rx;
    let mut stopped = false;
    let mut audio_packets_sent: u32 = 0;
    loop {
        tokio::select! {
            maybe_samples = audio_rx.recv(), if !stopped => {
                if let Some(samples) = maybe_samples {
                    pending.extend_from_slice(&samples);
                    while pending.len() >= SAMPLES_PER_PACKET {
                        let chunk: Vec<i16> = pending.drain(..SAMPLES_PER_PACKET).collect();
                        let bytes = pcm16_to_le_bytes(&chunk);
                        send_ws_binary(&mut sink, build_audio_frame(sequence, false, &bytes), "send audio").await?;
                        audio_packets_sent += 1;
                        if audio_packets_sent == 1 || audio_packets_sent % 10 == 0 {
                            log_event(
                                &app,
                                "INFO",
                                "voice",
                                "asr",
                                format!("audio packets sent={audio_packets_sent}"),
                            );
                        }
                        sequence += 1;
                    }
                }
            }
            maybe_msg = stream.next() => {
                let Some(msg) = maybe_msg else { break; };
                let msg = msg.map_err(|err| format!("Doubao websocket read failed: {err}"))?;
                if let Message::Binary(bytes) = msg {
                    if let Some(text) = parse_server_frame(&bytes)? {
                        final_text = text.clone();
                        log_event(
                            &app,
                            "INFO",
                            "voice",
                            "asr",
                            format!("partial transcript chars={}", text.chars().count()),
                        );
                        emit_voice(&app, "partial", &text);
                    }
                }
            }
            _ = &mut stop_rx, if !stopped => {
                stopped = true;
                let remainder = std::mem::take(&mut pending);
                let bytes = pcm16_to_le_bytes(&remainder);
                let _ = send_ws_binary(&mut sink, build_audio_frame(-sequence, true, &bytes), "send final audio").await;
                log_event(
                    &app,
                    "INFO",
                    "voice",
                    "asr",
                    format!("final audio sent packets={audio_packets_sent} tail_bytes={}", bytes.len()),
                );
                final_deadline = Some(tokio::time::Instant::now() + Duration::from_millis(8000));
            }
            _ = async { tokio::time::sleep_until(final_deadline.unwrap()).await }, if final_deadline.is_some() => {
                break;
            }
        }
    }
    drop(audio_stream);
    let _ = sink.close().await;
    log_event(
        &app,
        "INFO",
        "voice",
        "asr",
        format!("websocket closed final_chars={}", final_text.chars().count()),
    );
    Ok(final_text)
}

async fn send_ws_binary<S>(sink: &mut S, bytes: Vec<u8>, label: &str) -> Result<(), String>
where
    S: Sink<Message> + Unpin,
    S::Error: std::fmt::Display,
{
    tokio::time::timeout(WS_SEND_TIMEOUT, sink.send(Message::Binary(bytes.into())))
        .await
        .map_err(|_| format!("{label} timed out"))?
        .map_err(|err| format!("{label} failed: {err}"))
}

/// Pick the cpal input device matching `name`, falling back to the system
/// default when the name is empty or no longer present.
fn select_input_device(host: &cpal::Host, name: &str) -> Result<cpal::Device, String> {
    if !name.trim().is_empty() {
        if let Ok(devices) = host.input_devices() {
            for device in devices {
                if device.name().map(|n| n == name).unwrap_or(false) {
                    return Ok(device);
                }
            }
        }
    }
    host.default_input_device()
        .ok_or_else(|| "no input device available".to_string())
}

fn start_audio_stream(
    audio_tx: tokio::sync::mpsc::UnboundedSender<Vec<i16>>,
    device_name: &str,
) -> Result<cpal::Stream, String> {
    let host = cpal::default_host();
    let device = select_input_device(&host, device_name)?;
    let supported = device
        .default_input_config()
        .map_err(|err| format!("default input config failed: {err}"))?;
    let sample_rate = supported.sample_rate().0;
    let channels = supported.channels();
    let err_fn = |err| eprintln!("audio stream error: {err}");
    match supported.sample_format() {
        cpal::SampleFormat::I16 => device
            .build_input_stream(
                &supported.clone().into(),
                move |data: &[i16], _| {
                    let _ = audio_tx.send(convert_i16_to_16k_mono(data, channels, sample_rate));
                },
                err_fn,
                None,
            )
            .map_err(|err| format!("build audio stream failed: {err}")),
        cpal::SampleFormat::F32 => device
            .build_input_stream(
                &supported.clone().into(),
                move |data: &[f32], _| {
                    let samples = data
                        .iter()
                        .map(|sample| {
                            (*sample * i16::MAX as f32).clamp(i16::MIN as f32, i16::MAX as f32)
                                as i16
                        })
                        .collect::<Vec<_>>();
                    let _ = audio_tx.send(convert_i16_to_16k_mono(&samples, channels, sample_rate));
                },
                err_fn,
                None,
            )
            .map_err(|err| format!("build audio stream failed: {err}")),
        cpal::SampleFormat::U16 => device
            .build_input_stream(
                &supported.into(),
                move |data: &[u16], _| {
                    let samples = data
                        .iter()
                        .map(|sample| {
                            (*sample as i32 - 32768).clamp(i16::MIN as i32, i16::MAX as i32) as i16
                        })
                        .collect::<Vec<_>>();
                    let _ = audio_tx.send(convert_i16_to_16k_mono(&samples, channels, sample_rate));
                },
                err_fn,
                None,
            )
            .map_err(|err| format!("build audio stream failed: {err}")),
        other => Err(format!("unsupported input sample format: {other:?}")),
    }
}

fn convert_i16_to_16k_mono(samples: &[i16], channels: u16, sample_rate: u32) -> Vec<i16> {
    let channels = channels.max(1) as usize;
    let mono = samples
        .chunks(channels)
        .map(|frame| {
            let sum = frame.iter().map(|sample| *sample as i32).sum::<i32>();
            (sum / frame.len().max(1) as i32).clamp(i16::MIN as i32, i16::MAX as i32) as i16
        })
        .collect::<Vec<_>>();

    if sample_rate == 16000 {
        return mono;
    }
    if mono.is_empty() || sample_rate == 0 {
        return Vec::new();
    }

    let out_len = ((mono.len() as u64 * 16000) / sample_rate as u64).max(1) as usize;
    let mut out = Vec::with_capacity(out_len);
    for index in 0..out_len {
        let src_index = (index as u64 * sample_rate as u64 / 16000) as usize;
        out.push(mono[src_index.min(mono.len() - 1)]);
    }
    out
}

/// full client request frame: message type 0x1, POS_SEQUENCE flag (0x1), JSON
/// serialization (0x1), no compression. Layout: Header | Sequence | PayloadSize
/// | Payload. Made `pub` so the asr_probe example exercises the real code path.
pub fn build_full_request_frame(sequence: i32, payload: &[u8]) -> Vec<u8> {
    // byte0 ver/hsize, byte1 mtype(0x1)/flags(0x1), byte2 serial(JSON 0x1)/compress(0x0)
    let mut frame = vec![0x11, (0x1 << 4) | 0x1, 0x10, 0x00];
    frame.extend_from_slice(&sequence.to_be_bytes());
    frame.extend_from_slice(&(payload.len() as u32).to_be_bytes());
    frame.extend_from_slice(payload);
    frame
}

/// audio only request frame: message type 0x2. Non-final packets use a positive
/// sequence (flag 0x1); the final packet uses a negative sequence (flag 0x3).
pub fn build_audio_frame(sequence: i32, final_packet: bool, payload: &[u8]) -> Vec<u8> {
    let flags = if final_packet { 0x3 } else { 0x1 };
    let mut frame = vec![0x11, (0x2 << 4) | flags, 0x00, 0x00];
    frame.extend_from_slice(&sequence.to_be_bytes());
    frame.extend_from_slice(&(payload.len() as u32).to_be_bytes());
    frame.extend_from_slice(payload);
    frame
}

pub fn pcm16_to_le_bytes(samples: &[i16]) -> Vec<u8> {
    let mut bytes = Vec::with_capacity(samples.len() * 2);
    for sample in samples {
        bytes.extend_from_slice(&sample.to_le_bytes());
    }
    bytes
}

/// Decompress a payload slice when the header's compression nibble marks it as
/// gzip; otherwise return it as-is. Defensive: the live service currently
/// answers uncompressed even when the request is gzipped, but the protocol
/// allows compressed responses.
fn decode_payload(raw: &[u8], compression: u8) -> Vec<u8> {
    if compression == 0x1 {
        let mut decoder = GzDecoder::new(raw);
        let mut out = Vec::new();
        if decoder.read_to_end(&mut out).is_ok() {
            return out;
        }
    }
    raw.to_vec()
}

/// Parse a server frame. Returns the transcript text for a full server response,
/// `None` for frames without usable text (acks, empty results), or an `Err`
/// carrying the server's code+message for error frames.
///
/// Frame layout depends on flags: the full-request ack uses flag 0x0 (no
/// sequence field), while audio responses use flag 0x1 (4-byte sequence after
/// the header). `pub` so the asr_probe example tests the real parser.
pub fn parse_server_frame(bytes: &[u8]) -> Result<Option<String>, String> {
    if bytes.len() < 8 {
        return Ok(None);
    }
    let message_type = bytes[1] >> 4;
    let flags = bytes[1] & 0x0f;
    let compression = bytes[2] & 0x0f;
    let mut offset = 4usize;
    if flags == 0x1 || flags == 0x3 {
        if bytes.len() < offset + 4 {
            return Ok(None);
        }
        offset += 4;
    }
    if message_type == 0xF {
        if bytes.len() < offset + 8 {
            return Err("Doubao returned malformed error frame".to_string());
        }
        let code = u32::from_be_bytes(bytes[offset..offset + 4].try_into().unwrap());
        offset += 4;
        let size = u32::from_be_bytes(bytes[offset..offset + 4].try_into().unwrap()) as usize;
        offset += 4;
        let raw = bytes.get(offset..offset + size).unwrap_or_default();
        let message = decode_payload(raw, compression);
        return Err(format!(
            "Doubao error {code}: {}",
            String::from_utf8_lossy(&message)
        ));
    }
    if message_type != 0x9 || bytes.len() < offset + 4 {
        return Ok(None);
    }
    let size = u32::from_be_bytes(bytes[offset..offset + 4].try_into().unwrap()) as usize;
    offset += 4;
    let raw = bytes.get(offset..offset + size).unwrap_or_default();
    let payload = decode_payload(raw, compression);
    let value = serde_json::from_slice::<serde_json::Value>(&payload)
        .map_err(|err| format!("Doubao response JSON failed: {err}"))?;
    Ok(extract_transcript(&value))
}

pub fn extract_transcript(value: &serde_json::Value) -> Option<String> {
    if let Some(text) = value
        .get("result")
        .and_then(|v| v.get("text"))
        .and_then(|v| v.as_str())
    {
        if !text.trim().is_empty() {
            return Some(text.to_string());
        }
    }
    if let Some(result) = value.get("result").and_then(|v| v.as_array()) {
        let text = result
            .iter()
            .filter_map(|item| item.get("text").and_then(|v| v.as_str()))
            .collect::<Vec<_>>()
            .join("");
        if !text.trim().is_empty() {
            return Some(text);
        }
    }
    for path in [
        &["text"][..],
        &["result", "utterances", "0", "text"][..],
        &["data", "text"][..],
    ] {
        if let Some(text) = value_at_path(value, path).and_then(|v| v.as_str()) {
            if !text.trim().is_empty() {
                return Some(text.to_string());
            }
        }
    }
    None
}

fn value_at_path<'a>(value: &'a serde_json::Value, path: &[&str]) -> Option<&'a serde_json::Value> {
    let mut current = value;
    for part in path {
        if let Ok(index) = part.parse::<usize>() {
            current = current.as_array()?.get(index)?;
        } else {
            current = current.get(*part)?;
        }
    }
    Some(current)
}

fn paste_text(text: &str) -> Result<(), String> {
    Clipboard::new()
        .map_err(|err| format!("clipboard unavailable: {err}"))?
        .set_text(text.to_string())
        .map_err(|err| format!("clipboard write failed: {err}"))?;

    #[cfg(target_os = "macos")]
    {
        macos_release_command_key()?;
        macos_key_event(55, true)?; // kVK_Command
        std::thread::sleep(std::time::Duration::from_millis(20));
        macos_tap_key(9)?; // kVK_ANSI_V
        std::thread::sleep(std::time::Duration::from_millis(20));
        macos_key_event(55, false)?;
    }

    Ok(())
}

fn press_return_key() -> Result<(), String> {
    #[cfg(target_os = "macos")]
    {
        macos_release_command_key()?;
        macos_tap_key(36)?; // kVK_Return
    }
    Ok(())
}

fn press_backspace_key() -> Result<(), String> {
    #[cfg(target_os = "macos")]
    {
        macos_release_command_key()?;
        macos_tap_key(51)?; // kVK_Delete (Backspace)
    }
    Ok(())
}

fn press_command_right_bracket_key() -> Result<(), String> {
    #[cfg(target_os = "macos")]
    {
        macos_release_command_key()?;
        macos_key_event(55, true)?; // kVK_Command
        std::thread::sleep(std::time::Duration::from_millis(20));
        macos_tap_key(30)?; // kVK_ANSI_RightBracket
        std::thread::sleep(std::time::Duration::from_millis(20));
        macos_key_event(55, false)?;
    }
    Ok(())
}

#[cfg(target_os = "macos")]
fn macos_key_event(keycode: u16, down: bool) -> Result<(), String> {
    let source = core_graphics::event_source::CGEventSource::new(
        core_graphics::event_source::CGEventSourceStateID::HIDSystemState,
    )
    .map_err(|_| "CGEventSource creation failed".to_string())?;
    let event = core_graphics::event::CGEvent::new_keyboard_event(source, keycode, down)
        .map_err(|_| "CGEvent keyboard event creation failed".to_string())?;
    event.post(core_graphics::event::CGEventTapLocation::HID);
    Ok(())
}

#[cfg(target_os = "macos")]
fn macos_tap_key(keycode: u16) -> Result<(), String> {
    macos_key_event(keycode, true)?;
    std::thread::sleep(std::time::Duration::from_millis(30));
    macos_key_event(keycode, false)
}

#[cfg(target_os = "macos")]
fn macos_release_command_key() -> Result<(), String> {
    macos_key_event(55, false) // kVK_Command
}

fn spawn_hook_server(app: AppHandle, state: SharedState) {
    thread::spawn(move || {
        let listener = match TcpListener::bind(HOOK_ADDR) {
            Ok(listener) => listener,
            Err(err) => {
                log_error(&app, "hook", "http", format!("server bind failed: {err}"));
                return;
            }
        };
        log_event(&app, "INFO", "hook", "http", format!("server listening addr={HOOK_ADDR}"));
        for stream in listener.incoming().flatten() {
            handle_hook_stream(&app, &state, stream);
        }
    });
}

fn handle_hook_stream(app: &AppHandle, state: &SharedState, mut stream: TcpStream) {
    let cloned = match stream.try_clone() {
        Ok(cloned) => cloned,
        Err(_) => return,
    };
    let mut reader = BufReader::new(cloned);
    let mut content_length = 0usize;
    loop {
        let mut line = String::new();
        match reader.read_line(&mut line) {
            Ok(0) | Err(_) => return,
            Ok(_) => {
                let trimmed = line.trim();
                if trimmed.is_empty() {
                    break;
                }
                if let Some(value) = trimmed.strip_prefix("Content-Length:") {
                    content_length = value.trim().parse().unwrap_or(0);
                } else if let Some(value) = trimmed.strip_prefix("content-length:") {
                    content_length = value.trim().parse().unwrap_or(0);
                }
            }
        }
    }
    let mut body_bytes = vec![0u8; content_length];
    if reader.read_exact(&mut body_bytes).is_err() {
        let _ = stream.write_all(b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
        return;
    }
    let body = String::from_utf8_lossy(&body_bytes);
    let Ok(payload) = serde_json::from_str::<HookPayload>(&body) else {
        let _ = stream.write_all(b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
        return;
    };
    let value = serde_json::to_value(payload).unwrap_or_else(|_| serde_json::json!({}));
    let _ = write_board_json_from_arc(state, &value);
    log_event(app, "INFO", "hook", "http", format!("forwarded {value}"));
    let _ = stream.write_all(b"HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n");
}

pub fn run() {
    let _ = rustls::crypto::ring::default_provider().install_default();
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_global_shortcut::Builder::new().build())
        .plugin(tauri_plugin_dialog::init())
        .setup(|app| {
            let settings = load_settings(app.handle());
            let state = Arc::new(AppState {
                inner: Mutex::new(AppStateInner {
                    settings,
                    logs: Vec::new(),
                    transport: None,
                    transport_kind: TransportKind::None,
                    endpoint: String::new(),
                    device_name: String::new(),
                    connection_id: 0,
                    missed_pongs: 0,
                    recording: None,
                    busy: false,
                    authenticated: false,
                    pairing_code: String::new(),
                    last_transcript: String::new(),
                    last_partial: String::new(),
                    last_error: String::new(),
                    pending_requests: HashMap::new(),
                }),
            });
            spawn_hook_server(app.handle().clone(), state.clone());
            app.manage(state.clone());
            spawn_connection_manager(app.handle().clone(), state);

            // Tray icon — app lives in the menu bar, not the Dock.
            use tauri::tray::{MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent};
            use tauri::menu::{Menu, MenuItem};

            let show_item = MenuItem::with_id(app, "show", "Show", true, None::<&str>)?;
            let quit_item = MenuItem::with_id(app, "quit", "Quit", true, None::<&str>)?;
            let menu = Menu::with_items(app, &[&show_item, &quit_item])?;

            TrayIconBuilder::new()
                .icon(app.default_window_icon().cloned().unwrap())
                .menu(&menu)
                .on_menu_event(|app, event| {
                    match event.id.as_ref() {
                        "show" => {
                            if let Some(win) = app.get_webview_window("main") {
                                let _ = win.show();
                                let _ = win.set_focus();
                            }
                        }
                        "quit" => std::process::exit(0),
                        _ => {}
                    }
                })
                .on_tray_icon_event(|tray, event| {
                    if let TrayIconEvent::Click { button: MouseButton::Left, button_state: MouseButtonState::Up, .. } = event {
                        let app = tray.app_handle();
                        if let Some(win) = app.get_webview_window("main") {
                            let _ = win.show();
                            let _ = win.set_focus();
                        }
                    }
                })
                .build(app)?;

            // Hide from Dock (accessory app).
            #[cfg(target_os = "macos")]
            app.set_activation_policy(tauri::ActivationPolicy::Accessory);

            // Closing the main window hides it instead of quitting (tray app).
            if let Some(win) = app.get_webview_window("main") {
                let w = win.clone();
                win.on_window_event(move |event| {
                    if let tauri::WindowEvent::CloseRequested { api, .. } = event {
                        api.prevent_close();
                        let _ = w.hide();
                    }
                });
            }

            // Auto-connect: if already paired, connect to the board on startup
            // so the user never has to manually click Connect.
            let auto_app = app.handle().clone();
            thread::spawn(move || {
                // Brief delay to let BLE stack settle after boot.
                thread::sleep(Duration::from_secs(2));
                let Some(state) = auto_app.try_state::<SharedState>() else { return; };
                let should_connect = state
                    .inner
                    .lock()
                    .map(|inner| {
                        inner.transport.is_none()
                            && !inner.settings.paired_board_id.trim().is_empty()
                    })
                    .unwrap_or(false);
                if should_connect {
                    let _ = connect_device_inner(&auto_app, state.inner(), None);
                }
            });

            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            get_settings,
            save_settings,
            save_doubao_api_key,
            has_doubao_api_key,
            get_status,
            get_logs,
            clear_logs,
            list_serial_ports,
            list_ble_devices,
            list_audio_input_devices,
            test_log_event,
            connect_device,
            disconnect_device,
            start_pairing,
            forget_device,
            set_hid_output,
            get_keymap,
            set_keymap,
            flash_firmware,
            ota_flash_firmware,
            start_recording,
            stop_recording_and_transcribe,
            cancel_recording,
            hide_main_window,
        ])
        .run(tauri::generate_context!())
        .expect("error while running Kiro Keyboard Companion");
}

#[cfg(test)]
mod tests {
    use super::*;

    fn test_state_with_recording(done_rx: Receiver<Result<String, String>>) -> SharedState {
        let (stop_tx, _stop_rx) = tokio::sync::oneshot::channel::<()>();
        Arc::new(AppState {
                inner: Mutex::new(AppStateInner {
                    settings: Settings::default(),
                    logs: Vec::new(),
                    transport: None,
                    transport_kind: TransportKind::None,
                    endpoint: String::new(),
                    device_name: String::new(),
                    connection_id: 0,
                    missed_pongs: 0,
                    recording: Some(RecordingHandle {
                        stop_tx,
                        done_rx,
                        target_bundle_id: None,
                    }),
                    busy: true,
                    authenticated: false,
                    pairing_code: String::new(),
                    last_transcript: String::new(),
                    last_partial: String::new(),
                    last_error: String::new(),
                    pending_requests: HashMap::new(),
                }),
        })
    }

    #[test]
    fn recording_error_does_not_drop_worker_result_receiver() {
        let (done_tx, done_rx) = mpsc::channel();
        let state = test_state_with_recording(done_rx);

        mark_recording_error(&state, "websocket failed".to_string());

        done_tx
            .send(Err("websocket failed".to_string()))
            .expect("done_rx should still be alive after marking the error");

        let mut inner = state.inner.lock().unwrap();
        assert!(inner.recording.is_some());
        assert!(!inner.busy);
        assert_eq!(inner.last_error, "websocket failed");
        let recording = inner.recording.take().unwrap();
        drop(inner);
        assert_eq!(
            recording
                .done_rx
                .recv_timeout(Duration::from_millis(50))
                .unwrap(),
            Err("websocket failed".to_string())
        );
    }

    #[test]
    fn normalizes_legacy_doubao_engine_to_third_party_provider() {
        let mut settings = Settings {
            voice_engine: "doubao".to_string(),
            asr_provider: String::new(),
            ..Settings::default()
        };

        normalize_voice_settings(&mut settings);

        assert_eq!(settings.voice_engine, "third_party");
        assert_eq!(settings.asr_provider, "doubao");
        assert!(is_third_party_voice_engine(&settings.voice_engine));
    }

    #[test]
    fn voice_engine_payload_includes_asr_provider() {
        let payload = protocol::voice_engine_payload("third_party", "doubao");

        assert_eq!(payload["type"], "voice_engine");
        assert_eq!(payload["engine"], "third_party");
        assert_eq!(payload["asr_provider"], "doubao");
    }

    #[test]
    fn parses_doubao_server_response_text() {
        let payload = br#"{"result":{"text":"hello kiro"}}"#;
        let mut frame = vec![0x11, 0x90, 0x10, 0x00];
        frame.extend_from_slice(&(payload.len() as u32).to_be_bytes());
        frame.extend_from_slice(payload);

        assert_eq!(
            parse_server_frame(&frame).unwrap(),
            Some("hello kiro".to_string())
        );
    }

    #[test]
    fn parses_audio_response_with_sequence_field() {
        // Audio responses use flag 0x1 and carry a 4-byte sequence after the
        // header, before the payload size. This mirrors the live frames the
        // asr_probe captured.
        let payload =
            r#"{"audio_info":{"duration":600},"result":{"text":"你好，世界"}}"#.as_bytes();
        let mut frame = vec![0x11, 0x91, 0x10, 0x00];
        frame.extend_from_slice(&5i32.to_be_bytes());
        frame.extend_from_slice(&(payload.len() as u32).to_be_bytes());
        frame.extend_from_slice(payload);

        assert_eq!(
            parse_server_frame(&frame).unwrap(),
            Some("你好，世界".to_string())
        );
    }

    #[test]
    fn parses_gzip_compressed_response() {
        use flate2::write::GzEncoder;
        use flate2::Compression;
        let payload = br#"{"result":{"text":"compressed"}}"#;
        let mut encoder = GzEncoder::new(Vec::new(), Compression::default());
        encoder.write_all(payload).unwrap();
        let compressed = encoder.finish().unwrap();
        // compression nibble = 0x1 in byte2.
        let mut frame = vec![0x11, 0x91, 0x11, 0x00];
        frame.extend_from_slice(&1i32.to_be_bytes());
        frame.extend_from_slice(&(compressed.len() as u32).to_be_bytes());
        frame.extend_from_slice(&compressed);

        assert_eq!(
            parse_server_frame(&frame).unwrap(),
            Some("compressed".to_string())
        );
    }

    #[test]
    fn propagates_server_error_frame() {
        let message = b"invalid request";
        // message type 0xF, flag 0x0 (no sequence), code + size + message.
        let mut frame = vec![0x11, 0xF0, 0x10, 0x00];
        frame.extend_from_slice(&45000007u32.to_be_bytes());
        frame.extend_from_slice(&(message.len() as u32).to_be_bytes());
        frame.extend_from_slice(message);

        let err = parse_server_frame(&frame).unwrap_err();
        assert!(err.contains("45000007"), "got: {err}");
        assert!(err.contains("invalid request"), "got: {err}");
    }

    #[test]
    fn full_request_frame_carries_sequence_one_and_json_flags() {
        let frame = build_full_request_frame(1, &[0x7b, 0x7d]); // "{}"
        assert_eq!(frame[0], 0x11);
        // message type 0x1 (full client request) + POS_SEQUENCE flag 0x1.
        assert_eq!(frame[1], 0x11);
        // serialization JSON (0x1), no compression.
        assert_eq!(frame[2], 0x10);
        assert_eq!(i32::from_be_bytes(frame[4..8].try_into().unwrap()), 1);
        assert_eq!(u32::from_be_bytes(frame[8..12].try_into().unwrap()), 2);
    }

    #[test]
    fn final_audio_frame_keeps_negative_sequence() {
        let frame = build_audio_frame(-3, true, &[]);
        assert_eq!(frame[0], 0x11);
        assert_eq!(frame[1], 0x23);
        assert_eq!(i32::from_be_bytes(frame[4..8].try_into().unwrap()), -3);
        assert_eq!(u32::from_be_bytes(frame[8..12].try_into().unwrap()), 0);
    }

    #[test]
    fn first_audio_frame_uses_sequence_after_full_request() {
        let frame = build_audio_frame(2, false, &[1, 2, 3, 4]);
        assert_eq!(frame[0], 0x11);
        assert_eq!(frame[1], 0x21);
        assert_eq!(i32::from_be_bytes(frame[4..8].try_into().unwrap()), 2);
        assert_eq!(u32::from_be_bytes(frame[8..12].try_into().unwrap()), 4);
    }

    #[test]
    fn converts_default_device_audio_to_16k_mono() {
        let mut stereo_48k = Vec::new();
        for index in 0..48 {
            stereo_48k.push(index * 2);
            stereo_48k.push(index * 2 + 2);
        }

        let converted = convert_i16_to_16k_mono(&stereo_48k, 2, 48000);

        assert_eq!(converted.len(), 16);
        assert_eq!(converted[0], 1);
        assert_eq!(converted[1], 7);
        assert_eq!(converted[15], 91);
    }
}
