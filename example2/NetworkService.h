#pragma once

#include <Arduino.h>

namespace NetworkService {
void begin();
void loop();
bool isProvisioning();
bool saveWifiCredentials(const String& ssid, const String& password,
                         String& error);
}
