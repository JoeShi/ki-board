#include "wifi_ota.h"
#include "wifi_config.h"
#include "ota_manager.h"
#include "wifi_ota_page.h"
#include "display_hardware.h"
#include "ui_render.h"
#include <WiFi.h>
#include <mbedtls/sha256.h>
#include <Update.h>
#include <ESPAsyncWebServer.h>

// ─── Timeout and size constants ───────────────────────────────────────────────
static constexpr unsigned long OTA_AP_TIMEOUT_MS     = 3000;   // AP startup timeout
static constexpr unsigned long OTA_IDLE_TIMEOUT_MS   = 300000; // No connection/activity timeout (5 min)
static constexpr unsigned long OTA_UPLOAD_TIMEOUT_MS = 180000; // Total upload timeout (3 min)
static constexpr unsigned long OTA_CHUNK_TIMEOUT_MS  = 15000;  // Inter-chunk timeout (15s)
static constexpr unsigned long OTA_REBOOT_DELAY_MS   = 900;    // Delay before reboot after success
static constexpr unsigned long OTA_ERROR_DISPLAY_MS  = 5000;   // Error display time before reboot
static constexpr size_t        OTA_MAX_FIRMWARE_SIZE = 7340032; // 7MB partition size

// ─── Internal state context ───────────────────────────────────────────────────
struct WifiOtaContext {
    WifiOtaState state;           // Current OTA state
    WifiOtaError error;           // Error type
    unsigned long stateEnteredMs; // Timestamp when entering current state
    unsigned long lastActivityMs; // Last activity timestamp (chunk/connection)
    size_t expectedSize;          // Expected firmware size
    size_t writtenBytes;          // Bytes written so far
    char expectedSha256[65];      // Expected SHA-256 hex string
    mbedtls_sha256_context sha;   // SHA-256 accumulation context
    bool shaInitialized;          // Whether SHA context is initialized
    bool uploadInProgress;        // Whether an upload is in progress
    unsigned long lastDisplayMs;  // Last display refresh timestamp
};

static WifiOtaContext ctx = {};
static AsyncWebServer* otaServer = nullptr;

// Firmware version from build system
#ifndef FW_VERSION_MAJOR
#define FW_VERSION_MAJOR 0
#endif
#ifndef FW_VERSION_MINOR
#define FW_VERSION_MINOR 0
#endif
#ifndef FW_VERSION_PATCH
#define FW_VERSION_PATCH 0
#endif

static String fwVersionString() {
    return String(FW_VERSION_MAJOR) + "." + String(FW_VERSION_MINOR) + "." + String(FW_VERSION_PATCH);
}

// ─── State query functions ────────────────────────────────────────────────────

bool wifiOtaIsActive() {
    return ctx.state != WIFI_OTA_IDLE;
}

WifiOtaState wifiOtaGetState() {
    return ctx.state;
}

uint8_t wifiOtaProgress() {
    if (ctx.expectedSize == 0) return 0;
    return (uint8_t)min((size_t)100, ctx.writtenBytes * 100 / ctx.expectedSize);
}

WifiOtaError wifiOtaGetError() {
    return ctx.error;
}

// ─── OTA Web Server (tasks 3.2-3.8) ──────────────────────────────────────────

