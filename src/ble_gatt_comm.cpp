#include "ble_gatt_comm.h"
#include "config.h"
#include "pairing.h"
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

static BLEUUID SERVICE_UUID("6b69726f-6b62-0001-8000-00805f9b34fb");
static BLEUUID RX_UUID("6b69726f-6b62-0002-8000-00805f9b34fb");
static BLEUUID TX_UUID("6b69726f-6b62-0003-8000-00805f9b34fb");

static BLEServer* s_server = nullptr;
static BLECharacteristic* s_tx = nullptr;
static bool s_connected = false;
static String s_rxBuffer;
static String s_boardId;
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
    s_tx->setValue(const_cast<uint8_t*>(data + offset), chunk);
    s_tx->notify();
    delay(2);
    offset += chunk;
  }
}

class BleNotifyPrint : public Print {
public:
  // Buffer outgoing bytes and flush one whole line per notification. ArduinoJson
  // serializes byte-by-byte through Print, and sending one BLE notification per
  // byte floods the link (and the companion log) and lets concurrent senders
  // interleave their bytes. Buffering until '\n' makes each JSON line a single
  // notification, matching bleGattCommSendLine().
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

class KiroServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    s_connected = true;
    Serial.println("[BLE-GATT] companion connected");
  }

  void onDisconnect(BLEServer* server) override {
    s_connected = false;
    Serial.println("[BLE-GATT] companion disconnected");
    pairingOnDisconnect(PAIR_TRANSPORT_BLE);
    server->startAdvertising();
  }
};

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
        s_handler(s_rxBuffer.c_str(), s_slots, *s_selectedAgent, s_blePrint);
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

class KiroRxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    String value = characteristic->getValue();
    for (size_t i = 0; i < value.length(); i++) {
      handleRxByte(value[i]);
    }
  }
};

void bleGattCommBegin(BleGattLineHandler handler) {
  s_handler = handler;
  BLEDevice::init(BLE_DEVICE_NAME);
  s_boardId = BLEDevice::getAddress().toString().c_str();
  BLEServer* server = BLEDevice::createServer();
  s_server = server;
  server->setCallbacks(new KiroServerCallbacks());

  BLEService* service = server->createService(SERVICE_UUID);
  s_tx = service->createCharacteristic(
    TX_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  s_tx->addDescriptor(new BLE2902());

  BLECharacteristic* rx = service->createCharacteristic(
    RX_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  rx->setCallbacks(new KiroRxCallbacks());

  service->start();
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->start();
  Serial.println("[BLE-GATT] advertising companion service");
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
  return s_tx != nullptr;
}

const char* bleGattCommBoardId() {
  return s_boardId.c_str();
}
