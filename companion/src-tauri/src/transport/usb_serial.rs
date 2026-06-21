use super::{BoardTransport, LineSender, TransportKind};
use crate::protocol;
use serde::Serialize;
use serde_json::Value;
use serialport::{SerialPort, SerialPortType};
use std::io::{BufRead, BufReader, Write};
use std::thread;
use std::time::Duration;

pub const BOARD_VID: u16 = 0x303A;
pub const BOARD_PID: u16 = 0x1001;
pub const CH340_VID: u16 = 0x1A86;
pub const CH340_PID: u16 = 0x55D3;

#[derive(Clone, Debug, Serialize)]
pub struct SerialPortInfo {
    pub name: String,
    pub kind: String,
}

pub struct UsbSerialTransport {
    endpoint: String,
    port: Box<dyn SerialPort>,
}

impl UsbSerialTransport {
    pub fn connect(port_name: Option<String>, lines: LineSender) -> Result<Self, String> {
        let selected = port_name
            .filter(|port| !port.is_empty())
            .or_else(discover_board_port)
            .ok_or("ki-board CDC serial port not found")?;

        let mut port = serialport::new(&selected, 115200)
            .timeout(Duration::from_millis(100))
            .open()
            .map_err(|err| format!("open serial port failed: {err}"))?;
        // ESP32-S3 native USB CDC only starts delivering Serial traffic reliably
        // after the host asserts DTR. Keep RTS low so opening the port does not
        // enter a boot/download path.
        let _ = port.write_data_terminal_ready(true);
        let _ = port.write_request_to_send(false);

        let reader = port
            .try_clone()
            .map_err(|err| format!("clone serial port failed: {err}"))?;
        spawn_reader(reader, lines);

        Ok(Self {
            endpoint: selected,
            port,
        })
    }
}

impl BoardTransport for UsbSerialTransport {
    fn kind(&self) -> TransportKind {
        TransportKind::Usb
    }

    fn endpoint(&self) -> String {
        self.endpoint.clone()
    }

    fn write_json(&mut self, payload: &Value) -> Result<(), String> {
        let line = protocol::encode_json_line(payload)?;
        self.port
            .write_all(&line)
            .and_then(|_| self.port.flush())
            .map_err(|err| format!("serial write failed: {err}"))
    }

    fn disconnect(&mut self) {
        let _ = self.port.flush();
    }
}

pub fn discover_board_port() -> Option<String> {
    serialport::available_ports()
        .ok()?
        .into_iter()
        .find_map(|port| {
            if let SerialPortType::UsbPort(info) = port.port_type {
                if info.vid == BOARD_VID && info.pid == BOARD_PID {
                    let product = info.product.unwrap_or_default().to_ascii_lowercase();
                    if product.contains("ki-board") || product.contains("esp32") {
                        return Some(port.port_name);
                    }
                }
            }
            None
        })
}

pub fn discover_flash_port() -> Option<String> {
    serialport::available_ports()
        .ok()?
        .into_iter()
        .find_map(|port| {
            if let SerialPortType::UsbPort(info) = port.port_type {
                if info.vid == CH340_VID && info.pid == CH340_PID {
                    return Some(port.port_name);
                }
            }
            None
        })
}

pub fn list_serial_ports() -> Result<Vec<SerialPortInfo>, String> {
    let ports =
        serialport::available_ports().map_err(|err| format!("list serial ports failed: {err}"))?;
    Ok(ports
        .into_iter()
        .map(|port| {
            let kind = match &port.port_type {
                SerialPortType::UsbPort(info) if info.vid == BOARD_VID && info.pid == BOARD_PID => {
                    "board-cdc"
                }
                SerialPortType::UsbPort(info) if info.vid == CH340_VID && info.pid == CH340_PID => {
                    "flash-ch340"
                }
                SerialPortType::UsbPort(_) => "usb-serial",
                _ => "serial",
            };
            SerialPortInfo {
                name: port.port_name,
                kind: kind.to_string(),
            }
        })
        .collect())
}

fn spawn_reader(reader: Box<dyn SerialPort>, lines: LineSender) {
    thread::spawn(move || {
        let mut buffered = BufReader::new(reader);
        loop {
            let mut line = String::new();
            match buffered.read_line(&mut line) {
                Ok(0) => thread::sleep(Duration::from_millis(50)),
                Ok(_) => {
                    let trimmed = line.trim();
                    if !trimmed.is_empty() && lines.send(trimmed.to_string()).is_err() {
                        break;
                    }
                }
                Err(_) => thread::sleep(Duration::from_millis(50)),
            }
        }
    });
}