static void handleUpload(AsyncWebServerRequest *request, String filename,
                         size_t index, uint8_t *data, size_t len, bool final) {
    // First chunk (index == 0)
    if (index == 0) {
        // Upload mutex (task 3.8)
        if (ctx.uploadInProgress) {
            Serial.println("[WIFI_OTA] Rejected: upload already in progress");
            return;
        }

        size_t contentLength = request->contentLength();

        // Size validation (task 3.5)
        if (contentLength == 0) {
            Serial.println("[WIFI_OTA] Rejected: empty file");
            ctx.state = WIFI_OTA_ERROR;
            ctx.error = WIFI_OTA_ERR_EMPTY_FILE;
            ctx.stateEnteredMs = millis();
            return;
        }
        if (contentLength > OTA_MAX_FIRMWARE_SIZE) {
            Serial.printf("[WIFI_OTA] Rejected: file too large (%u > %u)\n",
                          (unsigned)contentLength, (unsigned)OTA_MAX_FIRMWARE_SIZE);
            ctx.state = WIFI_OTA_ERROR;
            ctx.error = WIFI_OTA_ERR_TOO_LARGE;
            ctx.stateEnteredMs = millis();
            return;
        }

        ctx.uploadInProgress = true;
        ctx.expectedSize = contentLength;
        ctx.writtenBytes = 0;
        ctx.state = WIFI_OTA_UPLOADING;
        ctx.stateEnteredMs = millis();
        ctx.lastActivityMs = millis();

        // Initialize SHA-256 context
        mbedtls_sha256_init(&ctx.sha);
        mbedtls_sha256_starts(&ctx.sha, 0); // 0 = SHA-256
        ctx.shaInitialized = true;

        // Begin Update
        if (!Update.begin(contentLength)) {
            Serial.printf("[WIFI_OTA] Update.begin failed: %s\n", Update.errorString());
            ctx.state = WIFI_OTA_ERROR;
            ctx.error = WIFI_OTA_ERR_WRITE_FAIL;
            ctx.stateEnteredMs = millis();
            ctx.uploadInProgress = false;
            mbedtls_sha256_free(&ctx.sha);
            ctx.shaInitialized = false;
            return;
        }

        Serial.printf("[WIFI_OTA] Upload started: %s (%u bytes)\n",
                      filename.c_str(), (unsigned)contentLength);
    }

    // Skip data if in error state
    if (ctx.state != WIFI_OTA_UPLOADING) return;

    // Write chunk (task 3.6)
    if (len > 0) {
        size_t written = Update.write(data, len);
        if (written != len) {
            Serial.printf("[WIFI_OTA] Write failed: wrote %u of %u\n",
                          (unsigned)written, (unsigned)len);
            ctx.state = WIFI_OTA_ERROR;
            ctx.error = WIFI_OTA_ERR_WRITE_FAIL;
            ctx.stateEnteredMs = millis();
            Update.abort();
            ctx.uploadInProgress = false;
            return;
        }

        // Update SHA-256
        mbedtls_sha256_update(&ctx.sha, data, len);

        ctx.writtenBytes += len;
        ctx.lastActivityMs = millis();
    }

    // Final chunk (task 3.7)
    if (final) {
        Serial.printf("[WIFI_OTA] Upload complete: %u bytes\n", (unsigned)ctx.writtenBytes);

        ctx.state = WIFI_OTA_VERIFYING;

        // Finalize SHA-256
        uint8_t sha256Hash[32];
        mbedtls_sha256_finish(&ctx.sha, sha256Hash);
        mbedtls_sha256_free(&ctx.sha);
        ctx.shaInitialized = false;

        // End update (sets boot partition)
        if (!Update.end(true)) {
            Serial.printf("[WIFI_OTA] Update.end failed: %s\n", Update.errorString());
            ctx.state = WIFI_OTA_ERROR;
            ctx.error = WIFI_OTA_ERR_UPDATE_END;
            ctx.stateEnteredMs = millis();
            ctx.uploadInProgress = false;
            return;
        }

        // Success
        ctx.state = WIFI_OTA_SUCCESS;
        ctx.stateEnteredMs = millis();
        ctx.uploadInProgress = false;

        Serial.print("[WIFI_OTA] SHA-256: ");
        for (int i = 0; i < 32; i++) Serial.printf("%02x", sha256Hash[i]);
        Serial.println();
        Serial.println("[WIFI_OTA] Update successful!");
    }
}

