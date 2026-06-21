#include "wifi_config.h"
#include "webconfig.h"
#include <WiFi.h>
#include <Preferences.h>
#include <esp_mac.h>

static constexpr const char* NVS_NAMESPACE = "wifi";
static constexpr const char* NVS_KEY_SSID = "ssid";
static constexpr const char* NVS_KEY_PASSWORD = "password";
static constexpr unsigned long STA_CONNECT_TIMEOUT_MS = 15000;
static constexpr unsigned long STA_DISCONNECT_GRACE_MS = 10000;

static WifiConfigMode s_mode = WIFI_CONFIG_AP;
static String s_savedSsid;
static String s_savedPassword;
static unsigned long s_connectStartedMs = 0;
static unsigned long s_disconnectedSinceMs = 0;

static String macSuffix() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char code[7];
  snprintf(code, sizeof(code), "%02X%02X%02X", mac[3], mac[4], mac[5]);
  return String(code);
}

static bool loadCredentials() {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) {
    return false;
  }
  s_savedSsid = prefs.getString(NVS_KEY_SSID, "");
  s_savedPassword = prefs.getString(NVS_KEY_PASSWORD, "");
  prefs.end();
  return s_savedSsid.length() > 0;
}

static void startApMode() {
  s_mode = WIFI_CONFIG_AP;
  s_disconnectedSinceMs = 0;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(wifiApSsid().c_str(), wifiApPassword().c_str());

  Serial.printf("[WIFI] AP mode: ssid=%s ip=%s\n",
                wifiApSsid().c_str(),
                WiFi.softAPIP().toString().c_str());
}

static void startStaConnect() {
  s_mode = WIFI_CONFIG_STA_CONNECTING;
  s_connectStartedMs = millis();
  s_disconnectedSinceMs = 0;

  WiFi.mode(WIFI_STA);
  WiFi.begin(s_savedSsid.c_str(), s_savedPassword.c_str());
  Serial.printf("[WIFI] Connecting to %s\n", s_savedSsid.c_str());
}

void wifiConfigBegin() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);

  loadCredentials();
  if (s_savedSsid.length() > 0) {
    startStaConnect();
  } else {
    startApMode();
  }

  webConfigBegin();
}

void wifiConfigLoop() {
  const unsigned long now = millis();

  if (s_mode == WIFI_CONFIG_STA_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      s_mode = WIFI_CONFIG_STA_CONNECTED;
      Serial.printf("[WIFI] Connected: ip=%s\n", WiFi.localIP().toString().c_str());
      webConfigStartMdns();
      return;
    }

    if (now - s_connectStartedMs >= STA_CONNECT_TIMEOUT_MS) {
      Serial.println("[WIFI] Connect timeout, falling back to AP");
      startApMode();
    }
    return;
  }

  if (s_mode == WIFI_CONFIG_STA_CONNECTED) {
    if (WiFi.status() == WL_CONNECTED) {
      s_disconnectedSinceMs = 0;
      return;
    }

    if (s_disconnectedSinceMs == 0) {
      s_disconnectedSinceMs = now;
      Serial.println("[WIFI] Disconnected");
      return;
    }

    if (now - s_disconnectedSinceMs >= STA_DISCONNECT_GRACE_MS) {
      Serial.println("[WIFI] Disconnect grace elapsed, falling back to AP");
      startApMode();
    }
  }
}

WifiConfigMode wifiConfigMode() {
  return s_mode;
}

bool wifiIsConnected() {
  return s_mode == WIFI_CONFIG_STA_CONNECTED && WiFi.status() == WL_CONNECTED;
}

bool wifiHasSavedCredentials() {
  return s_savedSsid.length() > 0;
}

String wifiDeviceCode() {
  return macSuffix();
}

String wifiApSsid() {
  return "KiroKB-" + wifiDeviceCode();
}

String wifiApPassword() {
  return "kiro-" + wifiDeviceCode();
}

String wifiCurrentSsid() {
  if (wifiIsConnected()) {
    return WiFi.SSID();
  }
  return s_savedSsid;
}

String wifiIpAddress() {
  if (wifiIsConnected()) {
    return WiFi.localIP().toString();
  }
  if (s_mode == WIFI_CONFIG_AP) {
    return WiFi.softAPIP().toString();
  }
  return "";
}

String wifiModeLabel() {
  switch (s_mode) {
    case WIFI_CONFIG_STA_CONNECTED:
      return "STA";
    case WIFI_CONFIG_STA_CONNECTING:
      return "CONNECT";
    case WIFI_CONFIG_AP:
    default:
      return "AP";
  }
}

bool wifiSaveCredentials(const String& ssid, const String& password) {
  if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 64) {
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) {
    return false;
  }
  bool ok = prefs.putString(NVS_KEY_SSID, ssid) > 0;
  ok = prefs.putString(NVS_KEY_PASSWORD, password) > 0 && ok;
  prefs.end();

  if (!ok) {
    return false;
  }

  s_savedSsid = ssid;
  s_savedPassword = password;
  WiFi.softAPdisconnect(true);
  startStaConnect();
  return true;
}

void wifiForgetCredentials() {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.remove(NVS_KEY_SSID);
    prefs.remove(NVS_KEY_PASSWORD);
    prefs.end();
  }
  s_savedSsid = "";
  s_savedPassword = "";
  WiFi.disconnect(true, true);
  startApMode();
  Serial.println("[WIFI] Credentials cleared");
}

void wifiStartReconnect() {
  if (s_savedSsid.length() == 0) {
    startApMode();
    return;
  }
  WiFi.softAPdisconnect(true);
  startStaConnect();
}
