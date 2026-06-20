import { listen } from "@tauri-apps/api/event";
import { invoke } from "@tauri-apps/api/core";
import { getCurrentWindow } from "@tauri-apps/api/window";

const statusEl = document.getElementById("status")!;
const textEl = document.getElementById("text")!;
const closeBtn = document.getElementById("closeBtn")!;
const win = getCurrentWindow();
let hasLiveText = false;

type CompanionStatus = {
  recording: boolean;
  busy: boolean;
  last_transcript: string;
  last_partial: string;
  last_error: string;
};

function setText(text: string, emptyText: string) {
  textEl.textContent = text || emptyText;
  textEl.className = text ? "text" : "text empty";
}

function renderStatus(status: CompanionStatus, fallbackOnly = false) {
  if (status.recording) {
    if (fallbackOnly && hasLiveText) {
      statusEl.textContent = "● Listening...";
      return;
    }
    setText(status.last_partial, "Speak now...");
    statusEl.textContent = "● Listening...";
    return;
  }
  if (status.busy) {
    if (fallbackOnly && hasLiveText) {
      statusEl.textContent = "● Transcribing...";
      return;
    }
    setText(status.last_partial || status.last_transcript, "Transcribing...");
    statusEl.textContent = "● Transcribing...";
    return;
  }
  if (status.last_error) {
    setText(status.last_error, "Error");
    statusEl.textContent = "✗ Error";
    return;
  }
  setText(status.last_transcript, "Speak now...");
}

async function syncStatus() {
  try {
    renderStatus(await invoke<CompanionStatus>("get_status"), true);
  } catch {
    // The event listener below is still the primary live update path.
  }
}

listen<{ event: string; text: string }>("voice-event", (event) => {
  const { event: evt, text } = event.payload;
  if (evt === "partial") {
    hasLiveText = Boolean(text);
    setText(text, "Speak now...");
    statusEl.textContent = "● Listening...";
  } else if (evt === "final") {
    hasLiveText = Boolean(text);
    setText(text, "(empty)");
    statusEl.textContent = "✓ Done";
  } else if (evt === "error") {
    hasLiveText = Boolean(text);
    setText(text, "Error");
    statusEl.textContent = "✗ Error";
  }
});

async function closeOverlay() {
  try { await win.destroy(); } catch {}
}

listen("voice-overlay-close", closeOverlay);
listen("voice-recording-stop", closeOverlay);
closeBtn.addEventListener("click", async () => {
  await invoke("cancel_recording").catch(() => {});
  await closeOverlay();
});

invoke<CompanionStatus>("get_status")
  .then((status) => renderStatus(status))
  .catch(() => {});
const statusTimer = window.setInterval(syncStatus, 1000);
window.addEventListener("beforeunload", () => window.clearInterval(statusTimer));
