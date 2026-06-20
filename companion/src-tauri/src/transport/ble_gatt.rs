use super::{BleDeviceInfo, BoardTransport, LineSender, TransportKind};
use crate::protocol;
use btleplug::api::{
    Central, CharPropFlags, Manager as _, Peripheral as _, ScanFilter, WriteType,
};
use btleplug::platform::{Manager, Peripheral};
use futures_util::StreamExt;
use serde_json::Value;
use std::sync::mpsc as std_mpsc;
use std::thread;
use std::time::Duration;
use tokio::runtime::Builder;
use tokio::sync::mpsc as tokio_mpsc;
use uuid::Uuid;

const SERVICE_UUID: Uuid = Uuid::from_u128(0x6b69726f_6b62_0001_8000_00805f9b34fb);
const RX_UUID: Uuid = Uuid::from_u128(0x6b69726f_6b62_0002_8000_00805f9b34fb);
const TX_UUID: Uuid = Uuid::from_u128(0x6b69726f_6b62_0003_8000_00805f9b34fb);

pub struct BleGattTransport {
    write_tx: tokio_mpsc::UnboundedSender<BleCommand>,
    endpoint: String,
}

enum BleCommand {
    Write(Vec<u8>),
    Disconnect,
}

impl BleGattTransport {
    pub fn connect(device_id: Option<String>, lines: LineSender) -> Result<Self, String> {
        let (write_tx, write_rx) = tokio_mpsc::unbounded_channel::<BleCommand>();
        let (ready_tx, ready_rx) = std_mpsc::channel::<Result<String, String>>();
        spawn_ble_worker(device_id, lines, write_rx, ready_tx);
        let endpoint = ready_rx
            .recv_timeout(Duration::from_secs(8))
            .map_err(|err| format!("BLE connect timed out: {err}"))??;

        Ok(Self {
            write_tx,
            endpoint,
        })
    }
}

impl BoardTransport for BleGattTransport {
    fn kind(&self) -> TransportKind {
        TransportKind::Ble
    }

    fn endpoint(&self) -> String {
        self.endpoint.clone()
    }

    fn write_json(&mut self, payload: &Value) -> Result<(), String> {
        let line = protocol::encode_json_line(payload)?;
        self.write_tx
            .send(BleCommand::Write(line))
            .map_err(|_| "BLE worker is not running".to_string())
    }

    fn disconnect(&mut self) {
        let _ = self.write_tx.send(BleCommand::Disconnect);
    }
}

pub fn list_ble_devices() -> Result<Vec<BleDeviceInfo>, String> {
    let runtime = Builder::new_current_thread()
        .enable_all()
        .build()
        .map_err(|err| format!("BLE runtime failed: {err}"))?;
    runtime.block_on(async {
        let (_manager, adapter) = default_adapter().await?;
        adapter
            .start_scan(ScanFilter {
                services: vec![SERVICE_UUID],
            })
            .await
            .map_err(|err| format!("BLE scan failed: {err}"))?;
        tokio::time::sleep(Duration::from_millis(1200)).await;
        let peripherals = adapter
            .peripherals()
            .await
            .map_err(|err| format!("BLE peripherals failed: {err}"))?;
        let mut devices = Vec::new();
        for peripheral in peripherals {
            let Ok(Some(props)) = peripheral.properties().await else {
                continue;
            };
            let name = props.local_name.unwrap_or_else(|| "Kiro KB".to_string());
            let raw_address = props.address.to_string();
            let address = if raw_address == "00:00:00:00:00:00" {
                peripheral.id().to_string()
            } else {
                raw_address
            };
            devices.push(BleDeviceInfo {
                id: peripheral.id().to_string(),
                name,
                address,
            });
        }
        Ok(devices)
    })
}

async fn find_peripheral(
    device_id: Option<String>,
) -> Result<(Manager, btleplug::platform::Adapter, Peripheral, String), String> {
    let (manager, adapter) = default_adapter().await?;
    adapter
        .start_scan(ScanFilter {
            services: vec![SERVICE_UUID],
        })
        .await
        .map_err(|err| format!("BLE scan failed: {err}"))?;
    tokio::time::sleep(Duration::from_millis(1500)).await;

    let peripherals = adapter
        .peripherals()
        .await
        .map_err(|err| format!("BLE peripherals failed: {err}"))?;
    for peripheral in peripherals {
        let id = peripheral.id().to_string();
        if device_id.as_deref().is_some_and(|wanted| wanted != id) {
            continue;
        }
        let props = peripheral
            .properties()
            .await
            .map_err(|err| format!("BLE properties failed: {err}"))?;
        let name = props
            .as_ref()
            .and_then(|props| props.local_name.clone())
            .unwrap_or_else(|| "Kiro KB".to_string());
        return Ok((manager, adapter, peripheral, format!("{name} ({id})")));
    }
    Err("ki-board BLE GATT device not found".to_string())
}

async fn default_adapter() -> Result<(Manager, btleplug::platform::Adapter), String> {
    let manager = Manager::new()
        .await
        .map_err(|err| format!("BLE manager failed: {err}"))?;
    let adapters = manager
        .adapters()
        .await
        .map_err(|err| format!("BLE adapters failed: {err}"))?;
    let adapter = adapters
        .into_iter()
        .next()
        .ok_or("no BLE adapter found".to_string())?;
    // Return the Manager alongside the Adapter so the caller can keep the whole
    // CoreBluetooth central alive for the connection's lifetime instead of
    // dropping it right after discovery.
    Ok((manager, adapter))
}

