// Standalone btleplug probe to determine, independently of Tauri, whether the
// board's GATT notifications are delivered to btleplug on macOS.
//
// Run with full internal tracing:
//   RUST_LOG=btleplug=trace cargo run --example ble_probe
//
// Make sure the Tauri companion app is NOT connected at the same time (only one
// central can hold the BLE connection).

use std::time::Duration;

use btleplug::api::{Central, Manager as _, Peripheral as _, ScanFilter, WriteType};
use btleplug::platform::Manager;
use futures_util::StreamExt;
use uuid::Uuid;

const SERVICE_UUID: Uuid = Uuid::from_u128(0x6b69726f_6b62_0001_8000_00805f9b34fb);
const RX_UUID: Uuid = Uuid::from_u128(0x6b69726f_6b62_0002_8000_00805f9b34fb);
const TX_UUID: Uuid = Uuid::from_u128(0x6b69726f_6b62_0003_8000_00805f9b34fb);

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    env_logger::init();

    let manager = Manager::new().await?;
    let adapter = manager
        .adapters()
        .await?
        .into_iter()
        .next()
        .ok_or("no BLE adapter")?;

    println!("[probe] scanning for Kiro service...");
    adapter
        .start_scan(ScanFilter {
            services: vec![SERVICE_UUID],
        })
        .await?;
    tokio::time::sleep(Duration::from_millis(2000)).await;

    let peripherals = adapter.peripherals().await?;
    let mut target = None;
    for p in peripherals {
        let props = p.properties().await?;
        let name = props
            .as_ref()
            .and_then(|pr| pr.local_name.clone())
            .unwrap_or_else(|| "<unknown>".into());
        println!("[probe] found {} ({})", name, p.id());
        target = Some(p);
    }
    let peripheral = target.ok_or("Kiro device not found")?;

    println!("[probe] connecting...");
    peripheral.connect().await?;
    println!("[probe] discovering services...");
    peripheral.discover_services().await?;

    let chars = peripheral.characteristics();
    for ch in &chars {
        println!(
            "[probe] char uuid={} service={} props={:?}",
            ch.uuid, ch.service_uuid, ch.properties
        );
    }

    let tx_char = chars
        .iter()
        .find(|ch| ch.uuid == TX_UUID)
        .cloned()
        .ok_or("TX characteristic not found")?;
    let rx_char = chars.iter().find(|ch| ch.uuid == RX_UUID).cloned();

    println!("[probe] subscribing to TX...");
    peripheral.subscribe(&tx_char).await?;
    println!("[probe] subscribed. getting notifications stream...");
    let mut notifications = peripheral.notifications().await?;

    // Send a hello so the board definitely has a reason to notify back.
    if let Some(rx) = rx_char {
        let hello = b"{\"type\":\"hello\",\"protocol\":1,\"client\":\"probe\",\"version\":\"0.0.1\"}\n";
        println!("[probe] writing hello to RX...");
        if let Err(err) = peripheral.write(&rx, hello, WriteType::WithResponse).await {
            println!("[probe] hello write error: {err}");
        }
    }

    println!("[probe] waiting up to 20s for notifications...");
    let deadline = tokio::time::sleep(Duration::from_secs(20));
    tokio::pin!(deadline);
    let mut count = 0usize;
    loop {
        tokio::select! {
            maybe = notifications.next() => {
                match maybe {
                    Some(n) => {
                        count += 1;
                        println!(
                            "[probe] NOTIFICATION #{count} uuid={} {} bytes: {}",
                            n.uuid,
                            n.value.len(),
                            String::from_utf8_lossy(&n.value)
                        );
                    }
                    None => {
                        println!("[probe] notification stream ended");
                        break;
                    }
                }
            }
            _ = &mut deadline => {
                println!("[probe] timeout. total notifications received: {count}");
                break;
            }
        }
    }

    let _ = peripheral.disconnect().await;
    Ok(())
}