static void setupOtaWebServer() {
    if (otaServer) {
        delete otaServer;
    }
    otaServer = new AsyncWebServer(80);

    // GET / - Serve OTA HTML page (task 3.2)
    otaServer->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        ctx.lastActivityMs = millis();
        String page = String(OTA_PAGE_HTML);
        page.replace("%FW_VERSION%", fwVersionString());
        request->send(200, "text/html", page);
    });

    // GET /ota/status - JSON status (task 3.3)
    otaServer->on("/ota/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        ctx.lastActivityMs = millis();
        String json = "{\"state\":" + String((int)ctx.state) +
                      ",\"progress\":" + String(wifiOtaProgress()) +
                      ",\"error\":" + String((int)ctx.error) +
                      ",\"version\":\"" + fwVersionString() + "\"}";
        request->send(200, "application/json", json);
    });

    // POST /ota/upload - Firmware upload (tasks 3.4, 3.5, 3.6, 3.7, 3.8)
    otaServer->on("/ota/upload", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            String json;
            if (ctx.state == WIFI_OTA_SUCCESS) {
                json = "{\"ok\":true,\"message\":\"Update successful, rebooting...\"}";
                request->send(200, "application/json", json);
            } else {
                String errMsg;
                switch (ctx.error) {
                    case WIFI_OTA_ERR_TOO_LARGE:  errMsg = "File too large (max 7MB)"; break;
                    case WIFI_OTA_ERR_EMPTY_FILE: errMsg = "Empty file"; break;
                    case WIFI_OTA_ERR_WRITE_FAIL: errMsg = "Write failed"; break;
                    case WIFI_OTA_ERR_UPDATE_END: errMsg = "Update finalization failed"; break;
                    default:                      errMsg = "Upload failed"; break;
                }
                json = "{\"ok\":false,\"message\":\"" + errMsg + "\"}";
                request->send(400, "application/json", json);
            }
        },
        handleUpload
    );

    otaServer->begin();
    Serial.println("[WIFI_OTA] Web server started on port 80");
}

// ─── OTA Begin ────────────────────────────────────────────────────────────────

bool wifiOtaBegin() {
    if (otaIsActive()) {
        Serial.println("[WIFI_OTA] Rejected: serial/BLE OTA already active");
        return false;
    }
    if (wifiOtaIsActive()) {
        Serial.println("[WIFI_OTA] Already active");
        return false;
    }

    Serial.println("[WIFI_OTA] Starting WiFi OTA mode...");

    ctx.state = WIFI_OTA_AP_STARTING;
    ctx.error = WIFI_OTA_ERR_NONE;
    ctx.stateEnteredMs = millis();
    ctx.lastActivityMs = millis();
    ctx.expectedSize = 0;
    ctx.writtenBytes = 0;
    ctx.shaInitialized = false;
    ctx.uploadInProgress = false;
    ctx.lastDisplayMs = 0;
    memset(ctx.expectedSha256, 0, sizeof(ctx.expectedSha256));

    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);

    String ssid = wifiOtaApSsid();
    String pass = wifiOtaApPassword();
    bool apOk = WiFi.softAP(ssid.c_str(), pass.c_str(), 1, 0, 1);

    if (!apOk) {
        Serial.println("[WIFI_OTA] ERROR: AP start failed");
        ctx.state = WIFI_OTA_ERROR;
        ctx.error = WIFI_OTA_ERR_AP_FAIL;
        ctx.stateEnteredMs = millis();
        return false;
    }

    ctx.state = WIFI_OTA_AP_READY;
    ctx.stateEnteredMs = millis();
    ctx.lastActivityMs = millis();

    Serial.printf("[WIFI_OTA] AP ready: SSID=%s IP=%s\n",
                  ssid.c_str(), WiFi.softAPIP().toString().c_str());

    // Start web server
    setupOtaWebServer();

    // Display AP info (tasks 5.1, 5.2)
    drawOtaRect(rectDisplay(), "Entering OTA...", 0);
    drawOtaRound(&roundDisplay(), 0);

    return true;
}

// ─── Cleanup helper ───────────────────────────────────────────────────────────
static void wifiOtaCleanup() {
    if (ctx.shaInitialized) {
        mbedtls_sha256_free(&ctx.sha);
        ctx.shaInitialized = false;
    }

    if (ctx.uploadInProgress) {
        Update.abort();
        ctx.uploadInProgress = false;
    }

    // Stop web server
    if (otaServer) {
        otaServer->end();
        delete otaServer;
        otaServer = nullptr;
    }

    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);

    Serial.println("[WIFI_OTA] Cleanup complete");
}

