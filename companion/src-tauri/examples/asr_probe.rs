// Standalone probe for the Doubao (Volcengine) big-model streaming ASR.
//
// It replays a WAV file through the EXACT framing functions the companion uses
// (build_full_request_frame / build_audio_frame / parse_server_frame), so this
// validates the real code path, not a copy. Every server frame is logged with
// its decoded header so we can see precisely what the service returns.
//
// Usage:
//   DOUBAO_API_KEY=<key> cargo run --example asr_probe -- /tmp/asr_test.wav
//
// Env knobs:
//   DOUBAO_ENDPOINT     default wss://openspeech.bytedance.com/api/v3/sauc/bigmodel_async
//   DOUBAO_RESOURCE_ID  default volc.bigasr.sauc.duration
//   PROBE_CHUNK_MS      default 200 = audio packet size in milliseconds

use std::time::Duration;

use futures_util::{SinkExt, StreamExt};
use kiro_keyboard_companion_lib::{
    build_audio_frame, build_full_request_frame, parse_server_frame, pcm16_to_le_bytes,
};
use tokio_tungstenite::connect_async;
use tokio_tungstenite::tungstenite::client::IntoClientRequest;
use tokio_tungstenite::tungstenite::Message;
use uuid::Uuid;

fn env_or(key: &str, default: &str) -> String {
    std::env::var(key).unwrap_or_else(|_| default.to_string())
}

fn hex_prefix(bytes: &[u8], n: usize) -> String {
    bytes
        .iter()
        .take(n)
        .map(|b| format!("{b:02x}"))
        .collect::<Vec<_>>()
        .join(" ")
}

