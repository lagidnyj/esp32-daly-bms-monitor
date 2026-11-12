#define CONFIG_NIMBLE_CPP_LOG_LEVEL 4

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "BmsBle.h"
#include "BmsConfig.h"
#include "BmsOutput.h"
#include "CloudReporter.h"
#include "FileManager.h"
#include "NetworkService.h"

static uint32_t lastScanAt = 0;

static void rescanTargets() {
  scanAndUpdateTargets();
  lastScanAt = millis();
}

static void pollAll() {
  std::vector<Bms>& bms = bmsDevices();
  Serial.printf("\n=== poll cycle, heap %u ===\n", (unsigned)ESP.getFreeHeap());
  int okCount = 0;
  for (auto& device : bms) {
    if (pollOne(device)) {
      device.consecutiveFailures = 0;
      okCount++;
    } else if (device.consecutiveFailures < 3) {
      device.consecutiveFailures++;
    }
    delay(1000);
    NetworkService::loop();
    FileManager::loop();
  }
  const String telemetryJson = buildJson(bms);
  FileManager::setTelemetryJson(telemetryJson);
  if (PRINT_JSON) Serial.println(telemetryJson);
  if (okCount > 0) CloudReporter::maybeSendUpdate(telemetryJson);

  static int failStreak = 0;
  failStreak = (okCount == 0) ? failStreak + 1 : 0;
  if (failStreak >= 2) {
    Serial.println("[auto] nothing reachable twice in a row, rescanning");
    rescanTargets();
    failStreak = 0;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nDaly BLE poller, NimBLE-Arduino 2.x");
  NetworkService::begin();
  CloudReporter::begin();
  FileManager::begin();
  NimBLEDevice::init("esp32-daly");
  if (!NimBLEDevice::setMTU(BLE_MTU)) {
    Serial.printf("Could not set local BLE MTU to %u.\n", (unsigned)BLE_MTU);
  }
  doScan();
  buildTargets();
  lastScanAt = millis();
}

void loop() {
  NetworkService::loop();
  FileManager::loop();
  static uint32_t last = 0;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == 's') {
      rescanTargets();
    }
    if (ch == 'p') last = 0;
  }

  if (last == 0 || millis() - last >= POLL_INTERVAL_MS) {
    std::vector<Bms>& bms = bmsDevices();
    if (bms.empty()) {
      Serial.println("[cfg] no targets found; waiting for next discovery scan");
    } else {
      pollAll();
    }
    last = millis();
    if (last == 0) last = 1;
  }
  if (bmsDevices().size() < MAX_BMS &&
      millis() - lastScanAt >= RESCAN_INTERVAL_MS) {
    Serial.println("[auto] periodic target discovery scan");
    rescanTargets();
  }
  delay(20);
}
