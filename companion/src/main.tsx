import React, { useEffect, useMemo, useState } from "react";
import { createRoot } from "react-dom/client";
import { invoke } from "@tauri-apps/api/core";
import { listen, emit } from "@tauri-apps/api/event";
import { register, unregister } from "@tauri-apps/plugin-global-shortcut";
import { WebviewWindow } from "@tauri-apps/api/webviewWindow";
import "./styles.css";

// ─── Global voice dictation shortcut (module-level, outside React) ──────────
let _voiceRecording = false;

async function _openVoiceOverlay() {
  const existing = await WebviewWindow.getByLabel("voice-overlay");
  if (existing) return;
  await invoke("hide_main_window").catch(() => {});
  new WebviewWindow("voice-overlay", {
    url: "voice-overlay.html",
    title: "",
    width: 500,
    height: 100,
    decorations: false,
    transparent: true,
    alwaysOnTop: true,
    resizable: false,
    center: true,
    skipTaskbar: true,
    focus: false,
  });
  await invoke("hide_main_window").catch(() => {});
}

async function _closeVoiceOverlay() {
  const win = await WebviewWindow.getByLabel("voice-overlay");
  if (win) { try { await win.destroy(); } catch {} }
}

async function _handleVoiceToggle() {
  if (!_voiceRecording) {
    await invoke("start_recording")
      .then(async () => {
        _voiceRecording = true;
        await _openVoiceOverlay();
      })
      .catch(() => { _voiceRecording = false; });
  } else {
    _voiceRecording = false;
    await invoke("stop_recording_and_transcribe").catch(() => {});
    // Overlay will be closed by the "voice-recording-stop" event from backend
    // after paste completes.
  }
}

async function _handleVoiceCancel() {
  if (!_voiceRecording) return;
  _voiceRecording = false;
  await invoke("cancel_recording").catch(() => {});
  _closeVoiceOverlay();
}

register("CommandOrControl+Shift+Space", (e) => {
  if (e.state === "Pressed") _handleVoiceToggle();
}).catch((err) => console.error("shortcut failed:", err));

register("CommandOrControl+Shift+.", (e) => {
  if (e.state === "Pressed") _handleVoiceCancel();
}).catch((err) => console.error("shortcut failed:", err));

// Also open/close overlay when triggered by board button (backend emits events).
listen("voice-recording-start", () => {
  _voiceRecording = true;
  invoke("hide_main_window").catch(() => {});
  _openVoiceOverlay();
});
listen("voice-recording-stop", () => {
  _voiceRecording = false;
  _closeVoiceOverlay();
});

type Settings = {
  connection_mode: string;
  serial_port: string;
  ble_device_id: string;
  flash_port: string;
  doubao_endpoint: string;
  doubao_resource_id: string;
  doubao_language: string;
  paste_after_transcribe: boolean;
  audio_input_device: string;
  voice_engine: string;
};

type Status = {
  device_connected: boolean;
  serial_port: string;
  transport: "none" | "usb" | "ble";
  device_name: string;
  endpoint: string;
  recording: boolean;
  busy: boolean;
  paired: boolean;
  authenticated: boolean;
  pairing_code: string;
  last_transcript: string;
  last_partial: string;
  last_error: string;
};

type SerialPortInfo = {
  name: string;
  kind: string;
};

type BleDeviceInfo = {
  id: string;
  name: string;
  address: string;
};

type KeyBinding = {
  label: string;
  action_type: string;
  key: string;
  modifiers: string[];
};

type VoiceEvent = {
  event: string;
  text: string;
};

const defaultSettings: Settings = {
  connection_mode: "auto",
  serial_port: "",
  ble_device_id: "",
  flash_port: "",
  doubao_endpoint: "wss://openspeech.bytedance.com/api/v3/sauc/bigmodel_async",
  doubao_resource_id: "volc.bigasr.sauc.duration",
  doubao_language: "",
  paste_after_transcribe: true,
  audio_input_device: "",
  voice_engine: "doubao",
};

const defaultStatus: Status = {
  device_connected: false,
  serial_port: "",
  transport: "none",
  device_name: "",
  endpoint: "",
  recording: false,
  busy: false,
  paired: false,
  authenticated: false,
  pairing_code: "",
  last_transcript: "",
  last_partial: "",
  last_error: "",
};

