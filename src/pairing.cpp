#include "pairing.h"
#include "ble_gatt_comm.h"
#include "agent_registry.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_random.h>
#include <cstring>

static constexpr const char* PAIR_NAMESPACE = "kiropair";
static constexpr const char* PAIR_TOKEN_KEY = "token";
static constexpr unsigned long PAIRING_WINDOW_MS = 30000;

static PairingPhase s_phase = PAIRING_UNPAIRED;
static bool s_auth[2] = {false, false};       // [USB], [BLE]
static char s_code[7] = "";                    // 6-digit code while Pairing
static char s_token[33] = "";                  // 32 hex chars + null
static char s_pendingToken[33] = "";           // token for the active window
static unsigned long s_windowDeadlineMs = 0;
static Print* s_pairOutput = nullptr;          // transport that requested pairing
static PairTransport s_pairTransport = PAIR_TRANSPORT_BLE;

// ---- helpers ---------------------------------------------------------------

static void loadToken() {
  Preferences prefs;
  s_token[0] = '\0';
  if (prefs.begin(PAIR_NAMESPACE, true)) {
    String stored = prefs.getString(PAIR_TOKEN_KEY, "");
    prefs.end();
    strncpy(s_token, stored.c_str(), sizeof(s_token) - 1);
    s_token[sizeof(s_token) - 1] = '\0';
  }
}

static bool persistToken(const char* token) {
  Preferences prefs;
  bool ok = false;
  if (prefs.begin(PAIR_NAMESPACE, false)) {
    ok = prefs.putString(PAIR_TOKEN_KEY, token) >= 0;
    prefs.end();
  }
  return ok;
}

static void clearStoredToken() {
  Preferences prefs;
  if (prefs.begin(PAIR_NAMESPACE, false)) {
    prefs.remove(PAIR_TOKEN_KEY);
    prefs.end();
  }
}

static void genCode() {
  uint32_t n = esp_random() % 1000000UL;
  snprintf(s_code, sizeof(s_code), "%06lu", static_cast<unsigned long>(n));
}

static void genToken(char* out, size_t outSize) {
  static const char* hex = "0123456789abcdef";
  size_t pos = 0;
  for (int i = 0; i < 16 && pos + 2 < outSize; i++) {
    uint8_t b = static_cast<uint8_t>(esp_random() & 0xFF);
    out[pos++] = hex[(b >> 4) & 0x0F];
    out[pos++] = hex[b & 0x0F];
  }
  out[pos] = '\0';
}

static void sendLine(Print& out, const char* line) {
  out.println(line);
}

static void sendPairCode(Print& out) {
  char line[160];
  snprintf(line, sizeof(line),
           "{\"type\":\"pair_code\",\"code\":\"%s\",\"board_id\":\"%s\",\"name\":\"Kiro KB\"}",
           s_code, bleGattCommBoardId());
  sendLine(out, line);
}

static void sendPairOk(Print& out) {
  char line[160];
  snprintf(line, sizeof(line),
           "{\"type\":\"pair_ok\",\"token\":\"%s\",\"board_id\":\"%s\"}",
           s_token, bleGattCommBoardId());
  sendLine(out, line);
}

static void sendSimple(Print& out, const char* type) {
  char line[64];
  snprintf(line, sizeof(line), "{\"type\":\"%s\"}", type);
  sendLine(out, line);
}

static void sendPairFailed(Print& out, const char* reason) {
  char line[96];
  snprintf(line, sizeof(line), "{\"type\":\"pair_failed\",\"reason\":\"%s\"}", reason);
  sendLine(out, line);
}

// ---- public API ------------------------------------------------------------

void pairingBegin() {
  loadToken();
  s_phase = (s_token[0] != '\0') ? PAIRING_PAIRED : PAIRING_UNPAIRED;
  s_auth[PAIR_TRANSPORT_USB] = false;
  s_auth[PAIR_TRANSPORT_BLE] = false;
  s_code[0] = '\0';
}

void pairingEnterMode() {
  genCode();
  genToken(s_pendingToken, sizeof(s_pendingToken));
  s_phase = PAIRING_PAIRING;
  s_windowDeadlineMs = millis() + PAIRING_WINDOW_MS;
  Serial.printf("[PAIR] pairing window open, code=%s\n", s_code);
}

void pairingConfirm() {
  if (s_phase != PAIRING_PAIRING) {
    return;
  }
  // Only finalize if a companion actually requested pairing (we have a channel
  // to deliver the token on).
  if (s_pairOutput == nullptr) {
    Serial.println("[PAIR] confirm ignored: no companion has requested pairing yet");
    return;
  }
  strncpy(s_token, s_pendingToken, sizeof(s_token) - 1);
  s_token[sizeof(s_token) - 1] = '\0';
  persistToken(s_token);
  s_phase = PAIRING_PAIRED;
  s_auth[s_pairTransport] = true;
  s_code[0] = '\0';
  Serial.println("[PAIR] confirmed, binding stored");
  sendPairOk(*s_pairOutput);
}

