#[path = "../src/protocol.rs"]
mod protocol;
#[path = "../src/transport/mod.rs"]
mod transport;

use base64::{engine::general_purpose, Engine as _};
use keyring::Entry;
use serde_json::Value;
use sha2::{Digest, Sha256};
use std::env;
use std::fs;
use std::path::PathBuf;
use std::sync::mpsc::{self, Receiver};
use std::time::{Duration, Instant};
use transport::ble_gatt::BleGattTransport;
use transport::usb_serial::UsbSerialTransport;
use transport::{BoardTransport, TransportKind};
use uuid::Uuid;

const KEYRING_SERVICE: &str = "kiro-keyboard-companion";
const KEYRING_BLE_TOKEN: &str = "ble-pairing-token";
const OTA_CHUNK_BYTES: usize = 512;

#[derive(Debug)]
struct Args {
    firmware: PathBuf,
    transport: TransportKind,
    port: Option<String>,
    device_id: Option<String>,
}

fn usage() -> &'static str {
    "Usage: cargo run --example ota_flash -- --firmware <firmware.bin> [--transport ble|usb] [--port <cdc-port>] [--device-id <ble-id>]"
}

fn parse_args() -> Result<Args, String> {
    let mut firmware = None;
    let mut transport = TransportKind::Ble;
    let mut port = None;
    let mut device_id = None;
    let mut items = env::args().skip(1);
    while let Some(arg) = items.next() {
        match arg.as_str() {
            "--firmware" => {
                firmware = Some(PathBuf::from(
                    items.next().ok_or_else(|| "--firmware needs a path".to_string())?,
                ));
            }
            "--transport" => {
                transport = match items
                    .next()
                    .ok_or_else(|| "--transport needs ble or usb".to_string())?
                    .as_str()
                {
                    "ble" => TransportKind::Ble,
                    "usb" => TransportKind::Usb,
                    other => return Err(format!("unknown transport: {other}")),
                };
            }
            "--port" => {
                port = Some(items.next().ok_or_else(|| "--port needs a value".to_string())?);
            }
            "--device-id" => {
                device_id = Some(
                    items
                        .next()
                        .ok_or_else(|| "--device-id needs a value".to_string())?,
                );
            }
            "--help" | "-h" => return Err(usage().to_string()),
            other => return Err(format!("unknown argument: {other}\n{}", usage())),
        }
    }
    Ok(Args {
        firmware: firmware.ok_or_else(|| usage().to_string())?,
        transport,
        port,
        device_id,
    })
}

fn ble_token() -> Result<String, String> {
    Entry::new(KEYRING_SERVICE, KEYRING_BLE_TOKEN)
        .map_err(|err| format!("keyring unavailable: {err}"))?
        .get_password()
        .map_err(|_| "BLE token missing; pair with the companion app first".to_string())
        .and_then(|token| {
            if token.trim().is_empty() {
                Err("BLE token missing; pair with the companion app first".to_string())
            } else {
                Ok(token)
            }
        })
}

fn recv_json(rx: &Receiver<String>, deadline: Instant) -> Result<Value, String> {
    loop {
        let remaining = deadline.saturating_duration_since(Instant::now());
        if remaining.is_zero() {
            return Err("board response timed out".to_string());
        }
        let line = rx
            .recv_timeout(remaining.min(Duration::from_millis(500)))
            .map_err(|_| "board response timed out".to_string())?;
        let Ok(value) = serde_json::from_str::<Value>(&line) else {
            continue;
        };
        if value.get("type").and_then(|v| v.as_str()) == Some("ble_transport") {
            continue;
        }
        return Ok(value);
    }
}

fn wait_for_type(rx: &Receiver<String>, message_type: &str, timeout: Duration) -> Result<Value, String> {
    let deadline = Instant::now() + timeout;
    loop {
        let value = recv_json(rx, deadline)?;
        match value.get("type").and_then(|v| v.as_str()) {
            Some(found) if found == message_type => return Ok(value),
            Some("auth_required") => return Err("board requires pairing/auth".to_string()),
            _ => {}
        }
    }
}

