use serde_json::Value;

pub const PROTOCOL_VERSION: u8 = 1;

pub fn encode_json_line(payload: &Value) -> Result<Vec<u8>, String> {
    let mut line = serde_json::to_vec(payload).map_err(|err| err.to_string())?;
    line.push(b'\n');
    Ok(line)
}

pub fn hello_payload() -> Value {
    serde_json::json!({
        "type": "hello",
        "protocol": PROTOCOL_VERSION,
        "client": "companion",
        "version": "0.2.0"
    })
}

pub fn legacy_hello_payload() -> Value {
    serde_json::json!({"type":"companion_hello","version":"0.2.0"})
}

pub fn ping_payload() -> Value {
    serde_json::json!({"type":"ping","protocol": PROTOCOL_VERSION})
}

pub fn pair_request_payload(client: &str) -> Value {
    serde_json::json!({"type":"pair_request","client": client})
}

pub fn auth_payload(token: &str) -> Value {
    serde_json::json!({"type":"auth","token": token})
}

pub fn unpair_payload() -> Value {
    serde_json::json!({"type":"unpair"})
}

pub fn legacy_ping_payload() -> Value {
    serde_json::json!({"type":"companion_ping"})
}
