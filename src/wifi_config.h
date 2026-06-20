#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include <Arduino.h>

enum WifiConfigMode : uint8_t {
  WIFI_CONFIG_STA_CONNECTING = 0,
  WIFI_CONFIG_STA_CONNECTED = 1,
  WIFI_CONFIG_AP = 2
};

void wifiConfigBegin();
void wifiConfigLoop();

WifiConfigMode wifiConfigMode();
bool wifiIsConnected();
bool wifiHasSavedCredentials();

String wifiDeviceCode();
String wifiApSsid();
String wifiApPassword();
String wifiCurrentSsid();
String wifiIpAddress();
String wifiModeLabel();

bool wifiSaveCredentials(const String& ssid, const String& password);
void wifiForgetCredentials();
void wifiStartReconnect();

#endif // WIFI_CONFIG_H