void pairingCancel() {
  if (s_phase != PAIRING_PAIRING) {
    return;
  }
  s_phase = (s_token[0] != '\0') ? PAIRING_PAIRED : PAIRING_UNPAIRED;
  s_code[0] = '\0';
  Serial.println("[PAIR] pairing cancelled");
  if (s_pairOutput) {
    sendPairFailed(*s_pairOutput, "cancelled");
  }
}

bool pairingPoll(unsigned long now) {
  if (s_phase == PAIRING_PAIRING && (long)(now - s_windowDeadlineMs) >= 0) {
    s_phase = (s_token[0] != '\0') ? PAIRING_PAIRED : PAIRING_UNPAIRED;
    s_code[0] = '\0';
    Serial.println("[PAIR] pairing window timed out");
    if (s_pairOutput) {
      sendPairFailed(*s_pairOutput, "timeout");
    }
    return true;
  }
  return false;
}

PairLineAction pairingHandleLine(const char* line, Print& out, PairTransport transport) {
  JsonDocument doc;
  if (deserializeJson(doc, line)) {
    return PAIR_LINE_FORWARD;
  }
  const char* type = doc["type"] | "";

  if (strcmp(type, "pair_request") == 0) {
    companionMarkSeen();
    if (transport == PAIR_TRANSPORT_USB) {
      // USB is physically trusted: provision a token immediately. No 6-digit
      // code and no on-device confirm needed.
      genToken(s_token, sizeof(s_token));
      persistToken(s_token);
      s_phase = PAIRING_PAIRED;
      Serial.println("[PAIR] USB trusted pairing: token issued");
      sendPairOk(out);
      return PAIR_LINE_CONSUMED;
    }
    // BLE requires an on-device pairing window + button confirm.
    s_pairOutput = &out;
    s_pairTransport = transport;
    if (s_phase == PAIRING_PAIRING) {
      sendPairCode(out);
    } else {
      sendPairFailed(out, "not_pairing");
    }
    return PAIR_LINE_CONSUMED;
  }

  if (strcmp(type, "auth") == 0) {
    companionMarkSeen();
    const char* token = doc["token"] | "";
    if (s_phase == PAIRING_PAIRED && s_token[0] != '\0' && strcmp(token, s_token) == 0) {
      s_auth[transport] = true;
      sendSimple(out, "auth_ok");
    } else {
      s_auth[transport] = false;
      sendSimple(out, "auth_required");
    }
    return PAIR_LINE_CONSUMED;
  }

  if (strcmp(type, "unpair") == 0) {
    companionMarkSeen();
    clearStoredToken();
    s_token[0] = '\0';
    s_phase = PAIRING_UNPAIRED;
    s_auth[PAIR_TRANSPORT_USB] = false;
    s_auth[PAIR_TRANSPORT_BLE] = false;
    sendSimple(out, "unpaired");
    return PAIR_LINE_CONSUMED;
  }

  // Connection-level messages always allowed. A fresh hello resets this
  // transport's auth so each new companion session must re-authenticate.
  if (strcmp(type, "hello") == 0 || strcmp(type, "companion_hello") == 0) {
    s_auth[transport] = false;
    return PAIR_LINE_FORWARD;
  }
  if (strcmp(type, "ping") == 0 || strcmp(type, "companion_ping") == 0) {
    return PAIR_LINE_FORWARD;
  }

  // Privileged commands: USB is trusted; BLE requires authentication.
  if (strcmp(type, "agent_state") == 0 || strcmp(type, "get_keymap") == 0 ||
      strcmp(type, "set_keymap") == 0) {
    if (transport == PAIR_TRANSPORT_USB || s_auth[transport]) {
      return PAIR_LINE_FORWARD;
    }
    sendSimple(out, "auth_required");
    return PAIR_LINE_CONSUMED;
  }

  return PAIR_LINE_FORWARD;
}

void pairingOnDisconnect(PairTransport transport) {
  s_auth[transport] = false;
  if (s_phase == PAIRING_PAIRING && s_pairTransport == transport) {
    s_phase = (s_token[0] != '\0') ? PAIRING_PAIRED : PAIRING_UNPAIRED;
    s_code[0] = '\0';
    s_pairOutput = nullptr;
  }
}

PairingPhase pairingPhase() {
  return s_phase;
}

const char* pairingCodeStr() {
  return s_code;
}
