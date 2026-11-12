#pragma once

#include <NimBLEDevice.h>
#include <stdint.h>
#include <string>

struct Found {
  NimBLEAddress addr;
  std::string mac;
  std::string name;
  std::string id;    // Device ID (last 4 chars of MAC or from config)
  int rssi;
  bool fff0;

  Found(const NimBLEAddress& address, const std::string& deviceMac,
        const std::string& deviceName, const std::string& deviceId, int signal, bool advertisesFff0)
    : addr(address), mac(deviceMac), name(deviceName), id(deviceId), rssi(signal), fff0(advertisesFff0) {}
};

struct Bms {
  Found dev;
  bool ok = false;
  uint32_t lastOk = 0;
  uint8_t consecutiveFailures = 0;
  float v = 0, a = 0, soc = 0;
  int maxMv = 0, minMv = 0, tMax = 0, tMin = 0;
  uint8_t state = 0, chg = 0, dis = 0, cells = 0, temps = 0;
  uint16_t cycles = 0;
  uint32_t remMah = 0;
  uint16_t mv[48] = {0};
  bool alertsValid = false;
  uint8_t alerts[8] = {};

  explicit Bms(const Found& device) : dev(device) {}
};