fn spawn_ble_worker(
    device_id: Option<String>,
    lines: LineSender,
    mut write_rx: tokio_mpsc::UnboundedReceiver<BleCommand>,
    ready_tx: std_mpsc::Sender<Result<String, String>>,
) {
    thread::spawn(move || {
        // btleplug's CoreBluetooth backend delivers notifications through a
        // background event-pump task. A current-thread runtime cannot reliably
        // drive that task concurrently with our select loop, so push
        // notifications never arrive even though request/response operations
        // (scan, connect, write, subscribe) work. A multi-threaded runtime keeps
        // the event pump running alongside the worker loop.
        let Ok(runtime) = Builder::new_multi_thread()
            .worker_threads(2)
            .enable_all()
            .build()
        else {
            let _ = ready_tx.send(Err("BLE runtime failed".to_string()));
            return;
        };
        runtime.block_on(async move {
            // Bind the Manager and Adapter for the whole connection lifetime.
            // Dropping them right after discovery (as the old code did) can tear
            // down the CoreBluetooth central that delivers notifications.
            let (_manager, _adapter, peripheral, endpoint) = match find_peripheral(device_id).await {
                Ok(value) => value,
                Err(err) => {
                    let _ = ready_tx.send(Err(err));
                    return;
                }
            };
            if let Err(err) = peripheral.connect().await {
                let _ = ready_tx.send(Err(format!("BLE connect failed: {err}")));
                return;
            }
            if let Err(err) = peripheral.discover_services().await {
                let _ = ready_tx.send(Err(format!("BLE discover services failed: {err}")));
                let _ = peripheral.disconnect().await;
                return;
            }

            let chars = peripheral.characteristics();
            // Diagnostic: surface every discovered characteristic so we can tell
            // the custom Kiro service apart from the BLE HID keyboard service,
            // which also exposes NOTIFY characteristics on the same peripheral.
            for ch in &chars {
                let _ = lines.send(format!(
                    "{{\"type\":\"ble_transport\",\"event\":\"characteristic\",\"uuid\":\"{}\",\"properties\":\"{:?}\"}}",
                    ch.uuid, ch.properties
                ));
            }
            let Some(rx_char) = chars.iter().find(|ch| ch.uuid == RX_UUID).cloned() else {
                let _ = ready_tx.send(Err("BLE RX characteristic not found".to_string()));
                let _ = peripheral.disconnect().await;
                return;
            };
            // Match the Kiro TX characteristic strictly by UUID. The previous
            // `|| properties.contains(NOTIFY)` fallback could accidentally pick a
            // BLE HID input-report characteristic (also NOTIFY) and subscribe to
            // the wrong endpoint, so board notifications were never delivered.
            let Some(tx_char) = chars.iter().find(|ch| ch.uuid == TX_UUID).cloned() else {
                let _ = ready_tx.send(Err("BLE TX characteristic not found".to_string()));
                let _ = peripheral.disconnect().await;
                return;
            };
            if !tx_char.properties.contains(CharPropFlags::NOTIFY) {
                let _ = lines.send(
                    "{\"type\":\"ble_transport\",\"event\":\"warning\",\"detail\":\"tx characteristic missing NOTIFY\"}"
                        .to_string(),
                );
            }

            let mut notifications = match peripheral.notifications().await {
                Ok(stream) => stream,
                Err(err) => {
                    let _ = ready_tx.send(Err(format!("BLE notifications stream failed: {err}")));
                    let _ = peripheral.disconnect().await;
                    return;
                }
            };
            if let Err(err) = peripheral.subscribe(&tx_char).await {
                let _ = ready_tx.send(Err(format!("BLE subscribe failed: {err}")));
                let _ = peripheral.disconnect().await;
                return;
            }
            let _ = lines.send(
                "{\"type\":\"ble_transport\",\"event\":\"subscribed\"}".to_string(),
            );
            let _ = ready_tx.send(Ok(endpoint));

            let mut buffer = Vec::<u8>::new();
            loop {
                tokio::select! {
                    maybe_notification = notifications.next() => {
                        let Some(notification) = maybe_notification else {
                            let _ = lines.send(
                                "{\"type\":\"ble_transport\",\"event\":\"notify_stream_ended\"}".to_string(),
                            );
                            break;
                        };
                        for byte in notification.value {
                            if byte == b'\n' {
                                if let Ok(line) = String::from_utf8(buffer.clone()) {
                                    if lines.send(line.trim().to_string()).is_err() {
                                        let _ = peripheral.disconnect().await;
                                        return;
                                    }
                                }
                                buffer.clear();
                            } else if byte != b'\r' {
                                buffer.push(byte);
                            }
                        }
                    }
                    maybe_command = write_rx.recv() => {
                        match maybe_command {
                            Some(BleCommand::Write(line)) => {
                                // Report the actual write outcome. The previous
                                // unconditional "write" log was emitted before the
                                // BLE write even ran, so it could not tell a real
                                // delivery apart from a silently failing write.
                                let mut write_ok = true;
                                for chunk in line.chunks(180) {
                                    if let Err(err) =
                                        peripheral.write(&rx_char, chunk, WriteType::WithoutResponse).await
                                    {
                                        write_ok = false;
                                        let _ = lines.send(format!(
                                            "{{\"type\":\"ble_transport\",\"event\":\"write_error\",\"detail\":\"{err}\"}}"
                                        ));
                                        break;
                                    }
                                }
                                if write_ok {
                                    let _ = lines.send(
                                        "{\"type\":\"ble_transport\",\"event\":\"write\"}".to_string(),
                                    );
                                }
                            }
                            Some(BleCommand::Disconnect) | None => {
                                break;
                            }
                        }
                    }
                }
            }
            let _ = peripheral.disconnect().await;
        });
    });
}
