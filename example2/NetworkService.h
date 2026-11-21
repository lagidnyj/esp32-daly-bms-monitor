#pragma once

#include <Arduino.h>

namespace NetworkService {
void begin();
void loop();
bool isProvisioning();
bool isConnecting();
const String& provisioningSsid();
const String& provisioningPassword();
bool saveWifiCredentials(const String& ssid, const String& password,
                         String& error);
}
