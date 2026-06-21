#ifndef PAIRING_H
#define PAIRING_H

#include <Arduino.h>

// Application-level pairing + authentication over BOTH transports (USB CDC and
// BLE GATT). One board binds to one companion (Mac) via a shared token; each
// transport authenticates independently with that token.

enum PairingPhase : uint8_t {
  PAIRING_UNPAIRED = 0,
  PAIRING_PAIRING,
  PAIRING_PAIRED,
};

enum PairTransport : uint8_t {
  PAIR_TRANSPORT_USB = 0,
  PAIR_TRANSPORT_BLE = 1,
};

// What the transport should do with an incoming line after pairing inspects it.
enum PairLineAction : uint8_t {
  PAIR_LINE_FORWARD = 0,  // pass to the normal agent-registry handler
  PAIR_LINE_CONSUMED,     // pairing handled (or rejected) it; do not forward
};

// Load any persisted token from NVS and set the initial phase.
void pairingBegin();

// Enter the pairing window (button-triggered on the board).
void pairingEnterMode();

// On-device confirm (middle key) / cancel (left key).
void pairingConfirm();
void pairingCancel();

// Drive the pairing timeout. Returns true if the phase changed (UI refresh).
bool pairingPoll(unsigned long now);

// Inspect an incoming JSON line from a transport. `out` is used to reply.
PairLineAction pairingHandleLine(const char* line, Print& out, PairTransport transport);

// Reset per-connection auth (and abort any in-progress pairing) on disconnect.
void pairingOnDisconnect(PairTransport transport);

PairingPhase pairingPhase();
const char* pairingCodeStr();

#endif // PAIRING_H