/// Log a server frame header and the transcript extracted by the real parser.
fn log_frame(bytes: &[u8]) {
    let header = hex_prefix(bytes, 16);
    let (mtype, flags, serial, compress) = if bytes.len() >= 4 {
        (bytes[1] >> 4, bytes[1] & 0x0f, bytes[2] >> 4, bytes[2] & 0x0f)
    } else {
        (0, 0, 0, 0)
    };
    println!(
        "[recv] {} bytes | mtype=0x{mtype:x} flags=0x{flags:x} serial=0x{serial:x} compress=0x{compress:x} | {header}",
        bytes.len()
    );
    match parse_server_frame(bytes) {
        Ok(Some(text)) => println!("       transcript: {text}"),
        Ok(None) => println!("       (ack / no text)"),
        Err(err) => println!("       *** {err}"),
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let _ = rustls::crypto::ring::default_provider().install_default();

    let wav_path = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "/tmp/asr_test.wav".to_string());
    let api_key = std::env::var("DOUBAO_API_KEY")
        .expect("set DOUBAO_API_KEY env var (test key, not committed)");
    let endpoint = env_or(
        "DOUBAO_ENDPOINT",
        "wss://openspeech.bytedance.com/api/v3/sauc/bigmodel_async",
    );
    let resource_id = env_or("DOUBAO_RESOURCE_ID", "volc.bigasr.sauc.duration");
    let chunk_ms: usize = env_or("PROBE_CHUNK_MS", "200").parse().unwrap_or(200);

    println!("=== Doubao ASR probe (real lib framing) ===");
    println!("endpoint     = {endpoint}");
    println!("resource_id  = {resource_id}");
    println!("chunk_ms     = {chunk_ms}");
    println!("wav          = {wav_path}");

    let mut reader = hound::WavReader::open(&wav_path)?;
    let spec = reader.spec();
    println!(
        "wav spec: {} Hz, {} ch, {} bits",
        spec.sample_rate, spec.channels, spec.bits_per_sample
    );
    let samples: Vec<i16> = reader.samples::<i16>().filter_map(|s| s.ok()).collect();
    println!(
        "samples: {} ({:.2}s)",
        samples.len(),
        samples.len() as f32 / spec.sample_rate as f32
    );

    let request_id = Uuid::new_v4().to_string();
    let mut request = endpoint.as_str().into_client_request()?;
    request.headers_mut().insert("X-Api-Key", api_key.parse()?);
    request
        .headers_mut()
        .insert("X-Api-Resource-Id", resource_id.parse()?);
    request
        .headers_mut()
        .insert("X-Api-Request-Id", request_id.parse()?);
    request.headers_mut().insert("X-Api-Sequence", "-1".parse()?);

    println!("\n[connect] dialing...");
    let (ws, response) = connect_async(request).await?;
    println!("[connect] connected. HTTP status: {}", response.status());
    if let Some(logid) = response.headers().get("x-tt-logid") {
        println!("[connect] x-tt-logid: {}", logid.to_str().unwrap_or("?"));
    }
    let (mut sink, mut stream) = ws.split();

    // full client request — same JSON the companion sends.
    let full_request = serde_json::json!({
        "user": {"uid": "asr-probe", "platform": std::env::consts::OS, "app_version": "0.2.0"},
        "audio": {"format": "pcm", "codec": "raw", "rate": 16000, "bits": 16, "channel": 1},
        "request": {"model_name": "bigmodel", "enable_itn": true, "show_utterances": true, "result_type": "full"}
    });
    let json = serde_json::to_vec(&full_request)?;
    let frame = build_full_request_frame(1, &json);
    println!(
        "\n[send] full client request: {} bytes | {}",
        frame.len(),
        hex_prefix(&frame, 12)
    );
    sink.send(Message::Binary(frame.into())).await?;

    if let Ok(Some(Ok(Message::Binary(bytes)))) =
        tokio::time::timeout(Duration::from_secs(5), stream.next()).await
    {
        println!("[after full request]");
        log_frame(&bytes);
    } else {
        println!("[after full request] (no response within 5s)");
    }

    // Stream audio in chunk_ms packets, starting at sequence 2.
    let chunk_samples = 16000 * chunk_ms / 1000;
    let mut seq = 2i32;
    let chunks: Vec<&[i16]> = samples.chunks(chunk_samples).collect();
    let total = chunks.len();
    let mut last_transcript = String::new();
    for (index, chunk) in chunks.iter().enumerate() {
        let last = index + 1 == total;
        let pcm = pcm16_to_le_bytes(chunk);
        let frame_seq = if last { -seq } else { seq };
        let frame = build_audio_frame(frame_seq, last, &pcm);
        println!(
            "[send] audio #{}/{} seq={} last={} pcm={} bytes",
            index + 1,
            total,
            frame_seq,
            last,
            pcm.len()
        );
        sink.send(Message::Binary(frame.into())).await?;
        seq += 1;

        while let Ok(Some(Ok(msg))) =
            tokio::time::timeout(Duration::from_millis(chunk_ms as u64), stream.next()).await
        {
            if let Message::Binary(bytes) = msg {
                if let Ok(Some(text)) = parse_server_frame(&bytes) {
                    last_transcript = text;
                }
                log_frame(&bytes);
            }
        }
    }

    println!("\n[drain] waiting for final responses...");
    loop {
        match tokio::time::timeout(Duration::from_secs(5), stream.next()).await {
            Ok(Some(Ok(Message::Binary(bytes)))) => {
                if let Ok(Some(text)) = parse_server_frame(&bytes) {
                    last_transcript = text;
                }
                log_frame(&bytes);
            }
            Ok(Some(Ok(Message::Close(frame)))) => {
                println!("[drain] server closed: {frame:?}");
                break;
            }
            Ok(Some(Ok(_))) => {}
            Ok(Some(Err(err))) => {
                println!("[drain] stream error: {err}");
                break;
            }
            Ok(None) => {
                println!("[drain] stream ended");
                break;
            }
            Err(_) => {
                println!("[drain] timed out waiting for more frames");
                break;
            }
        }
    }

    let _ = sink.close().await;
    println!("\n=== FINAL TRANSCRIPT: {last_transcript} ===");
    Ok(())
}
