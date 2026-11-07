#include "CloudReporter.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

namespace {
constexpr char CONFIG_PATH[] = "/config.json";
constexpr uint32_t HTTP_TIMEOUT_MS = 5000;
constexpr uint32_t DEFAULT_INTERVAL_SEC = 60;
constexpr uint32_t RETRY_INTERVAL_MS = 30000;

bool cloudEnabled = false;
String cloudUrl;
String cloudDeviceId;
String cloudDeviceToken;
uint32_t cloudIntervalMs = DEFAULT_INTERVAL_SEC * 1000UL;
uint32_t nextAttemptAt = 0;
bool attemptScheduled = false;

bool readConfigString(JsonObjectConst config, const char* key, String& value,
                      size_t maxLength) {
  JsonVariantConst field = config[key];
  if (!field.is<const char*>()) return false;
  value = field.as<const char*>();
  value.trim();
  return value.length() > 0 && value.length() <= maxLength;
}

void loadConfig() {
  File configFile = LittleFS.open(CONFIG_PATH, "r");
  if (!configFile) {
    Serial.println("Cloud config not found; cloud reporting is disabled.");
    return;
  }
  if (configFile.size() > 16384) {
    Serial.println("Cloud config is too large; cloud reporting is disabled.");
    configFile.close();
    return;
  }

  DynamicJsonDocument config(12288);
  const DeserializationError error = deserializeJson(config, configFile);
  configFile.close();
  if (error) {
    Serial.printf("Cloud config parse failed: %s; cloud reporting is disabled.\n",
                  error.c_str());
    return;
  }

  JsonObjectConst cloud = config.as<JsonObjectConst>();
  if (cloud.isNull()) {
    Serial.println("Cloud config must be a JSON object; cloud reporting is disabled.");
    return;
  }
  JsonVariantConst enabled = cloud["cloud_enabled"];
  if (!enabled.is<bool>() || !enabled.as<bool>()) {
    Serial.println("Cloud reporting is disabled by config.");
    return;
  }
  if (!readConfigString(cloud, "server_url", cloudUrl, 256) ||
      !cloudUrl.startsWith("https://")) {
    Serial.println("Cloud server_url must be an HTTPS URL; cloud reporting is disabled.");
    return;
  }
  if (!readConfigString(cloud, "device_id", cloudDeviceId, 128)) {
    Serial.println("Cloud device_id is missing or invalid; cloud reporting is disabled.");
    return;
  }
  if (!readConfigString(cloud, "device_token", cloudDeviceToken, 256)) {
    Serial.println("Cloud device_token is missing or invalid; cloud reporting is disabled.");
    return;
  }

  JsonVariantConst interval = cloud["cloud_interval_sec"];
  if (interval.is<uint32_t>()) {
    const uint32_t intervalSec = interval.as<uint32_t>();
    if (intervalSec >= 60 && intervalSec <= 180) {
      cloudIntervalMs = intervalSec * 1000UL;
    } else {
      Serial.println("Cloud interval must be 60-180 seconds; using 60 seconds.");
    }
  } else if (!interval.isNull()) {
    Serial.println("Cloud interval is invalid; using 60 seconds.");
  }

  cloudEnabled = true;
  Serial.printf("Cloud reporting enabled for device %s at %s (interval %lu sec)\n",
                cloudDeviceId.c_str(), cloudUrl.c_str(),
                static_cast<unsigned long>(cloudIntervalMs / 1000));
}

bool sendUpdate(const String& telemetryJson) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Cloud update skipped: Wi-Fi is disconnected.");
    return false;
  }
  if (telemetryJson.length() == 0) {
    Serial.println("Cloud update failed: telemetry JSON is empty.");
    return false;
  }

  DynamicJsonDocument request(1024);
  request["device_id"] = cloudDeviceId;
  request["device_token"] = cloudDeviceToken;
  request["payload"] = serialized(telemetryJson.c_str());
  if (request.overflowed()) {
    Serial.println("Cloud update failed: request JSON capacity exceeded.");
    return false;
  }

  String body;
  serializeJson(request, body);
  if (body.length() == 0) {
    Serial.println("Cloud update failed: unable to serialize request JSON.");
    return false;
  }

  WiFiClientSecure tlsClient;
  tlsClient.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(tlsClient, cloudUrl)) {
    Serial.println("Cloud update failed: unable to initialize HTTPS request.");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  const int status = http.POST(body);
  http.end();
  if (status != HTTP_CODE_OK) {
    if (status < 0) {
      Serial.printf("POST failed: %s\n", HTTPClient::errorToString(status).c_str());
    } else {
      Serial.printf("POST cloud: HTTP %d\n", status);
    }
    return false;
  }

  Serial.println("POST cloud: 200 OK");
  return true;
}
}

namespace CloudReporter {
void begin() {
//
//    Serial.println("LittleFS mount failed; cloud reporting is disabled.");
//    return;
//  }
if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed, formatting...");
    if (LittleFS.format()) {
        Serial.println("LittleFS formatted successfully.");
        if (LittleFS.begin()) {
            Serial.println("LittleFS mounted successfully.");
        }
    }
}
  loadConfig();
}

void maybeSendUpdate(const String& telemetryJson) {
  if (!cloudEnabled) return;

  const uint32_t now = millis();
  if (attemptScheduled && static_cast<int32_t>(now - nextAttemptAt) < 0) return;

  const bool succeeded = sendUpdate(telemetryJson);
  const uint32_t delayMs = succeeded ? cloudIntervalMs : RETRY_INTERVAL_MS;
  nextAttemptAt = millis() + delayMs;
  attemptScheduled = true;
}
}
