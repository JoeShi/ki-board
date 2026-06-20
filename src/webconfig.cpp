#include "webconfig.h"
#include "webpage.h"
#include "wifi_config.h"
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>

static AsyncWebServer s_server(80);
static bool s_started = false;
static bool s_mdnsStarted = false;
static String s_postBody;

static void sendJson(AsyncWebServerRequest* request, JsonDocument& doc, int status = 200) {
  String out;
  serializeJson(doc, out);
  request->send(status, "application/json", out);
}

static void handleStatus(AsyncWebServerRequest* request) {
  JsonDocument doc;
  doc["mode"] = wifiModeLabel();
  doc["connected"] = wifiIsConnected();
  doc["hasCredentials"] = wifiHasSavedCredentials();
  doc["ssid"] = wifiCurrentSsid();
  doc["ip"] = wifiIpAddress();
  doc["deviceCode"] = wifiDeviceCode();
  doc["apSsid"] = wifiApSsid();
  doc["apPassword"] = wifiApPassword();
  doc["mdns"] = s_mdnsStarted ? "kirokb.local" : "";
  sendJson(request, doc);
}

static void handleScan(AsyncWebServerRequest* request) {
  int count = WiFi.scanNetworks(false, true);

  JsonDocument doc;
  JsonArray networks = doc["networks"].to<JsonArray>();
  for (int i = 0; i < count; i++) {
    JsonObject network = networks.add<JsonObject>();
    network["ssid"] = WiFi.SSID(i);
    network["rssi"] = WiFi.RSSI(i);
    network["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  sendJson(request, doc);
}

static void handleWifiBody(AsyncWebServerRequest* request, uint8_t* data,
                           size_t len, size_t index, size_t total) {
  if (index == 0) {
    s_postBody = "";
  }
  s_postBody.concat(reinterpret_cast<const char*>(data), len);

  if (index + len != total) {
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, s_postBody);
  s_postBody = "";

  JsonDocument response;
  if (err) {
    response["ok"] = false;
    response["error"] = "invalid_json";
    sendJson(request, response, 400);
    return;
  }

  const char* ssid = doc["ssid"] | "";
  const char* password = doc["password"] | "";
  bool ok = wifiSaveCredentials(String(ssid), String(password));
  response["ok"] = ok;
  if (!ok) {
    response["error"] = "invalid_or_not_saved";
  }
  sendJson(request, response, ok ? 200 : 400);
}

static void handleForget(AsyncWebServerRequest* request) {
  wifiForgetCredentials();
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(request, doc);
}

void webConfigBegin() {
  if (s_started) {
    return;
  }
  s_started = true;

  s_server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "text/html", INDEX_HTML);
  });
  s_server.on("/api/wifi/status", HTTP_GET, handleStatus);
  s_server.on("/api/wifi/scan", HTTP_GET, handleScan);
  s_server.on("/api/wifi", HTTP_POST,
              [](AsyncWebServerRequest* request) {},
              nullptr,
              handleWifiBody);
  s_server.on("/api/wifi/forget", HTTP_POST, handleForget);

  s_server.onNotFound([](AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "Not found");
  });

  s_server.begin();
  Serial.println("[WEB] Server started");
}

void webConfigStartMdns() {
  if (s_mdnsStarted) {
    return;
  }

  if (MDNS.begin("kirokb")) {
    MDNS.addService("http", "tcp", 80);
    s_mdnsStarted = true;
    Serial.println("[WEB] mDNS: http://kirokb.local");
  } else {
    Serial.println("[WEB] mDNS start failed");
  }
}
