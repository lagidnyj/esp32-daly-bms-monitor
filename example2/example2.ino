#define CONFIG_NIMBLE_CPP_LOG_LEVEL 4

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "BmsBle.h"
#include "BmsConfig.h"
#include "BmsOutput.h"
#include "CloudReporter.h"
#include "FileManager.h"
#include "NetworkService.h"
#include "OledDisplay.h"

static uint32_t lastScanAt = 0;

static void rescanTargets() {
  scanAndUpdateTargets();
  lastScanAt = millis();
}

static void pollAll() {
  std::vector<Bms>& bms = bmsDevices();
  DEBUG_SERIAL.printf("\n=== poll cycle, heap %u ===\n", (unsigned)ESP.getFreeHeap());
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
    OledDisplay::loop(bms.size());
  }
  const String telemetryJson = buildJson(bms);
  FileManager::setTelemetryJson(telemetryJson);
  if (PRINT_JSON) DEBUG_SERIAL.println(telemetryJson);
  if (okCount > 0) CloudReporter::maybeSendUpdate(telemetryJson);

  static int failStreak = 0;
  failStreak = (okCount == 0) ? failStreak + 1 : 0;
  if (failStreak >= 2) {
    DEBUG_SERIAL.println("[auto] nothing reachable twice in a row, rescanning");
    rescanTargets();
    failStreak = 0;
  }
}

void setup() {
  DEBUG_SERIAL.begin(115200);
  delay(1000);
  DEBUG_SERIAL.println("\nDaly BLE poller, NimBLE-Arduino 2.x");
  NetworkService::begin();
  CloudReporter::begin();
  FileManager::begin();
  OledDisplay::begin();
  NimBLEDevice::init("esp32-daly");
  if (!NimBLEDevice::setMTU(BLE_MTU)) {
    DEBUG_SERIAL.printf("Could not set local BLE MTU to %u.\n", (unsigned)BLE_MTU);
  }
  doScan();
  buildTargets();
  lastScanAt = millis();
}

void loop() {
  NetworkService::loop();
  FileManager::loop();
  OledDisplay::loop(bmsDevices().size());
  static uint32_t last = 0;
  while (DEBUG_SERIAL.available()) {
    char ch = DEBUG_SERIAL.read();
    if (ch == 's') {
      rescanTargets();
    }
    if (ch == 'p') last = 0;
  }

  if (last == 0 || millis() - last >= POLL_INTERVAL_MS) {
    std::vector<Bms>& bms = bmsDevices();
    if (bms.empty()) {
      DEBUG_SERIAL.println("[cfg] no targets found; waiting for next discovery scan");
    } else {
      pollAll();
    }
    last = millis();
    if (last == 0) last = 1;
  }
  if (bmsDevices().size() < MAX_BMS &&
      millis() - lastScanAt >= RESCAN_INTERVAL_MS) {
    DEBUG_SERIAL.println("[auto] periodic target discovery scan");
    rescanTargets();
  }
  delay(20);
}
