#include "ble_gatt_comm.h"
#include "config.h"
#include "pairing.h"
#include <NimBLEDevice.h>

// Custom Kiro companion service, now on NimBLE (so BLE HID can later coexist on
// the same host stack). Public API is unchanged.

static const char* SERVICE_UUID = "6b69726f-6b62-0001-8000-00805f9b34fb";
static const char* RX_UUID = "6b69726f-6b62-0002-8000-00805f9b34fb";
static const char* TX_UUID = "6b69726f-6b62-0003-8000-00805f9b34fb";

static NimBLEServer* s_server = nullptr;
static NimBLECharacteristic* s_tx = nullptr;
static String s_rxBuffer;
static String s_boardId;
static char s_deviceName[16] = "Kiro KB";
static BleGattLineHandler s_handler = nullptr;
static AgentSlot* s_slots = nullptr;
static uint8_t* s_selectedAgent = nullptr;

static void notifyBytes(const uint8_t* data, size_t size) {
  if (!s_tx) {
    Serial.println("[BLE-GATT] notify skipped: tx not ready");
    return;
  }
  size_t offset = 0;
  while (offset < size) {
    size_t chunk = min<size_t>(180, size - offset);
    s_tx->setValue(data + offset, chunk);
    s_tx->notify();
    delay(2);
    offset += chunk;
  }
}

class BleNotifyPrint : public Print {
public:
  // Buffer outgoing bytes and flush one whole line per notification. ArduinoJson
  // serializes byte-by-byte through Print; one notification per byte floods the
  // link and lets concurrent senders interleave. Buffering until '\n' makes each
  // JSON line a single notification, matching bleGattCommSendLine().
  size_t write(uint8_t b) override {
    appendByte(b);
    return 1;
  }

  size_t write(const uint8_t* buffer, size_t size) override {
    for (size_t i = 0; i < size; i++) {
      appendByte(buffer[i]);
    }
    return size;
  }

private:
  String m_lineBuffer;

  void appendByte(uint8_t b) {
    if (b == '\r') {
      return;
    }
    m_lineBuffer += static_cast<char>(b);
    if (b == '\n' || m_lineBuffer.length() >= 512) {
      notifyBytes(reinterpret_cast<const uint8_t*>(m_lineBuffer.c_str()),
                  m_lineBuffer.length());
      m_lineBuffer = "";
    }
  }
};

static BleNotifyPrint s_blePrint;

static void handleRxByte(char ch) {
  if (ch == '\r') {
    return;
  }
  if (ch == '\n') {
    if (s_rxBuffer.length() > 0 && s_handler && s_slots && s_selectedAgent) {
      Serial.printf("[BLE-GATT] rx line: %s\n", s_rxBuffer.c_str());
      // Pairing/auth gate runs first; only forward non-privileged or
      // authenticated lines to the normal handler.
      if (pairingHandleLine(s_rxBuffer.c_str(), s_blePrint, PAIR_TRANSPORT_BLE) == PAIR_LINE_FORWARD) {
        s_handler(s_rxBuffer.c_str(), s_slots, *s_selectedAgent, s_blePrint, CHANNEL_BLE);
      }
    }
    s_rxBuffer = "";
    return;
  }
  if (s_rxBuffer.length() < 1024) {
    s_rxBuffer += ch;
  } else {
    s_rxBuffer = "";
  }
}

class KiroServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo&) override {
    Serial.println("[BLE-GATT] companion connected");
  }

  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    Serial.println("[BLE-GATT] companion disconnected");
    pairingOnDisconnect(PAIR_TRANSPORT_BLE);
    // Keep the board discoverable for the next central (and, later, alongside a
    // system HID connection).
    NimBLEDevice::startAdvertising();
  }
};

class KiroRxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo&) override {
    NimBLEAttValue value = characteristic->getValue();
    for (size_t i = 0; i < value.length(); i++) {
      handleRxByte(static_cast<char>(value[i]));
    }
  }
};

void bleGattCommBegin(BleGattLineHandler handler) {
  s_handler = handler;

  // NimBLEDevice::init() is idempotent and createServer() is a singleton, so
  // it is safe to call these even if the HID library already initialised them.
  // This lets us add the Kiro companion service alongside the HID service on
  // the same server.
  if (!NimBLEDevice::isInitialized()) {
    NimBLEDevice::init(BLE_DEVICE_NAME);
  }
  NimBLEDevice::setMTU(185);
  s_boardId = NimBLEDevice::getAddress().toString().c_str();

  // Build a unique device name from MAC suffix (e.g. "Kiro-A3F2").
  String addr = NimBLEDevice::getAddress().toString().c_str();
  // addr format: "xx:xx:xx:xx:xx:xx" — use last 2 octets
  size_t alen = addr.length();
  if (alen >= 5) {
    char h1 = addr[alen - 5];
    char h2 = addr[alen - 4];
    char h3 = addr[alen - 2];
    char h4 = addr[alen - 1];
    snprintf(s_deviceName, sizeof(s_deviceName), "Kiro-%c%c%c%c", h1, h2, h3, h4);
  }
  NimBLEDevice::setDeviceName(s_deviceName);

  s_server = NimBLEDevice::createServer();
  s_server->setCallbacks(new KiroServerCallbacks(), true); // true = append, don't replace
  s_server->advertiseOnDisconnect(true);

  NimBLEService* service = s_server->createService(SERVICE_UUID);
  s_tx = service->createCharacteristic(TX_UUID, NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* rx = service->createCharacteristic(
    RX_UUID,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  rx->setCallbacks(new KiroRxCallbacks());
  service->start();

  // Advertising is managed by bleHidSetDiscoverable() which always includes our
  // custom service UUID. Start initial advertising here (companion-only, no HID).
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->enableScanResponse(true);
  NimBLEDevice::startAdvertising();
  Serial.println("[BLE-GATT] companion service registered (NimBLE)");
}

void bleGattCommSetRegistry(AgentSlot* slots, uint8_t* selectedAgent) {
  s_slots = slots;
  s_selectedAgent = selectedAgent;
}

void bleGattCommSendLine(const char* line) {
  if (!line) {
    return;
  }
  Serial.printf("[BLE-GATT] notify line: %s\n", line);
  notifyBytes(reinterpret_cast<const uint8_t*>(line), strlen(line));
  const uint8_t newline = '\n';
  notifyBytes(&newline, 1);
}

bool bleGattCommConnected() {
  return s_server && s_server->getConnectedCount() > 0;
}

const char* bleGattCommBoardId() {
  return s_boardId.c_str();
}

const char* bleGattCommDeviceName() {
  return s_deviceName;
}
