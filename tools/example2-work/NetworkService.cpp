#include "NetworkService.h"

#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include <time.h>

#include "BmsConfig.h"

namespace {
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 15000;
constexpr uint32_t NTP_SYNC_TIMEOUT_MS = 15000;
constexpr time_t MIN_VALID_TIME = 1704067200;

uint32_t lastWifiAttemptAt = 0;
uint32_t ntpSyncStartedAt = 0;
bool ntpConfigured = false;
bool ntpTimeoutReported = false;
bool wifiConnectedReported = false;

bool hasWifiCredentials() {
  return WIFI_SSID[0] != '\0' && WIFI_PASSWORD[0] != '\0' &&
         strcmp(WIFI_SSID, "YOUR_WIFI_SSID") != 0 &&
         strcmp(WIFI_PASSWORD, "YOUR_WIFI_PASSWORD") != 0;
}

bool isClockSynchronized() {
  return time(nullptr) >= MIN_VALID_TIME;
}
}

namespace NetworkService {
void begin() {
  WiFi.mode(WIFI_STA);
  if (!hasWifiCredentials()) {
    Serial.println("Set Wi-Fi credentials in BmsConfig.h; cloud reporting is unavailable.");
    return;
  }
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttemptAt = millis();
  Serial.println("Connecting to Wi-Fi...");
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiConnectedReported) {
      Serial.printf("Wi-Fi connected: %s\n", WiFi.localIP().toString().c_str());
      wifiConnectedReported = true;
    }
    if (!ntpConfigured) {
      configTime(0, 0, "pool.ntp.org", "time.google.com");
      ntpSyncStartedAt = millis();
      ntpConfigured = true;
      Serial.println("Synchronizing system clock with NTP...");
    }
    if (ntpConfigured && !ntpTimeoutReported && !isClockSynchronized() &&
        millis() - ntpSyncStartedAt >= NTP_SYNC_TIMEOUT_MS) {
      Serial.println("NTP synchronization timed out; continuing without synchronized time.");
      ntpTimeoutReported = true;
    } else if (isClockSynchronized() && !ntpTimeoutReported) {
      Serial.println("System clock synchronized.");
      ntpTimeoutReported = true;
    }
    return;
  }

  wifiConnectedReported = false;
  if (!hasWifiCredentials()) return;
  if (millis() - lastWifiAttemptAt >= WIFI_RETRY_INTERVAL_MS) {
    Serial.println("Wi-Fi disconnected; retrying connection.");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastWifiAttemptAt = millis();
  }
}
}
