import React, { useEffect, useMemo, useState } from "react";
import { createRoot } from "react-dom/client";
import { invoke } from "@tauri-apps/api/core";
import { listen, emit } from "@tauri-apps/api/event";
import { register, unregister } from "@tauri-apps/plugin-global-shortcut";
import { open } from "@tauri-apps/plugin-dialog";
import { WebviewWindow } from "@tauri-apps/api/webviewWindow";
import { useTranslation } from "react-i18next";
import i18n from "./i18n";
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
  serial_port: string;
  ble_device_id: string;
  paired_board_id: string;
  doubao_endpoint: string;
  doubao_resource_id: string;
  doubao_language: string;
  paste_after_transcribe: boolean;
  audio_input_device: string;
  voice_engine: string;
  asr_provider: string;
};

type Status = {
  device_connected: boolean;
  serial_port: string;
  transport: "none" | "usb" | "ble";
  device_name: string;
  endpoint: string;
  firmware_version: string;
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
  serial_port: "",
  ble_device_id: "",
  paired_board_id: "",
  doubao_endpoint: "wss://openspeech.bytedance.com/api/v3/sauc/bigmodel_async",
  doubao_resource_id: "volc.bigasr.sauc.duration",
  doubao_language: "",
  paste_after_transcribe: true,
  audio_input_device: "",
  voice_engine: "third_party",
  asr_provider: "doubao",
};

function isThirdPartyVoice(settings: Settings) {
  return settings.voice_engine === "third_party" || settings.voice_engine === "doubao";
}

function normalizeVoiceSettings(settings: Settings): Settings {
  if (settings.voice_engine === "doubao") {
    return {
      ...settings,
      voice_engine: "third_party",
      asr_provider: settings.asr_provider || "doubao",
    };
  }
  return {
    ...settings,
    asr_provider: settings.asr_provider || "doubao",
  };
}

