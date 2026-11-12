#include "NetworkService.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "BmsConfig.h"

namespace {
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 15000;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 30000;
constexpr uint32_t AP_START_RETRY_INTERVAL_MS = 5000;
constexpr uint32_t PROVISIONING_TIMEOUT_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t RESTART_DELAY_MS = 1500;
constexpr uint32_t NTP_SYNC_TIMEOUT_MS = 15000;
constexpr time_t MIN_VALID_TIME = 1704067200;
constexpr char PREFERENCES_NAMESPACE[] = "bms-wifi";
constexpr char CREDENTIALS_KEY[] = "credentials";

struct StoredWifiCredentials {
  char ssid[33];
  char password[64];
};

Preferences preferences;
StoredWifiCredentials storedCredentials = {};
String wifiSsid;
String wifiPassword;
uint32_t lastWifiAttemptAt = 0;
uint32_t lastApStartAttemptAt = 0;
uint32_t wifiConnectStartedAt = 0;
uint32_t provisioningStartedAt = 0;
uint32_t restartAt = 0;
uint32_t ntpSyncStartedAt = 0;
bool preferencesReady = false;
bool provisioningActive = false;
bool apStartAttempted = false;
bool stationConnectedOnce = false;
bool restartScheduled = false;
bool ntpConfigured = false;
bool ntpTimeoutReported = false;
bool wifiConnectedReported = false;

bool isConfiguredBuildCredential() {
  return WIFI_SSID[0] != '\0' && WIFI_PASSWORD[0] != '\0' &&
         strcmp(WIFI_SSID, "YOUR_WIFI_SSID") != 0 &&
         strcmp(WIFI_PASSWORD, "YOUR_WIFI_PASSWORD") != 0;
}

bool hasWifiCredentials() {
  return wifiSsid.length() > 0;
}

void startWifiConnection() {
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  lastWifiAttemptAt = millis();
  Serial.println("Connecting to Wi-Fi...");
}

void startProvisioningAP() {
  if (provisioningActive ||
      (apStartAttempted &&
       millis() - lastApStartAttemptAt < AP_START_RETRY_INTERVAL_MS)) {
    return;
  }
  apStartAttempted = true;
  lastApStartAttemptAt = millis();

  Serial.println("Starting Wi-Fi setup AP...");
  const wifi_mode_t mode = hasWifiCredentials() ? WIFI_AP_STA : WIFI_AP;
  if (!WiFi.mode(mode)) {
    Serial.println("Could not enable Wi-Fi AP mode; will retry.");
    return;
  }
  const uint32_t chipSuffix =
      static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFF);
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06lX",
           static_cast<unsigned long>(chipSuffix));
  const String apSsid = String("Daly-BMS-") + suffix;
  const String apPassword = String("Bms-") + suffix;
  Serial.printf("Starting setup network %s...\n", apSsid.c_str());
  if (!WiFi.softAP(apSsid.c_str(), apPassword.c_str())) {
    Serial.println("Could not start Wi-Fi setup access point; will retry.");
    return;
  }

  provisioningActive = true;
  provisioningStartedAt = millis();
  Serial.printf("Wi-Fi setup AP: %s, password: %s, page: http://%s/wifi\n",
                apSsid.c_str(), apPassword.c_str(),
                WiFi.softAPIP().toString().c_str());
  Serial.printf("The setup AP will restart the device in %u seconds.\n",
                static_cast<unsigned>(PROVISIONING_TIMEOUT_MS / 1000));
}

bool isValidWifiPassword(const String& password) {
  return password.length() == 0 ||
         (password.length() >= 8 && password.length() <= 63);
}

bool isClockSynchronized() {
  return time(nullptr) >= MIN_VALID_TIME;
}
}

namespace NetworkService {
void begin() {
  preferencesReady = preferences.begin(PREFERENCES_NAMESPACE, false);
  if (!preferencesReady) {
    Serial.println("Could not open Wi-Fi credential storage.");
  } else {
    const size_t storedLength = preferences.getBytesLength(CREDENTIALS_KEY);
    if (storedLength == sizeof(storedCredentials)) {
      if (preferences.getBytes(CREDENTIALS_KEY, &storedCredentials,
                               sizeof(storedCredentials)) ==
              sizeof(storedCredentials) &&
          memchr(storedCredentials.ssid, '\0', sizeof(storedCredentials.ssid)) &&
          memchr(storedCredentials.password, '\0',
                 sizeof(storedCredentials.password)) &&
          storedCredentials.ssid[0] != '\0') {
        wifiSsid = storedCredentials.ssid;
        wifiPassword = storedCredentials.password;
      } else {
        Serial.println("Stored Wi-Fi credentials are invalid; setup is required.");
      }
    } else if (storedLength != 0) {
      Serial.println("Stored Wi-Fi credentials are invalid; setup is required.");
    }
  }

  if (!hasWifiCredentials() && isConfiguredBuildCredential()) {
    wifiSsid = WIFI_SSID;
    wifiPassword = WIFI_PASSWORD;
  }

  if (!hasWifiCredentials()) {
    Serial.println("No Wi-Fi credentials are configured.");
    startProvisioningAP();
    return;
  }

  WiFi.mode(WIFI_STA);
  startWifiConnection();
  wifiConnectStartedAt = millis();
}

void loop() {
  if (restartScheduled &&
      static_cast<int32_t>(millis() - restartAt) >= 0) {
    ESP.restart();
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    stationConnectedOnce = true;
    if (provisioningActive) {
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      provisioningActive = false;
      Serial.println("Wi-Fi connected; setup access point stopped.");
    }
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
  if (!hasWifiCredentials()) {
    if (!provisioningActive) startProvisioningAP();
    if (provisioningActive &&
        millis() - provisioningStartedAt >= PROVISIONING_TIMEOUT_MS) {
      Serial.println("Wi-Fi setup timed out; restarting device.");
      ESP.restart();
    }
    return;
  }

  if (millis() - lastWifiAttemptAt >= WIFI_RETRY_INTERVAL_MS) {
    Serial.println("Wi-Fi disconnected; retrying connection.");
    WiFi.disconnect(false, false);
    startWifiConnection();
  }

  if (!stationConnectedOnce &&
      millis() - wifiConnectStartedAt >= WIFI_CONNECT_TIMEOUT_MS) {
    startProvisioningAP();
    if (provisioningActive &&
        millis() - provisioningStartedAt >= PROVISIONING_TIMEOUT_MS) {
      Serial.println("Wi-Fi setup timed out; restarting device.");
      ESP.restart();
    }
  }
}

bool isProvisioning() {
  return provisioningActive;
}

bool saveWifiCredentials(const String& ssid, const String& password,
                         String& error) {
  if (ssid.length() == 0 || ssid.length() > 32) {
    error = "SSID must contain 1 to 32 bytes.";
    return false;
  }
  if (!isValidWifiPassword(password)) {
    error = "Password must be empty for an open network or 8 to 63 characters.";
    return false;
  }
  if (!preferencesReady) {
    error = "Wi-Fi credential storage is unavailable.";
    return false;
  }

  StoredWifiCredentials credentials = {};
  memcpy(credentials.ssid, ssid.c_str(), ssid.length());
  memcpy(credentials.password, password.c_str(), password.length());
  if (preferences.putBytes(CREDENTIALS_KEY, &credentials,
                           sizeof(credentials)) != sizeof(credentials)) {
    error = "Could not save Wi-Fi credentials.";
    return false;
  }

  restartScheduled = true;
  restartAt = millis() + RESTART_DELAY_MS;
  return true;
}
}
