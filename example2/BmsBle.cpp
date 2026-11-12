#include "BmsBle.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <ctype.h>
#include <string.h>
#include <string>
#include <vector>

#include "BmsConfig.h"
#include "DalyParser.h"

static std::vector<Found> seen;
static std::vector<Bms> bms;
static DalyParser parser;
static uint8_t notifyBuffer[512];
static volatile size_t notifyLength = 0;
static portMUX_TYPE notifyMux = portMUX_INITIALIZER_UNLOCKED;

std::vector<Bms>& bmsDevices() {
  return bms;
}

static std::string lc(std::string value) {
  for (auto& ch : value) ch = (char)tolower((unsigned char)ch);
  return value;
}

// Extract last 4 characters of MAC address as device ID (format: 1122)
static std::string extractIdFromMac(const std::string& mac) {
  if (mac.length() >= 5) {
    std::string id;
    id += mac[mac.length() - 5];
    id += mac[mac.length() - 4];
    id += mac[mac.length() - 2];
    id += mac[mac.length() - 1];
    return id;
  }
  return "";
}

static const char* hint(int rc) {
  switch (rc) {
    case 0:     return "no error code (timeout or refused)";
    case 2:     return "EALREADY: another scan/connect procedure is still active in the BLE host";
    case 15:    return "EBUSY: scan still running";
    case 13:    return "host timeout";
    case 0x23E: return "connection not established: BMS busy (phone app open?), out of range, or wrong addr type";
    case 0x208: return "link lost (supervision timeout)";
    case 0x213: return "remote side terminated (BMS dropped us)";
    case 0x216: return "terminated by local host";
    default:    return "see NimBLE BLE_HS_* / HCI error table";
  }
}

static void onNotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
  portENTER_CRITICAL(&notifyMux);
  if (notifyLength + length <= sizeof(notifyBuffer)) {
    memcpy(notifyBuffer + notifyLength, data, length);
    notifyLength += length;
  }
  portEXIT_CRITICAL(&notifyMux);
}

static void pump() {
  static uint8_t temp[512];
  size_t length;
  portENTER_CRITICAL(&notifyMux);
  length = notifyLength;
  memcpy(temp, notifyBuffer, length);
  notifyLength = 0;
  portEXIT_CRITICAL(&notifyMux);
  parser.feed(temp, length);
}

static void clearNotifyBuffer() {
  portENTER_CRITICAL(&notifyMux);
  notifyLength = 0;
  portEXIT_CRITICAL(&notifyMux);
}

static bool waitCmd(uint8_t command, uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    pump();
    if (parser.hasCommand(command)) return true;
    delay(10);
  }
  pump();
  return parser.hasCommand(command);
}

static bool sendCmd(NimBLERemoteCharacteristic* writeCharacteristic, uint8_t command) {
  uint8_t packet[13] = {0xA5, 0x40, command, 0x08};
  uint8_t sum = 0;
  for (int i = 0; i < 12; i++) sum += packet[i];
  packet[12] = sum;
  parser.clearCommand(command);
  if (VERBOSE) Serial.printf("  -> %02X\n", command);
  return writeCharacteristic->writeValue(packet, 13, writeCharacteristic->canWrite());
}

static void dumpGatt(NimBLEClient* client) {
  for (auto* service : client->getServices(true)) {
    Serial.printf("  svc %s\n", service->getUUID().toString().c_str());
    for (auto* characteristic : service->getCharacteristics(true)) {
      Serial.printf("    chr %s%s%s%s%s\n", characteristic->getUUID().toString().c_str(),
                    characteristic->canRead() ? " R" : "", characteristic->canWrite() ? " W" : "",
                    characteristic->canWriteNoResponse() ? " Wn" : "",
                    characteristic->canNotify() ? " N" : "");
    }
  }
}

