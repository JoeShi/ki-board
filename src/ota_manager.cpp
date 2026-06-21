#include "ota_manager.h"

#include <Update.h>
#include <esp_ota_ops.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
#include <cstring>

static constexpr size_t OTA_CHUNK_BYTES = 512;
static constexpr unsigned long OTA_REBOOT_DELAY_MS = 900;
static constexpr unsigned long OTA_IDLE_TIMEOUT_MS = 15000;

static bool s_active = false;
static bool s_rebootPending = false;
static unsigned long s_rebootAtMs = 0;
static unsigned long s_lastActivityMs = 0;
static size_t s_expectedSize = 0;
static size_t s_written = 0;
static char s_expectedSha256[65] = "";
static char s_phase[16] = "idle";
static mbedtls_sha256_context s_sha;
static bool s_shaStarted = false;

static void sendResponse(Print& output, const char* requestId, bool ok,
                         const char* phase, const char* error = nullptr) {
  JsonDocument response;
  response["type"] = "ota_response";
  response["request_id"] = requestId ? requestId : "";
  response["ok"] = ok;
  response["phase"] = phase;
  response["offset"] = s_written;
  response["progress"] = otaProgressPercent();
  if (error) {
    response["error"] = error;
  }
  serializeJson(response, output);
  output.println();
}

static bool isHexSha256(const char* value) {
  if (!value || strlen(value) != 64) {
    return false;
  }
  for (size_t i = 0; i < 64; i++) {
    const char c = value[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
      return false;
    }
  }
  return true;
}

static void bytesToHex(const uint8_t* bytes, size_t len, char* out, size_t outSize) {
  static const char* hex = "0123456789abcdef";
  if (outSize < len * 2 + 1) {
    if (outSize > 0) {
      out[0] = '\0';
    }
    return;
  }
  for (size_t i = 0; i < len; i++) {
    out[i * 2] = hex[(bytes[i] >> 4) & 0x0F];
    out[i * 2 + 1] = hex[bytes[i] & 0x0F];
  }
  out[len * 2] = '\0';
}

static void resetState(const char* phase) {
  if (s_active) {
    Update.abort();
  }
  s_active = false;
  s_expectedSize = 0;
  s_written = 0;
  s_expectedSha256[0] = '\0';
  strncpy(s_phase, phase, sizeof(s_phase) - 1);
  s_phase[sizeof(s_phase) - 1] = '\0';
  if (s_shaStarted) {
    mbedtls_sha256_free(&s_sha);
    s_shaStarted = false;
  }
}

static bool handleBegin(const JsonDocument& request, Print& output) {
  const char* requestId = request["request_id"] | "";
  const size_t size = request["size"] | 0;
  const char* sha256 = request["sha256"] | "";

  if (s_active) {
    sendResponse(output, requestId, false, s_phase, "ota_already_active");
    return true;
  }
  if (size == 0) {
    sendResponse(output, requestId, false, "begin", "invalid_size");
    return true;
  }
  if (!isHexSha256(sha256)) {
    sendResponse(output, requestId, false, "begin", "invalid_sha256");
    return true;
  }

  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  if (!next) {
    sendResponse(output, requestId, false, "begin", "no_ota_partition");
    return true;
  }
  if (size > next->size) {
    sendResponse(output, requestId, false, "begin", "image_too_large");
    return true;
  }
  if (!Update.begin(size, U_FLASH)) {
    sendResponse(output, requestId, false, "begin", Update.errorString());
    return true;
  }

  s_active = true;
  s_lastActivityMs = millis();
  s_expectedSize = size;
  s_written = 0;
  strncpy(s_expectedSha256, sha256, sizeof(s_expectedSha256) - 1);
  s_expectedSha256[sizeof(s_expectedSha256) - 1] = '\0';
  strncpy(s_phase, "write", sizeof(s_phase));
  mbedtls_sha256_init(&s_sha);
  mbedtls_sha256_starts(&s_sha, 0);
  s_shaStarted = true;
  sendResponse(output, requestId, true, "begin");
  return true;
}

