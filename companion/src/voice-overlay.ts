import { listen } from "@tauri-apps/api/event";
import { getCurrentWindow } from "@tauri-apps/api/window";

const statusEl = document.getElementById("status")!;
const textEl = document.getElementById("text")!;
const closeBtn = document.getElementById("closeBtn")!;
const win = getCurrentWindow();

listen<{ event: string; text: string }>("voice-event", (event) => {
  const { event: evt, text } = event.payload;
  if (evt === "partial") {
    textEl.textContent = text || "Speak now...";
    textEl.className = text ? "text" : "text empty";
    statusEl.textContent = "● Listening...";
  } else if (evt === "final") {
    textEl.textContent = text || "(empty)";
    textEl.className = "text";
    statusEl.textContent = "✓ Done";
  } else if (evt === "error") {
    textEl.textContent = text || "Error";
    textEl.className = "text";
    statusEl.textContent = "✗ Error";
  }
});

listen("voice-overlay-close", () => win.close());
closeBtn.addEventListener("click", () => win.close());