bool pollOne(Bms& bms) {
  parser.begin(bms);
  bms.ok = false;
  bms.alertsValid = false;
  clearNotifyBuffer();

  Serial.printf("[%s] connecting (%s, addrType %d)...\n", bms.dev.mac.c_str(),
                bms.dev.name.c_str(), (int)bms.dev.addr.getType());
  Serial.printf("[%s] rssi at scan %d\n", bms.dev.mac.c_str(), bms.dev.rssi);

  NimBLEScan* scan = NimBLEDevice::getScan();
  if (scan->isScanning()) {
    Serial.println("  [ble] stopping active scan before connect");
    scan->stop();
    for (int i = 0; i < 50 && scan->isScanning(); i++) delay(20);
  }
  if (scan->isScanning()) {
    Serial.println("  [ble] scan is still active; skipping connect");
    return false;
  }

  NimBLEClient* client = NimBLEDevice::createClient();
  if (client == nullptr) {
    Serial.println("  [ble] could not create client");
    return false;
  }
  client->setConnectTimeout(CONNECT_MS);
  client->setConnectionParams(CONN_MIN, CONN_MAX, 0, 400);

  const uint32_t connectStart = millis();
  const bool connected = client->connect(bms.dev.addr, true, false, false);
  if (!connected) {
    const int error = client->getLastError();
    Serial.printf("[%s] CONNECT FAIL rc=%d (0x%X): %s  [%lu ms]\n", bms.dev.mac.c_str(),
                  error, error, hint(error), (unsigned long)(millis() - connectStart));
    NimBLEDevice::deleteClient(client);
    return false;
  }
  Serial.printf("[%s] connected in %lu ms\n", bms.dev.mac.c_str(),
                (unsigned long)(millis() - connectStart));
  if (!client->exchangeMTU()) {
    const int error = client->getLastError();
    Serial.printf("[%s] MTU exchange request failed rc=%d; continuing at MTU %u\n",
                  bms.dev.mac.c_str(), error, (unsigned)client->getMTU());
  } else {
    Serial.printf("[%s] MTU exchange request sent\n", bms.dev.mac.c_str());
  }

  bool good = false;
  NimBLERemoteService* service = client->getService(SVC_UUID);
  if (!service) {
    Serial.println("  service FFF0 not found, GATT dump:");
    dumpGatt(client);
  } else {
    NimBLERemoteCharacteristic* notifyCharacteristic = service->getCharacteristic(NTF_UUID);
    NimBLERemoteCharacteristic* writeCharacteristic = service->getCharacteristic(WR_UUID);
    if (!notifyCharacteristic || !writeCharacteristic) {
      Serial.println("  FFF1/FFF2 not found, GATT dump:");
      dumpGatt(client);
    } else if (!notifyCharacteristic->subscribe(true, onNotify)) {
      Serial.println("  subscribe FAILED");
    } else {
      delay(300);
      Serial.printf("[%s] current MTU %u\n", bms.dev.mac.c_str(),
                    (unsigned)client->getMTU());
      parser.resetResponses();

      static const uint8_t commands[] = {0x94, 0x90, 0x91, 0x92, 0x93, 0x98};
      for (uint8_t command : commands) {
        if (!sendCmd(writeCharacteristic, command)) {
          Serial.printf("  write %02X FAILED\n", command);
        }
        const uint32_t timeoutMs = command == 0x98 ? 500 : 800;
        if (!waitCmd(command, timeoutMs)) Serial.printf("  no reply to %02X\n", command);
        delay(80);
      }
      if (bms.alertsValid) {
        Serial.printf("[%s] alerts 0x98:", bms.dev.mac.c_str());
        for (uint8_t value : bms.alerts) Serial.printf(" %02X", value);
        Serial.println();
      } else {
        Serial.printf("[%s] alerts 0x98 unavailable\n", bms.dev.mac.c_str());
      }

      int neededFrames = ((bms.cells ? bms.cells : 16) + 2) / 3;
      if (neededFrames > 16) neededFrames = 16;
      sendCmd(writeCharacteristic, 0x95);
      const uint32_t frameStart = millis();
      while (millis() - frameStart < 1500 && parser.cellFrameCount() < neededFrames) {
        pump();
        delay(10);
      }
      if (parser.cellFrameCount() < neededFrames) {
        Serial.printf("  cells: got %d/%d frames\n", parser.cellFrameCount(), neededFrames);
      }

      good = parser.hasCommand(0x90);
    }
  }

  client->disconnect();
  delay(100);
  NimBLEDevice::deleteClient(client);

  bms.ok = good;
  if (good) {
    bms.lastOk = millis();
    Serial.printf("[%s] OK  %.1fV %.1fA SOC %.1f%%\n",
                  bms.dev.mac.c_str(), bms.v, bms.a, bms.soc);
  }
  return good;
}

void doScan() {
  seen.clear();
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);
  Serial.printf("[scan] %d ms...\n", SCAN_MS);
  NimBLEScanResults results = scan->getResults(SCAN_MS, false);
  for (int i = 0; i < (int)results.getCount(); i++) {
    const NimBLEAdvertisedDevice* device = results.getDevice(i);
    std::string mac = lc(device->getAddress().toString());
    std::string id = extractIdFromMac(mac);
    Found found(device->getAddress(), mac,
                device->haveName() ? device->getName() : std::string(), id, device->getRSSI(),
                device->isAdvertisingService(NimBLEUUID(SVC_UUID)));
    Serial.printf("  %s  rssi %4d  type %d  %-20s %s\n", found.mac.c_str(), found.rssi,
                  (int)device->getAddress().getType(), found.name.c_str(),
                  found.fff0 ? "[FFF0]" : "");
    seen.push_back(found);
  }
  scan->clearResults();
  Serial.printf("[scan] %u devices\n", (unsigned)seen.size());
}