const defaultStatus: Status = {
  device_connected: false,
  serial_port: "",
  transport: "none",
  device_name: "",
  endpoint: "",
  firmware_version: "",
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
  const { t, i18n } = useTranslation();
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
  const [otaInProgress, setOtaInProgress] = useState(false);
  const [activeView, setActiveView] = useState("device");
  const [uiError, setUiError] = useState("");
  const [hasApiKey, setHasApiKey] = useState(false);
  const [pairCode, setPairCode] = useState<string | null>(null);
  const [pairMessage, setPairMessage] = useState("");
  const [voiceText, setVoiceText] = useState("");

  const boardPorts = useMemo(() => ports.filter((port) => port.kind !== "flash-ch340"), [ports]);

  async function loadSettings() {
    setSettings(normalizeVoiceSettings(await invoke<Settings>("get_settings")));
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

  // Persist + sync the voice engine immediately so the
  // board learns whether to emit the system-dictation HID on the middle key.
  async function selectVoiceEngine(engine: string) {
    const next = normalizeVoiceSettings({ ...settings, voice_engine: engine });
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
            setPairMessage(i18n.t("device.pairSuccess"));
            refreshRuntime();
            break;
          case "failed":
            setPairCode(null);
            setPairMessage(i18n.t("device.pairFailed", { reason: payload.reason ?? "unknown" }));
            break;
          case "auth_ok":
            setPairMessage(i18n.t("device.pairAuthOk"));
            refreshRuntime();
            break;
          case "auth_required":
            setPairMessage(i18n.t("device.pairAuthRequired"));
            refreshRuntime();
            break;
          case "unpaired":
            setPairCode(null);
            setPairMessage(i18n.t("device.pairUnpaired"));
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
    const next = normalizeVoiceSettings(settings);
    setSettings(next);
    await invoke("save_settings", { settings: next });
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

  async function startPairing() {
    setUiError("");
    setPairMessage(i18n.t("device.pairRequesting"));
    await invoke("start_pairing");
  }

  // Pairing works over either transport (USB or BLE). Connect first if needed,
  // then send the pair request.
  async function pairFlow() {
    setUiError("");
    if (!status.device_connected) {
      await invoke("save_settings", { settings });
      setPairMessage(i18n.t("device.pairConnecting"));
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
      throw new Error(i18n.t("voice.keyMissing"));
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



  async function otaFlashFirmware() {
    setUiError("");
    setFlashOutput("");
    setOtaInProgress(true);
    try {
      const output = await invoke<string>("ota_flash_firmware", {
        firmwarePath,
        transportMode: "current",
      });
      setFlashOutput(output);
    } finally {
      setOtaInProgress(false);
      await refreshRuntime();
    }
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
          <img src="/kiro-logo.png" alt="Ki-board" className="brand-logo" />
          <div>
            <h1>{t("brand.name")}</h1>
            <p>{t("brand.slogan")}</p>
          </div>
        </div>
        {(["device", "voice", "flash", "logs"] as const).map((view) => (
          <button
            key={view}
            className={activeView === view ? "nav active" : "nav"}
            onClick={() => setActiveView(view)}
          >
            {t(`sidebar.${view}`)}
          </button>
        ))}
        <div className="lang-switcher">
          <button
            className={i18n.language === "en" ? "nav active" : "nav"}
            onClick={() => i18n.changeLanguage("en")}
          >
            {t("language.en")}
          </button>
          <button
            className={i18n.language === "zh-CN" ? "nav active" : "nav"}
            onClick={() => i18n.changeLanguage("zh-CN")}
          >
            {t("language.zhCN")}
          </button>
        </div>
        <div className="sidebar-footer">
          <p>{t("sidebar.footer")}</p>
          <p>{t("sidebar.version")}</p>
        </div>
      </aside>

      <section className="content">
        <header className="topbar">
          <div>
            <div className="eyebrow">{t("topbar.eyebrow")}</div>
            <h2>{t(`topbar.viewTitle.${activeView}`, { defaultValue: t("topbar.viewTitle.device") })}</h2>
          </div>
          <div className={status.device_connected ? "status online" : "status"}>
            {status.device_connected ? t("status.online") : t("status.offline")}
          </div>
        </header>

        <section className="lcd-grid">
          <div className="lcd">
            <span>{t("lcd.link")}</span>
            <strong>{status.device_connected ? status.transport.toUpperCase() : t("lcd.disconnected")}</strong>
          </div>
          <div className="lcd">
            <span>{t("lcd.firmware")}</span>
            <strong>{status.firmware_version || "--"}</strong>
          </div>
          <div className="lcd">
            <span>{t("lcd.voiceStatus")}</span>
            <strong>{status.busy ? t("lcd.busy") : status.recording ? t("lcd.rec") : t("lcd.idle")}</strong>
          </div>
          <div className="lcd">
            <span>{t("lcd.voiceMode")}</span>
            <strong>{settings.voice_engine === "system" ? t("lcd.system") : t("lcd.asr")}</strong>
          </div>
        </section>

        {(uiError || status.last_error) && <div className="error-banner">{uiError || status.last_error}</div>}

        {activeView === "device" && (
          <Panel title={t("device.title")}>
            <label>{t("device.portLabel")}</label>
            <select
              value={settings.serial_port}
              onChange={(event) => setSettings({ ...settings, serial_port: event.target.value })}
            >
              <option value="">{t("device.autoDetect")}</option>
              {boardPorts.map((port) => (
                <option key={port.name} value={port.name}>{port.name} · {port.kind}</option>
              ))}
            </select>
            <p className="hint good-text">{t("device.hint")}</p>
            <div className="actions">
              <button onClick={() => runAction(connect)} disabled={otaInProgress}>{t("device.connect")}</button>
              <button className="secondary" onClick={() => runAction(disconnect)} disabled={otaInProgress}>{t("device.disconnect")}</button>
              <button className="secondary" onClick={() => runAction(refresh)}>{t("device.refresh")}</button>
            </div>

            <div style={{ height: 1, background: "#2a2f3a", margin: "18px 0" }} />

            <label>{t("device.blePairing")}</label>
            <p className={status.paired ? "hint good-text" : "hint warn-text"}>
              {status.paired
                ? t("device.bleAuthorized")
                : t("device.bleNotAuthorized")}
            </p>
            <p className="hint" dangerouslySetInnerHTML={{ __html: t("device.pairHint") }} />
            <div className="actions">
              <button onClick={() => runAction(pairFlow)}>{t("device.pair")}</button>
              <button className="secondary" onClick={() => runAction(forgetDevice)} disabled={!status.paired}>{t("device.forgetDevice")}</button>
            </div>
            {status.pairing_code && (
              <div style={{ marginTop: 12, padding: "12px 14px", background: "#10131a", border: "1px solid #07c2d6", borderRadius: 8 }}>
                <div style={{ color: "#aab", fontSize: 12 }}>{t("device.pairCodeCompare")}</div>
                <div style={{ fontFamily: "monospace", fontSize: 32, letterSpacing: 6, color: "#fff", marginTop: 4 }}>{status.pairing_code}</div>
              </div>
            )}
            {pairMessage && <p className="hint">{pairMessage}</p>}
          </Panel>
        )}

        {activeView === "keys" && (
          <Panel title={t("keys.title")}>
            <div className="key-grid">
              {keys.map((binding, index) => (
                <div className="key-card" key={index}>
                  <div className="key-screen">{binding.label || t("keys.keyLabel", { num: index + 1 })}</div>
                  <label>{t("keys.label")}</label>
                  <input value={binding.label} onChange={(event) => updateKey(index, { label: event.target.value })} />
                  <label>{t("keys.action")}</label>
                  <select value={binding.action_type} onChange={(event) => updateKey(index, { action_type: event.target.value })}>
                    <option value="hotkey">{t("keys.hotkey")}</option>
                    <option value="voice">{t("keys.voiceAction")}</option>
                    <option value="agent_next">{t("keys.nextAgent")}</option>
                    <option value="none">{t("keys.none")}</option>
                  </select>
                  <label>{t("keys.key")}</label>
                  <select value={binding.key} onChange={(event) => updateKey(index, { key: event.target.value })}>
                    <option value="">{t("keys.none")}</option>
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
              <button onClick={() => runAction(loadKeymap)}>{t("keys.readBoard")}</button>
              <button onClick={() => runAction(saveKeymap)}>{t("keys.writeBoard")}</button>
            </div>
          </Panel>
        )}

        {activeView === "flash" && (
          <Panel title={t("flash.title")}>
            <label>{t("flash.firmwareLabel")}</label>
            <div className="actions">
              <button onClick={async () => {
                const file = await open({ multiple: false, filters: [{ name: "Firmware", extensions: ["bin"] }] });
                if (file) setFirmwarePath(file);
              }}>{t("flash.selectFile")}</button>
              <span style={{ fontSize: "0.85em", opacity: 0.7 }}>{firmwarePath || t("flash.noFile")}</span>
            </div>

            <div className="test-box">
              <div className="test-head">
                <span>{t("flash.otaFlash")}</span>
                <strong>{status.device_connected ? status.transport.toUpperCase() : t("flash.offline")}</strong>
              </div>
              {otaInProgress && (
                <p className="hint" style={{ color: "#f5a623", fontWeight: "bold" }}>
                  {t("flash.otaInProgress")}
                </p>
              )}
              <p className="hint">
                {t("flash.otaHint")}
              </p>
              <div className="actions">
                <button onClick={() => runAction(otaFlashFirmware)} disabled={otaInProgress || !firmwarePath.trim() || !status.device_connected}>
                  {otaInProgress ? t("flash.flashing") : t("flash.flashButton")}
                </button>
              </div>
            </div>

            {flashOutput && <pre className="terminal">{flashOutput}</pre>}
          </Panel>
        )}

        {activeView === "voice" && (
          <Panel title={t("voice.title")}>
            <label>{t("voice.engineLabel")}</label>
            <div className="chips">
              <button
                className={!isThirdPartyVoice(settings) ? "chip active" : "chip"}
                onClick={() => runAction(() => selectVoiceEngine("system"))}
              >
                {t("voice.systemHid")}
              </button>
              <button
                className={isThirdPartyVoice(settings) ? "chip active" : "chip"}
                onClick={() => runAction(() => selectVoiceEngine("third_party"))}
              >
                {t("voice.thirdPartyAsr")}
              </button>
            </div>
            <p className="hint">
              {isThirdPartyVoice(settings)
                ? t("voice.hintThirdParty")
                : t("voice.hintSystem")}
            </p>

            {isThirdPartyVoice(settings) ? (
              <>
                <label>{t("voice.asrProvider")}</label>
                <select
                  value={settings.asr_provider || "doubao"}
                  onChange={(event) => setSettings(normalizeVoiceSettings({ ...settings, asr_provider: event.target.value }))}
                >
                  <option value="doubao">{t("voice.doubaoProvider")}</option>
                </select>
                <label>{t("voice.audioInput")}</label>
                <select
                  value={settings.audio_input_device}
                  onChange={(event) => setSettings({ ...settings, audio_input_device: event.target.value })}
                >
                  <option value="">{t("voice.systemDefault")}</option>
                  {audioDevices.map((device) => (
                    <option key={device} value={device}>{device}</option>
                  ))}
                </select>
                <div className="actions">
                  <button className="secondary" onClick={() => runAction(loadAudioDevices)}>{t("voice.refreshDevices")}</button>
                </div>

                <label>{t("voice.endpoint")}</label>
                <input value={settings.doubao_endpoint} onChange={(event) => setSettings({ ...settings, doubao_endpoint: event.target.value })} />
                <label>{t("voice.resourceId")}</label>
                <input value={settings.doubao_resource_id} onChange={(event) => setSettings({ ...settings, doubao_resource_id: event.target.value })} />
                <label>{t("voice.languageOverride")}</label>
                <input
                  value={settings.doubao_language}
                  onChange={(event) => setSettings({ ...settings, doubao_language: event.target.value })}
                  placeholder={t("voice.languagePlaceholder")}
                />
                <p className="hint">
                  {t("voice.languageHint")}
                </p>
                <label>{t("voice.apiKeyLabel")}</label>
                <input value={apiKey} onChange={(event) => setApiKey(event.target.value)} type="password" placeholder={t("voice.apiKeyPlaceholder")} />
                <p className={hasApiKey ? "hint good-text" : "hint warn-text"}>
                  {hasApiKey ? t("voice.keySaved") : t("voice.keyMissing")}
                </p>
                <label className="check">
                  <input
                    type="checkbox"
                    checked={settings.paste_after_transcribe}
                    onChange={(event) => setSettings({ ...settings, paste_after_transcribe: event.target.checked })}
                  />
                  {t("voice.pasteTranscript")}
                </label>
                <div className="actions">
                  <button onClick={() => runAction(saveSettings)}>{t("voice.saveVoice")}</button>
                </div>

                <div className="test-box">
                  <div className="test-head">
                    <span>{t("voice.testInput")}</span>
                    <strong>{status.busy ? t("voice.transcribing") : status.recording ? t("voice.listening") : t("voice.ready")}</strong>
                  </div>
                  <div className="transcript-preview">
                    {voiceText || t("voice.testHint")}
                  </div>
                  <div className="actions">
                    <button disabled={status.recording || status.busy} onClick={() => runAction(startRecording)}>{t("voice.start")}</button>
                    <button disabled={!status.recording && !status.busy} onClick={() => runAction(stopRecording)}>{t("voice.stop")}</button>
                    <button className="secondary" disabled={!status.recording} onClick={() => runAction(cancelRecording)}>{t("voice.cancel")}</button>
                  </div>
                </div>
              </>
            ) : (
              <div className="actions">
                <button onClick={() => runAction(saveSettings)}>{t("voice.saveVoice")}</button>
              </div>
            )}
          </Panel>
        )}

        {activeView === "logs" && (
          <Panel title={t("logs.title")}>
            <div className="actions">
              <button className="secondary" onClick={() => runAction(clearLogs)}>{t("logs.clear")}</button>
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
            <h3 style={{ marginTop: 0, color: "#07c2d6" }}>{t("pairModal.title")}</h3>
            <p style={{ color: "#aab" }}>{t("pairModal.question")}</p>
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
            <p style={{ color: "#aab", fontSize: 13 }} dangerouslySetInnerHTML={{ __html: t("pairModal.matchHint") }} />
            <div className="actions" style={{ justifyContent: "center" }}>
              <button className="secondary" onClick={() => setPairCode(null)}>{t("pairModal.close")}</button>
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

createRoot(document.getElementById("root")!).render(<App />);
