pub mod ble_gatt;
pub mod usb_serial;

use serde::Serialize;
use serde_json::Value;
use std::sync::mpsc::Sender;

#[derive(Clone, Debug, Serialize)]
pub struct BleDeviceInfo {
    pub id: String,
    pub name: String,
    pub address: String,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum TransportKind {
    None,
    Usb,
    Ble,
}

pub trait BoardTransport: Send {
    fn kind(&self) -> TransportKind;
    fn endpoint(&self) -> String;
    fn write_json(&mut self, payload: &Value) -> Result<(), String>;
    fn disconnect(&mut self);
}

pub type LineSender = Sender<String>;