static bool handleChunk(const JsonDocument& request, Print& output) {
  const char* requestId = request["request_id"] | "";
  const size_t offset = request["offset"] | 0;
  const char* b64 = request["data_b64"] | "";

  if (!s_active) {
    sendResponse(output, requestId, false, "chunk", "ota_not_active");
    return true;
  }
  if (offset != s_written) {
    sendResponse(output, requestId, false, "chunk", "unexpected_offset");
    return true;
  }

  uint8_t decoded[OTA_CHUNK_BYTES];
  size_t decodedLen = 0;
  const int rc = mbedtls_base64_decode(
    decoded,
    sizeof(decoded),
    &decodedLen,
    reinterpret_cast<const unsigned char*>(b64),
    strlen(b64)
  );
  if (rc != 0 || decodedLen == 0 || s_written + decodedLen > s_expectedSize) {
    resetState("error");
    sendResponse(output, requestId, false, "chunk", "invalid_chunk");
    return true;
  }

  const size_t written = Update.write(decoded, decodedLen);
  if (written != decodedLen) {
    resetState("error");
    sendResponse(output, requestId, false, "chunk", Update.errorString());
    return true;
  }

  mbedtls_sha256_update(&s_sha, decoded, decodedLen);
  s_written += decodedLen;
  s_lastActivityMs = millis();
  sendResponse(output, requestId, true, "chunk");
  return true;
}

static bool handleEnd(const JsonDocument& request, Print& output) {
  const char* requestId = request["request_id"] | "";
  if (!s_active) {
    sendResponse(output, requestId, false, "end", "ota_not_active");
    return true;
  }
  if (s_written != s_expectedSize) {
    resetState("error");
    sendResponse(output, requestId, false, "end", "incomplete_image");
    return true;
  }

  uint8_t digest[32];
  char actual[65];
  mbedtls_sha256_finish(&s_sha, digest);
  mbedtls_sha256_free(&s_sha);
  s_shaStarted = false;
  bytesToHex(digest, sizeof(digest), actual, sizeof(actual));
  if (strcasecmp(actual, s_expectedSha256) != 0) {
    resetState("error");
    sendResponse(output, requestId, false, "verify", "sha256_mismatch");
    return true;
  }

  if (!Update.end(true)) {
    resetState("error");
    sendResponse(output, requestId, false, "end", Update.errorString());
    return true;
  }

  s_active = false;
  strncpy(s_phase, "reboot", sizeof(s_phase));
  s_rebootPending = true;
  s_rebootAtMs = millis() + OTA_REBOOT_DELAY_MS;
  sendResponse(output, requestId, true, "reboot");
  return true;
}

static bool handleAbort(const JsonDocument& request, Print& output) {
  const char* requestId = request["request_id"] | "";
  resetState("aborted");
  sendResponse(output, requestId, true, "aborted");
  return true;
}

bool otaHandleCommand(const JsonDocument& request, Print& output) {
  const char* type = request["type"] | "";
  if (strcmp(type, "ota_begin") == 0) {
    return handleBegin(request, output);
  }
  if (strcmp(type, "ota_chunk") == 0) {
    return handleChunk(request, output);
  }
  if (strcmp(type, "ota_end") == 0) {
    return handleEnd(request, output);
  }
  if (strcmp(type, "ota_abort") == 0) {
    return handleAbort(request, output);
  }
  return false;
}

bool otaIsActive() {
  return s_active;
}

uint8_t otaProgressPercent() {
  if (s_expectedSize == 0) {
    return 0;
  }
  size_t progress = (s_written * 100) / s_expectedSize;
  return progress > 100 ? 100 : static_cast<uint8_t>(progress);
}

const char* otaPhase() {
  return s_phase;
}

void otaLoop() {
  if (s_active && millis() - s_lastActivityMs >= OTA_IDLE_TIMEOUT_MS) {
    resetState("timeout");
  }
  if (s_rebootPending && (long)(millis() - s_rebootAtMs) >= 0) {
    ESP.restart();
  }
}