fn board_request(
    transport: &mut dyn BoardTransport,
    rx: &Receiver<String>,
    payload: Value,
    timeout: Duration,
) -> Result<Value, String> {
    let request_id = payload
        .get("request_id")
        .and_then(|value| value.as_str())
        .ok_or_else(|| "request missing request_id".to_string())?
        .to_string();
    transport.write_json(&payload)?;
    let deadline = Instant::now() + timeout;
    loop {
        let value = recv_json(rx, deadline)?;
        if value.get("type").and_then(|v| v.as_str()) == Some("auth_required") {
            return Err("board requires pairing/auth".to_string());
        }
        if value.get("request_id").and_then(|value| value.as_str()) == Some(request_id.as_str()) {
            if value.get("ok").and_then(|value| value.as_bool()).unwrap_or(false) {
                return Ok(value);
            }
            return Err(value
                .get("error")
                .and_then(|value| value.as_str())
                .unwrap_or("request failed")
                .to_string());
        }
    }
}

fn connect(args: &Args, rx: mpsc::Sender<String>) -> Result<Box<dyn BoardTransport>, String> {
    match args.transport {
        TransportKind::Ble => Ok(Box::new(BleGattTransport::connect(args.device_id.clone(), rx)?)),
        TransportKind::Usb => Ok(Box::new(UsbSerialTransport::connect(args.port.clone(), rx)?)),
        TransportKind::None => Err("invalid transport".to_string()),
    }
}

fn authenticate(
    transport: &mut dyn BoardTransport,
    rx: &Receiver<String>,
    kind: TransportKind,
) -> Result<(), String> {
    transport.write_json(&protocol::hello_payload())?;
    let _ = wait_for_type(rx, "hello_ack", Duration::from_secs(5))?;
    if kind == TransportKind::Ble {
        let token = ble_token()?;
        transport.write_json(&protocol::auth_payload(&token))?;
        let _ = wait_for_type(rx, "auth_ok", Duration::from_secs(5))?;
    }
    Ok(())
}

fn run() -> Result<(), String> {
    let args = parse_args()?;
    let image = fs::read(&args.firmware).map_err(|err| format!("read firmware failed: {err}"))?;
    if image.is_empty() {
        return Err("firmware file is empty".to_string());
    }
    let sha256 = format!("{:x}", Sha256::digest(&image));
    let (line_tx, line_rx) = mpsc::channel();
    let mut transport = connect(&args, line_tx)?;
    println!(
        "[ota] connected via {:?}: {}",
        transport.kind(),
        transport.endpoint()
    );
    authenticate(transport.as_mut(), &line_rx, args.transport)?;
    println!("[ota] authenticated");

    let begin_id = Uuid::new_v4().to_string();
    board_request(
        transport.as_mut(),
        &line_rx,
        serde_json::json!({
            "type": "ota_begin",
            "request_id": begin_id,
            "size": image.len(),
            "sha256": sha256,
        }),
        Duration::from_secs(15),
    )?;

    let total = image.len();
    let mut offset = 0usize;
    let mut next_print = 0usize;
    while offset < total {
        let end = (offset + OTA_CHUNK_BYTES).min(total);
        let encoded = general_purpose::STANDARD.encode(&image[offset..end]);
        let request_id = Uuid::new_v4().to_string();
        let response = board_request(
            transport.as_mut(),
            &line_rx,
            serde_json::json!({
                "type": "ota_chunk",
                "request_id": request_id,
                "offset": offset,
                "data_b64": encoded,
            }),
            Duration::from_secs(10),
        )?;
        offset = response
            .get("offset")
            .and_then(|value| value.as_u64())
            .map(|value| value as usize)
            .unwrap_or(end);
        let percent = (offset * 100) / total;
        if percent >= next_print || offset == total {
            println!("[ota] {percent}%");
            next_print = (percent + 5).min(100);
        }
    }

    let end_id = Uuid::new_v4().to_string();
    board_request(
        transport.as_mut(),
        &line_rx,
        serde_json::json!({"type": "ota_end", "request_id": end_id}),
        Duration::from_secs(20),
    )?;
    println!("[ota] complete; board is rebooting");
    transport.disconnect();
    Ok(())
}

fn main() {
    if let Err(err) = run() {
        eprintln!("[ota] error: {err}");
        std::process::exit(1);
    }
}