// ─── Main loop ────────────────────────────────────────────────────────────────
void wifiOtaLoop() {
    if (!wifiOtaIsActive()) return;

    unsigned long now = millis();

    switch (ctx.state) {
        case WIFI_OTA_AP_READY:
            if (now - ctx.lastActivityMs >= OTA_IDLE_TIMEOUT_MS) {
                Serial.println("[WIFI_OTA] Timeout: no activity in AP_READY");
                ctx.state = WIFI_OTA_TIMEOUT;
                ctx.error = WIFI_OTA_ERR_TIMEOUT;
                ctx.stateEnteredMs = now;
            }
            break;

        case WIFI_OTA_UPLOADING:
            if (now - ctx.lastActivityMs >= OTA_CHUNK_TIMEOUT_MS) {
                Serial.println("[WIFI_OTA] Timeout: chunk timeout");
                ctx.state = WIFI_OTA_TIMEOUT;
                ctx.error = WIFI_OTA_ERR_TIMEOUT;
                ctx.stateEnteredMs = now;
            } else if (now - ctx.stateEnteredMs >= OTA_UPLOAD_TIMEOUT_MS) {
                Serial.println("[WIFI_OTA] Timeout: total upload timeout");
                ctx.state = WIFI_OTA_TIMEOUT;
                ctx.error = WIFI_OTA_ERR_TIMEOUT;
                ctx.stateEnteredMs = now;
            }
            break;

        case WIFI_OTA_TIMEOUT:
        case WIFI_OTA_ERROR:
            if (now - ctx.stateEnteredMs >= OTA_ERROR_DISPLAY_MS) {
                Serial.println("[WIFI_OTA] Rebooting after error/timeout...");
                wifiOtaCleanup();
                ESP.restart();
            }
            break;

        case WIFI_OTA_SUCCESS:
            if (now - ctx.stateEnteredMs >= OTA_REBOOT_DELAY_MS) {
                Serial.println("[WIFI_OTA] Rebooting after success...");
                wifiOtaCleanup();
                ESP.restart();
            }
            break;

        default:
            break;
    }
}

// ─── Display helpers (tasks 5.1-5.6) ─────────────────────────────────────────

const char* wifiOtaPhase() {
    switch (ctx.state) {
        case WIFI_OTA_AP_STARTING:  return "Starting AP...";
        case WIFI_OTA_AP_READY:     return "Waiting...";
        case WIFI_OTA_UPLOADING:    return "Uploading";
        case WIFI_OTA_VERIFYING:    return "Verifying";
        case WIFI_OTA_SUCCESS:      return "Update OK";
        case WIFI_OTA_TIMEOUT:      return "Timeout";
        case WIFI_OTA_ERROR:        return wifiOtaErrorMessage();
        default:                    return "WiFi OTA";
    }
}

const char* wifiOtaErrorMessage() {
    switch (ctx.error) {
        case WIFI_OTA_ERR_AP_FAIL:         return "AP Failed";
        case WIFI_OTA_ERR_TOO_LARGE:       return "Too Large";
        case WIFI_OTA_ERR_EMPTY_FILE:      return "Empty File";
        case WIFI_OTA_ERR_WRITE_FAIL:      return "Write Fail";
        case WIFI_OTA_ERR_SHA256_MISMATCH: return "SHA Mismatch";
        case WIFI_OTA_ERR_TIMEOUT:         return "Timeout";
        case WIFI_OTA_ERR_UPDATE_END:      return "End Failed";
        default:                           return "Error";
    }
}

// ─── AP Credentials ───────────────────────────────────────────────────────────
String wifiOtaApSsid() {
    return "KiroKB-" + wifiDeviceCode();
}

String wifiOtaApPassword() {
    return "kiro-" + wifiDeviceCode();
}