void buildTargets() {
  bms.clear();
  if (BMS_DEVICES[0].mac != nullptr) {
    // Config-based mode: use configured devices
    for (int i = 0; BMS_DEVICES[i].mac != nullptr && bms.size() < MAX_BMS; i++) {
      const std::string cfgMac = lc(BMS_DEVICES[i].mac);
      const std::string cfgId = BMS_DEVICES[i].id ? std::string(BMS_DEVICES[i].id) : extractIdFromMac(cfgMac);
      const std::string cfgName = BMS_DEVICES[i].name ? std::string(BMS_DEVICES[i].name) : std::string();
      
      bool foundTarget = false;
      for (auto& device : seen) {
        if (device.mac == cfgMac) {
          device.id = cfgId;
          device.name = cfgName;
          bms.emplace_back(device);
          foundTarget = true;
          break;
        }
      }
      if (!foundTarget) {
        Serial.printf("[cfg] %s NOT seen in scan (busy with phone? out of range?) - trying anyway\n",
                      cfgMac.c_str());
        Found dev(NimBLEAddress(cfgMac, BLE_ADDR_PUBLIC), cfgMac, cfgName, cfgId, 0, false);
        bms.emplace_back(dev);
      }
    }
  } else {
    // Auto-scan mode: discover devices by name prefix or FFF0 service
    for (auto& device : seen) {
      if (bms.size() >= MAX_BMS) break;
      bool knownName = false;
      for (const char* prefix : BMS_NAME_PREFIXES) {
        if (device.name.rfind(prefix, 0) == 0) {
          knownName = true;
          break;
        }
      }
      if (knownName || device.fff0) bms.emplace_back(device);
    }
  }
  Serial.printf("[cfg] %u BMS target(s)\n", (unsigned)bms.size());
}

void scanAndUpdateTargets() {
  doScan();
  size_t added = 0;
  size_t updated = 0;

  if (BMS_DEVICES[0].mac != nullptr) {
    // Config-based mode
    for (int i = 0; BMS_DEVICES[i].mac != nullptr; i++) {
      const std::string cfgMac = lc(BMS_DEVICES[i].mac);
      const std::string cfgId = BMS_DEVICES[i].id ? std::string(BMS_DEVICES[i].id) : extractIdFromMac(cfgMac);
      const std::string cfgName = BMS_DEVICES[i].name ? std::string(BMS_DEVICES[i].name) : std::string();
      
      for (const auto& device : seen) {
        if (device.mac != cfgMac) continue;

        bool alreadyTracked = false;
        for (auto& target : bms) {
          if (target.dev.mac == cfgMac) {
            target.dev = device;
            target.dev.id = cfgId;
            target.dev.name = cfgName;
            alreadyTracked = true;
            ++updated;
            break;
          }
        }
        if (!alreadyTracked) {
          if (bms.size() < MAX_BMS) {
            Found dev = device;
            dev.id = cfgId;
            dev.name = cfgName;
            bms.emplace_back(dev);
            ++added;
          } else {
            Serial.printf("[cfg] target limit (%u) reached; ignoring %s\n",
                          static_cast<unsigned>(MAX_BMS), device.mac.c_str());
          }
        }
        break;
      }
    }
  } else {
    // Auto-scan mode
    for (const auto& device : seen) {
      bool knownName = false;
      for (const char* prefix : BMS_NAME_PREFIXES) {
        if (device.name.rfind(prefix, 0) == 0) {
          knownName = true;
          break;
        }
      }
      if (!knownName && !device.fff0) continue;

      bool alreadyTracked = false;
      for (auto& target : bms) {
        if (target.dev.mac == device.mac) {
          target.dev = device;
          alreadyTracked = true;
          ++updated;
          break;
        }
      }
      if (alreadyTracked) continue;
      if (bms.size() >= MAX_BMS) {
        Serial.printf("[cfg] target limit (%u) reached; ignoring %s\n",
                      static_cast<unsigned>(MAX_BMS), device.mac.c_str());
        continue;
      }
      bms.emplace_back(device);
      ++added;
      Serial.printf("[cfg] discovered new BMS %s (%s)\n", device.mac.c_str(),
                    device.name.c_str());
    }
  }

  Serial.printf("[cfg] rescan complete: %u target(s), %u added, %u refreshed\n",
                static_cast<unsigned>(bms.size()), static_cast<unsigned>(added),
                static_cast<unsigned>(updated));
}