const fallbackKeys: KeyBinding[] = [
  { label: "ESC", action_type: "hotkey", key: "Escape", modifiers: [] },
  { label: "Voice", action_type: "voice", key: "Voice", modifiers: [] },
  { label: "Next", action_type: "agent_next", key: "RightBracket", modifiers: ["gui"] },
];

const keyOptions = [
  "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
  "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
  "Tab", "Enter", "Escape", "Backspace", "Space", "Delete", "Left", "Right",
  "Up", "Down", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8",
];

const modifierOptions = ["ctrl", "shift", "alt", "gui"];

function App() {
  const [settings, setSettings] = useState<Settings>(defaultSettings);
  const [status, setStatus] = useState<Status>(defaultStatus);
  const [ports, setPorts] = useState<SerialPortInfo[]>([]);
  const [bleDevices, setBleDevices] = useState<BleDeviceInfo[]>([]);
  const [audioDevices, setAudioDevices] = useState<string[]>([]);
  const [apiKey, setApiKey] = useState("");
  const [events, setEvents] = useState<string[]>([]);
  const [keys, setKeys] = useState<KeyBinding[]>(fallbackKeys);
  const [firmwarePath, setFirmwarePath] = useState("");
  const [flashOutput, setFlashOutput] = useState("");
  const [activeView, setActiveView] = useState("device");
  const [uiError, setUiError] = useState("");
  const [hasApiKey, setHasApiKey] = useState(false);
  const [pairCode, setPairCode] = useState<string | null>(null);
  const [pairMessage, setPairMessage] = useState("");
  const [voiceText, setVoiceText] = useState("");

  const boardPorts = useMemo(() => ports.filter((port) => port.kind !== "flash-ch340"), [ports]);
  const flashPorts = useMemo(() => ports.filter((port) => port.kind === "flash-ch340" || port.kind === "usb-serial"), [ports]);

  async function loadSettings() {
    setSettings(await invoke<Settings>("get_settings"));
  }

  async function refreshRuntime() {
    setStatus(await invoke<Status>("get_status"));
    setPorts(await invoke<SerialPortInfo[]>("list_serial_ports"));
    setHasApiKey(await invoke<boolean>("has_doubao_api_key"));
    setEvents(await invoke<string[]>("get_logs"));
  }

  async function refresh() {
    await loadSettings();
    await refreshRuntime();
    await loadAudioDevices();
  }

  async function loadAudioDevices() {
    try {
      setAudioDevices(await invoke<string[]>("list_audio_input_devices"));
    } catch {
      setAudioDevices([]);
    }
  }

  // Persist + sync the voice engine immediately (mirrors selectMode) so the
  // board learns whether to emit the system-dictation HID on the middle key.
  async function selectVoiceEngine(engine: string) {
    const next = { ...settings, voice_engine: engine };
    setSettings(next);
    await invoke("save_settings", { settings: next });
  }

  async function scanBleDevices() {
    setUiError("");
    setBleDevices(await invoke<BleDeviceInfo[]>("list_ble_devices"));
  }

  async function clearLogs() {
    setUiError("");
    await invoke("clear_logs");
    setEvents([]);
  }

  async function testLogEvent() {
    setUiError("");
    await invoke("test_log_event");
  }

  useEffect(() => {
    refresh();
    const timer = window.setInterval(refreshRuntime, 1500);
    const unlistenLogs = listen<string>("companion-log", (event) => {
      setEvents((items) => [event.payload, ...items].slice(0, 120));
    });
    const unlistenVoice = listen<VoiceEvent>("voice-event", (event) => {
      setStatus((current) => ({
        ...current,
        last_partial: event.payload.event === "partial" ? event.payload.text : "",
        last_transcript: event.payload.event === "final" ? event.payload.text : current.last_transcript,
        last_error: event.payload.event === "error" ? event.payload.text : current.last_error,
      }));
      if (event.payload.event === "partial" || event.payload.event === "final") {
        setVoiceText(event.payload.text);
      }
      if (event.payload.event === "error") {
        setUiError(event.payload.text);
        setVoiceText(event.payload.text);
      }
    });
    const unlistenPair = listen<{ event: string; code?: string; reason?: string; name?: string }>(
      "pair-event",
      (event) => {
        const payload = event.payload;
        switch (payload.event) {
          case "code":
            setPairCode(payload.code ?? "");
            setPairMessage("");
            break;
          case "ok":
            setPairCode(null);
            setPairMessage("Paired successfully.");
            refreshRuntime();
            break;
          case "failed":
            setPairCode(null);
            setPairMessage(`Pairing failed: ${payload.reason ?? "unknown"}`);
            break;
          case "auth_ok":
            setPairMessage("Authenticated.");
            refreshRuntime();
            break;
          case "auth_required":
            setPairMessage("Board requires pairing. Put the board in pairing mode and click Pair.");
            refreshRuntime();
            break;
          case "unpaired":
            setPairCode(null);
            setPairMessage("Device forgotten.");
            refreshRuntime();
            break;
        }
      },
    );
    return () => {
      window.clearInterval(timer);
      unlistenLogs.then((unlisten) => unlisten());
      unlistenVoice.then((unlisten) => unlisten());
      unlistenPair.then((unlisten) => unlisten());
    };
  }, []);



  async function saveSettings() {
    setUiError("");
    await invoke("save_settings", { settings });
    if (apiKey.trim()) {
      await invoke("save_doubao_api_key", { apiKey });
      setApiKey("");
      setHasApiKey(true);
    }
    await refresh();
  }

  async function connect() {
    setUiError("");
    await invoke("save_settings", { settings });
    await invoke("connect_device", { portName: settings.serial_port || null });
    await refreshRuntime();
  }

  async function disconnect() {
    setUiError("");
    await invoke("disconnect_device");
    await refreshRuntime();
  }

  // Persist the connection mode immediately so a later Refresh (which reloads
  // settings from disk) doesn't revert the chip selection.
  // Also tells the board which HID output to use (USB or BLE).
  async function selectMode(mode: string) {
    const next = { ...settings, connection_mode: mode };
    setSettings(next);
    await invoke("save_settings", { settings: next });
    // Best-effort: if already connected, sync the HID output to the board.
    if (status.device_connected) {
      await invoke("set_hid_output", { mode: mode === "ble" ? "ble" : "usb" }).catch(() => {});
    }
  }

  async function startPairing() {
    setUiError("");
    setPairMessage("Requesting pairing… confirm on the board.");
    await invoke("start_pairing");
  }

  // Pairing works over either transport (USB or BLE). Connect first using the
  // current connection mode if needed, then send the pair request.
  async function pairFlow() {
    setUiError("");
    if (!status.device_connected) {
      await invoke("save_settings", { settings });
      setPairMessage("Connecting…");
      await invoke("connect_device", { portName: settings.serial_port || null });
      await refreshRuntime();
    }
    await startPairing();
  }

  async function forgetDevice() {
    setUiError("");
    await invoke("forget_device");
    await refresh();
  }

  async function loadKeymap() {
    setUiError("");
    setKeys(await invoke<KeyBinding[]>("get_keymap"));
  }

  async function saveKeymap() {
    setUiError("");
    await invoke("set_keymap", { keys });
    await loadKeymap();
  }

  async function startRecording() {
    setUiError("");
    setVoiceText("");
    await saveSettings();
    const saved = await invoke<boolean>("has_doubao_api_key");
    setHasApiKey(saved);
    if (!saved) {
      throw new Error("Paste your Doubao X-Api-Key, then click Save Voice or Start again.");
    }
    await invoke("start_recording");
    await refresh();
  }

  async function stopRecording() {
    setUiError("");
    await invoke("stop_recording_and_transcribe");
    await refresh();
  }

  async function cancelRecording() {
    setUiError("");
    await invoke("cancel_recording");
    await refresh();
  }

  async function flashFirmware() {
    setUiError("");
    setFlashOutput("");
    const output = await invoke<string>("flash_firmware", {
      firmwarePath,
      portName: settings.flash_port || null,
    });
    setFlashOutput(output);
  }

  async function runAction(action: () => Promise<void>) {
    try {
      await action();
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      setUiError(message);
      setEvents((items) => [`error: ${message}`, ...items].slice(0, 120));
      await refresh().catch(() => undefined);
    }
  }

  function updateKey(index: number, patch: Partial<KeyBinding>) {
    setKeys((items) => items.map((item, itemIndex) => itemIndex === index ? { ...item, ...patch } : item));
  }

  function toggleModifier(index: number, modifier: string) {
    const key = keys[index];
    const modifiers = key.modifiers.includes(modifier)
      ? key.modifiers.filter((item) => item !== modifier)
      : [...key.modifiers, modifier];
    updateKey(index, { modifiers });
  }

  return (
    <main className="shell">
      <aside className="sidebar">
        <div className="brand">
          <div className="ghost">KI</div>
          <div>
            <h1>Kiro Deck</h1>
            <p>Pixel companion</p>
          </div>
        </div>
        {["device", "keys", "flash", "voice", "logs"].map((view) => (
          <button
            key={view}
            className={activeView === view ? "nav active" : "nav"}
            onClick={() => setActiveView(view)}
          >
            {view.toUpperCase()}
          </button>
        ))}
      </aside>

      <section className="content">
        <header className="topbar">
          <div>
            <div className="eyebrow">USB CDC · BLE GATT · Doubao ASR · ESP32-S3</div>
            <h2>{viewTitle(activeView)}</h2>
          </div>
          <div className={status.device_connected ? "status online" : "status"}>
            {status.device_connected ? "BOARD ONLINE" : "BOARD OFFLINE"}
          </div>
        </header>

        <section className="lcd-grid">
          <div className="lcd">
            <span>LINK</span>
            <strong>{status.device_connected ? `${status.transport.toUpperCase()} ${status.endpoint || status.serial_port}` : settings.connection_mode.toUpperCase()}</strong>
          </div>
          <div className="lcd">
            <span>VOICE</span>
            <strong>{status.busy ? "BUSY" : status.recording ? "REC" : "IDLE"}</strong>
          </div>
          <div className="lcd wide">
            <span>TRANSCRIPT</span>
            <strong>{status.last_partial || status.last_transcript || "--"}</strong>
          </div>
        </section>

        {(uiError || status.last_error) && <div className="error-banner">{uiError || status.last_error}</div>}

        {activeView === "device" && (
          <Panel title="Device Bridge">
            <label>Connection mode</label>
            <div className="chips">
              <button
                className={settings.connection_mode !== "ble" ? "chip active" : "chip"}
                onClick={() => runAction(() => selectMode("usb"))}
              >
                USB
              </button>
              <button
                className={settings.connection_mode === "ble" ? "chip active" : "chip"}
                onClick={() => runAction(() => selectMode("ble"))}
              >
                Bluetooth
              </button>
            </div>

            {settings.connection_mode !== "ble" ? (
              <>
                <label>Board USB port</label>
                <select
                  value={settings.serial_port}
                  onChange={(event) => setSettings({ ...settings, serial_port: event.target.value })}
                >
                  <option value="">Auto-detect ki-board</option>
                  {boardPorts.map((port) => (
                    <option key={port.name} value={port.name}>{port.name} · {port.kind}</option>
                  ))}
                </select>
                <p className="hint good-text">USB is plug-and-play — no pairing needed.</p>
                <div className="actions">
                  <button onClick={() => runAction(connect)}>Connect</button>
                  <button className="secondary" onClick={() => runAction(disconnect)}>Disconnect</button>
                  <button className="secondary" onClick={() => runAction(refresh)}>Refresh</button>
                </div>
              </>
            ) : (
              <>
                <p className="hint">
                  {status.paired
                    ? "Connects to your paired Kiro board automatically and authenticates with the stored key."
                    : "Not paired yet — use Pair below first. Connect unlocks once this Mac is authorized."}
                </p>
                <div className="actions">
                  <button onClick={() => runAction(connect)} disabled={!status.paired}>Connect</button>
                  <button className="secondary" onClick={() => runAction(disconnect)}>Disconnect</button>
                  <button className="secondary" onClick={() => runAction(refresh)}>Refresh</button>
                </div>

                <div style={{ height: 1, background: "#2a2f3a", margin: "18px 0" }} />

                <label>Pairing</label>
                <p className={status.paired ? "hint good-text" : "hint warn-text"}>
                  {status.paired
                    ? "BLE AUTHORIZED for this Mac"
                    : "BLE NOT AUTHORIZED — pair to allow wireless access."}
                </p>
                <p className="hint">
                  On the board, hold the <strong>left + right</strong> keys together for ~3s until a 6-digit
                  code appears. Then click Pair, compare the code, and confirm on the board (● middle key).
                </p>
                <div className="actions">
                  <button onClick={() => runAction(pairFlow)}>Pair</button>
                  <button className="secondary" onClick={() => runAction(forgetDevice)} disabled={!status.paired}>Forget device</button>
                </div>
                {status.pairing_code && (
                  <div style={{ marginTop: 12, padding: "12px 14px", background: "#10131a", border: "1px solid #07c2d6", borderRadius: 8 }}>
                    <div style={{ color: "#aab", fontSize: 12 }}>Compare with the board, then press ● on the board:</div>
                    <div style={{ fontFamily: "monospace", fontSize: 32, letterSpacing: 6, color: "#fff", marginTop: 4 }}>{status.pairing_code}</div>
                  </div>
                )}
                {pairMessage && <p className="hint">{pairMessage}</p>}
              </>
            )}
          </Panel>
        )}

        {activeView === "keys" && (
          <Panel title="Key Tiles">
            <div className="key-grid">
              {keys.map((binding, index) => (
                <div className="key-card" key={index}>
                  <div className="key-screen">{binding.label || `KEY ${index + 1}`}</div>
                  <label>Label</label>
                  <input value={binding.label} onChange={(event) => updateKey(index, { label: event.target.value })} />
                  <label>Action</label>
                  <select value={binding.action_type} onChange={(event) => updateKey(index, { action_type: event.target.value })}>
                    <option value="hotkey">Hotkey</option>
                    <option value="voice">Voice</option>
                    <option value="agent_next">Next Agent</option>
                    <option value="none">None</option>
                  </select>
                  <label>Key</label>
                  <select value={binding.key} onChange={(event) => updateKey(index, { key: event.target.value })}>
                    <option value="">None</option>
                    {keyOptions.map((key) => <option key={key} value={key}>{key}</option>)}
                  </select>
                  <div className="chips">
                    {modifierOptions.map((modifier) => (
                      <button
                        key={modifier}
                        className={binding.modifiers.includes(modifier) ? "chip active" : "chip"}
                        onClick={() => toggleModifier(index, modifier)}
                      >
                        {modifier}
                      </button>
                    ))}
                  </div>
                </div>
              ))}
            </div>
            <div className="actions">
              <button onClick={() => runAction(loadKeymap)}>Read Board</button>
              <button onClick={() => runAction(saveKeymap)}>Write Board</button>
            </div>
          </Panel>
        )}

        {activeView === "flash" && (
          <Panel title="Firmware Flash">
            <label>CH340 flash port</label>
            <select
              value={settings.flash_port}
              onChange={(event) => setSettings({ ...settings, flash_port: event.target.value })}
            >
              <option value="">Auto-detect CH340</option>
              {flashPorts.map((port) => (
                <option key={port.name} value={port.name}>{port.name} · {port.kind}</option>
              ))}
            </select>
            <label>Firmware .bin path</label>
            <input
              value={firmwarePath}
              onChange={(event) => setFirmwarePath(event.target.value)}
              placeholder="/absolute/path/to/firmware.bin"
            />
            <div className="actions">
              <button onClick={() => runAction(flashFirmware)} disabled={!firmwarePath.trim()}>Flash</button>
              <button className="secondary" onClick={() => runAction(saveSettings)}>Save Port</button>
            </div>
            {flashOutput && <pre className="terminal">{flashOutput}</pre>}
          </Panel>
        )}

        {activeView === "voice" && (
          <Panel title="Voice">
            <label>Voice engine</label>
            <div className="chips">
              <button
                className={settings.voice_engine !== "doubao" ? "chip active" : "chip"}
                onClick={() => runAction(() => selectVoiceEngine("system"))}
              >
                System (HID)
              </button>
              <button
                className={settings.voice_engine === "doubao" ? "chip active" : "chip"}
                onClick={() => runAction(() => selectVoiceEngine("doubao"))}
              >
                Doubao ASR
              </button>
            </div>
            <p className="hint">
              {settings.voice_engine === "doubao"
                ? "Companion records the selected mic and streams to Doubao ASR, then pastes the transcript."
                : "The board triggers macOS dictation via the Control double-tap (HID). The companion does not record in this mode."}
            </p>

            {settings.voice_engine === "doubao" ? (
              <>
                <label>Audio input device</label>
                <select
                  value={settings.audio_input_device}
                  onChange={(event) => setSettings({ ...settings, audio_input_device: event.target.value })}
                >
                  <option value="">System default</option>
                  {audioDevices.map((device) => (
                    <option key={device} value={device}>{device}</option>
                  ))}
                </select>
                <div className="actions">
                  <button className="secondary" onClick={() => runAction(loadAudioDevices)}>Refresh devices</button>
                </div>

                <label>Endpoint</label>
                <input value={settings.doubao_endpoint} onChange={(event) => setSettings({ ...settings, doubao_endpoint: event.target.value })} />
                <label>Resource ID</label>
                <input value={settings.doubao_resource_id} onChange={(event) => setSettings({ ...settings, doubao_resource_id: event.target.value })} />
                <label>Language override</label>
                <input
                  value={settings.doubao_language}
                  onChange={(event) => setSettings({ ...settings, doubao_language: event.target.value })}
                  placeholder="Optional, for supported endpoints only"
                />
                <p className="hint">
                  Leave language empty by default. Volcengine documents this field as endpoint/mode dependent.
                </p>
                <label>X-Api-Key</label>
                <input value={apiKey} onChange={(event) => setApiKey(event.target.value)} type="password" placeholder="Stored in OS keyring" />
                <p className={hasApiKey ? "hint good-text" : "hint warn-text"}>
                  {hasApiKey ? "KEY SAVED in secure storage." : "KEY MISSING: paste X-Api-Key here before testing."}
                </p>
                <label className="check">
                  <input
                    type="checkbox"
                    checked={settings.paste_after_transcribe}
                    onChange={(event) => setSettings({ ...settings, paste_after_transcribe: event.target.checked })}
                  />
                  Paste final transcript
                </label>
                <div className="actions">
                  <button onClick={() => runAction(saveSettings)}>Save Voice</button>
                </div>

                <div className="test-box">
                  <div className="test-head">
                    <span>TEST INPUT</span>
                    <strong>{status.busy ? "TRANSCRIBING" : status.recording ? "LISTENING" : "READY"}</strong>
                  </div>
                  <div className="transcript-preview">
                    {voiceText || "Click Start and speak to test Doubao transcription."}
                  </div>
                  <div className="actions">
                    <button disabled={status.recording || status.busy} onClick={() => runAction(startRecording)}>Start</button>
                    <button disabled={!status.recording && !status.busy} onClick={() => runAction(stopRecording)}>Stop</button>
                    <button className="secondary" disabled={!status.recording} onClick={() => runAction(cancelRecording)}>Cancel</button>
                  </div>
                </div>
              </>
            ) : (
              <div className="actions">
                <button onClick={() => runAction(saveSettings)}>Save Voice</button>
              </div>
            )}
          </Panel>
        )}

        {activeView === "logs" && (
          <Panel title="Event Log">
            <div className="actions">
              <button className="secondary" onClick={() => runAction(clearLogs)}>Clear</button>
            </div>
            <div className="log">
              {events.map((event, index) => <div key={`${event}-${index}`}>{event}</div>)}
            </div>
          </Panel>
        )}
      </section>

      {pairCode !== null && (
        <div
          style={{
            position: "fixed",
            inset: 0,
            background: "rgba(0,0,0,0.7)",
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            zIndex: 1000,
          }}
        >
          <div
            style={{
              background: "#15171c",
              border: "1px solid #2a2f3a",
              borderRadius: 12,
              padding: "28px 32px",
              maxWidth: 420,
              textAlign: "center",
            }}
          >
            <h3 style={{ marginTop: 0, color: "#07c2d6" }}>Confirm pairing</h3>
            <p style={{ color: "#aab" }}>Does this code match the one on the board screen?</p>
            <div
              style={{
                fontSize: 44,
                letterSpacing: 8,
                fontWeight: 700,
                color: "#fff",
                margin: "12px 0 18px",
                fontFamily: "monospace",
              }}
            >
              {pairCode}
            </div>
            <p style={{ color: "#aab", fontSize: 13 }}>
              If they match, press the <strong>middle (●)</strong> key on the board to confirm,
              or the <strong>left (◀)</strong> key to cancel.
            </p>
            <div className="actions" style={{ justifyContent: "center" }}>
              <button className="secondary" onClick={() => setPairCode(null)}>Close</button>
            </div>
          </div>
        </div>
      )}
    </main>
  );
}

function Panel({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <section className="panel">
      <h3>{title}</h3>
      {children}
    </section>
  );
}

function viewTitle(view: string) {
  switch (view) {
    case "keys": return "Key Configuration";
    case "flash": return "Firmware Flasher";
    case "voice": return "Voice Console";
    case "logs": return "Event Stream";
    default: return "Device Control";
  }
}

createRoot(document.getElementById("root")!).render(<App />);
