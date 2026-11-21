#include "BmsBle.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <ctype.h>
#include <string.h>
#include <string>
#include <vector>

#include "BmsConfig.h"
#include "BmsModbusControl.h"
#include "DalyParser.h"

static std::vector<Found> seen;
static std::vector<Bms> bms;
static DalyParser parser;
static uint8_t notifyBuffer[512];
static volatile size_t notifyLength = 0;
static portMUX_TYPE notifyMux = portMUX_INITIALIZER_UNLOCKED;

static std::string lc(std::string value);

std::vector<Bms>& bmsDevices() {
  return bms;
}

bool hasBmsTarget(const char* mac) {
  if (mac == nullptr) return false;
  const std::string targetMac = lc(mac);
  for (const Bms& device : bms) {
    if (lc(device.dev.mac) == targetMac) return true;
  }
  return false;
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
  for (size_t offset = 0; offset < length; offset += 128) {
    const size_t remaining = length - offset;
    parser.feed(temp + offset, remaining < 128 ? remaining : 128);
  }
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
  if (VERBOSE) DEBUG_SERIAL.printf("  -> %02X\n", command);
  return writeCharacteristic->writeValue(packet, 13, writeCharacteristic->canWrite());
}

static size_t takeNotifyData(uint8_t* destination, size_t capacity) {
  portENTER_CRITICAL(&notifyMux);
  size_t length = notifyLength;
  if (length > capacity) length = capacity;
  memcpy(destination, notifyBuffer, length);
  if (length < notifyLength) {
    memmove(notifyBuffer, notifyBuffer + length, notifyLength - length);
  }
  notifyLength -= length;
  portEXIT_CRITICAL(&notifyMux);
  return length;
}

static bool waitModbusResponse(uint8_t function, size_t expectedLength,
                               uint32_t timeoutMs, uint8_t* response,
                               size_t& responseLength) {
  uint8_t accumulated[64] = {};
  size_t accumulatedLength = 0;
  uint8_t incoming[512];
  const uint32_t startedAt = millis();

  while (millis() - startedAt < timeoutMs) {
    const size_t incomingLength = takeNotifyData(incoming, sizeof(incoming));
    if (incomingLength > 0) {
      for (size_t i = 0; i < incomingLength; i++) {
        if (accumulatedLength == sizeof(accumulated)) {
          memmove(accumulated, accumulated + 1, sizeof(accumulated) - 1);
          accumulatedLength--;
        }
        accumulated[accumulatedLength++] = incoming[i];

        for (size_t start = 0; start + 2 <= accumulatedLength; start++) {
          if (accumulated[start] != DALY_MODBUS_BLE_RESPONSE_ADDRESS) continue;
          const bool exception = accumulated[start + 1] == (function | 0x80);
          if (accumulated[start + 1] != function && !exception) continue;
          const size_t candidateLength = exception ? 5 : expectedLength;
          if (start + candidateLength > accumulatedLength ||
              !BmsModbusControl::hasValidCrc(accumulated + start, candidateLength)) {
            continue;
          }
          memcpy(response, accumulated + start, candidateLength);
          responseLength = candidateLength;
          return true;
        }
      }
    }
    delay(10);
  }
  return false;
}

static void logModbusFrame(const char* targetMac, const char* direction,
                           const uint8_t* frame, size_t length) {
  if (!VERBOSE) return;
  DEBUG_SERIAL.printf("[%s] Modbus %s", targetMac, direction);
  for (size_t i = 0; i < length; i++) DEBUG_SERIAL.printf(" %02X", frame[i]);
  DEBUG_SERIAL.println();
}

static void processQueuedModbusWrites(
    const char* targetMac, NimBLERemoteCharacteristic* writeCharacteristic) {
  BmsModbusControl::Command command = {};
  while (BmsModbusControl::takeNextFor(targetMac, command)) {
    uint8_t request[BmsModbusControl::WRITE_UINT32_FRAME_LENGTH];
    uint8_t response[9] = {};
    size_t responseLength = 0;
    const bool isCapacity = command.reg == 0x0109;
    const uint8_t writeFunction = isCapacity ? 0x10 : 0x06;
    const size_t requestLength = isCapacity ? BmsModbusControl::WRITE_UINT32_FRAME_LENGTH
                                            : BmsModbusControl::FRAME_LENGTH;
    if (isCapacity) {
      BmsModbusControl::buildWriteUint32(
          DALY_MODBUS_BLE_REQUEST_ADDRESS, command.reg, command.value, request);
    } else {
      BmsModbusControl::buildWriteSingleRegister(
          DALY_MODBUS_BLE_REQUEST_ADDRESS, command.reg,
          static_cast<uint16_t>(command.value), request);
    }

    logModbusFrame(targetMac, "->", request, requestLength);
    clearNotifyBuffer();
    if (!writeCharacteristic->writeValue(request, requestLength,
                                         writeCharacteristic->canWrite())) {
      DEBUG_SERIAL.printf("[%s] Modbus 0x%02X BLE write failed (job %lu)\n", targetMac, writeFunction,
                    static_cast<unsigned long>(command.id));
      BmsModbusControl::setState(command.id, BmsModbusControl::CommandState::Failed);
      continue;
    }

    if (!waitModbusResponse(writeFunction, BmsModbusControl::FRAME_LENGTH, 1500, response,
                            responseLength)) {
      DEBUG_SERIAL.printf("[%s] Modbus 0x%02X response timeout (job %lu)\n", targetMac, writeFunction,
                    static_cast<unsigned long>(command.id));
      BmsModbusControl::setState(command.id, BmsModbusControl::CommandState::Failed);
      continue;
    }
    logModbusFrame(targetMac, "<-", response, responseLength);
    const bool matched = isCapacity
        ? BmsModbusControl::matchesWriteUint32Response(
              request, response, responseLength, DALY_MODBUS_BLE_RESPONSE_ADDRESS)
        : BmsModbusControl::matchesWriteResponse(
              request, response, responseLength, DALY_MODBUS_BLE_RESPONSE_ADDRESS);
    if (!matched) {
      DEBUG_SERIAL.printf("[%s] Modbus 0x%02X response mismatch (job %lu)\n", targetMac, writeFunction,
                    static_cast<unsigned long>(command.id));
      BmsModbusControl::setState(command.id, BmsModbusControl::CommandState::Failed);
      continue;
    }
    BmsModbusControl::setState(command.id,
                               BmsModbusControl::CommandState::Acknowledged);

    if (!command.verifyResult) continue;

    delay(500);
    uint8_t readRequest[BmsModbusControl::FRAME_LENGTH];
    const bool isMosfet = command.reg == 0x0121 || command.reg == 0x0122;
    const uint16_t readReg = isMosfet ? 0x0052 : command.reg;
    const uint8_t readCount = (isMosfet || isCapacity) ? 2 : 1;
    const size_t expectedReadLength = 5 + readCount * 2;
    BmsModbusControl::buildReadHoldingRegisters(
      DALY_MODBUS_BLE_REQUEST_ADDRESS, readReg, readCount, readRequest);
    clearNotifyBuffer();
    logModbusFrame(targetMac, "->", readRequest, sizeof(readRequest));
    if (!writeCharacteristic->writeValue(readRequest, sizeof(readRequest),
                                         writeCharacteristic->canWrite())) {
      DEBUG_SERIAL.printf("[%s] Modbus readback BLE write failed (job %lu)\n", targetMac,
                    static_cast<unsigned long>(command.id));
      BmsModbusControl::setState(command.id, BmsModbusControl::CommandState::Failed);
      continue;
    }
    if (!waitModbusResponse(0x03, expectedReadLength, 1500, response,
                            responseLength)) {
      DEBUG_SERIAL.printf("[%s] Modbus readback timeout (job %lu)\n", targetMac,
                    static_cast<unsigned long>(command.id));
      BmsModbusControl::setState(command.id, BmsModbusControl::CommandState::Failed);
      continue;
    }
    logModbusFrame(targetMac, "<-", response, responseLength);
        uint16_t readValues[2] = {};
        if (!BmsModbusControl::parseReadHoldingResponse(
          response, responseLength, DALY_MODBUS_BLE_RESPONSE_ADDRESS,
          readCount, readValues)) {
      DEBUG_SERIAL.printf("[%s] Modbus readback frame invalid (job %lu)\n", targetMac,
                    static_cast<unsigned long>(command.id));
      BmsModbusControl::setState(command.id, BmsModbusControl::CommandState::Failed);
      continue;
    }

    const uint32_t actualValue = isCapacity
      ? (static_cast<uint32_t>(readValues[0]) << 16) | readValues[1]
      : readValues[command.reg == 0x0122 ? 1 : 0];
    const BmsModbusControl::CommandState state = actualValue == command.value
        ? BmsModbusControl::CommandState::Verified
        : BmsModbusControl::CommandState::Mismatch;
    BmsModbusControl::setState(command.id, state, true, actualValue);
    if (VERBOSE) {
      DEBUG_SERIAL.printf("[%s] Modbus readback reg %04X requested %lu actual %lu (%s)\n",
            targetMac, command.reg, static_cast<unsigned long>(command.value),
            static_cast<unsigned long>(actualValue),
                    BmsModbusControl::stateName(state));
    }
  }
}

static void dumpGatt(NimBLEClient* client) {
  for (auto* service : client->getServices(true)) {
    DEBUG_SERIAL.printf("  svc %s\n", service->getUUID().toString().c_str());
    for (auto* characteristic : service->getCharacteristics(true)) {
      DEBUG_SERIAL.printf("    chr %s%s%s%s%s\n", characteristic->getUUID().toString().c_str(),
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

  DEBUG_SERIAL.printf("[%s] connecting (%s, addrType %d)...\n", bms.dev.mac.c_str(),
                bms.dev.name.c_str(), (int)bms.dev.addr.getType());
  DEBUG_SERIAL.printf("[%s] rssi at scan %d\n", bms.dev.mac.c_str(), bms.dev.rssi);

  NimBLEScan* scan = NimBLEDevice::getScan();
  if (scan->isScanning()) {
    DEBUG_SERIAL.println("  [ble] stopping active scan before connect");
    scan->stop();
    for (int i = 0; i < 50 && scan->isScanning(); i++) delay(20);
  }
  if (scan->isScanning()) {
    DEBUG_SERIAL.println("  [ble] scan is still active; skipping connect");
    return false;
  }

  NimBLEClient* client = NimBLEDevice::createClient();
  if (client == nullptr) {
    DEBUG_SERIAL.println("  [ble] could not create client");
    return false;
  }
  client->setConnectTimeout(CONNECT_MS);
  client->setConnectionParams(CONN_MIN, CONN_MAX, 0, 400);

  const uint32_t connectStart = millis();
  const bool connected = client->connect(bms.dev.addr, true, false, false);
  if (!connected) {
    const int error = client->getLastError();
    DEBUG_SERIAL.printf("[%s] CONNECT FAIL rc=%d (0x%X): %s  [%lu ms]\n", bms.dev.mac.c_str(),
                  error, error, hint(error), (unsigned long)(millis() - connectStart));
    NimBLEDevice::deleteClient(client);
    return false;
  }
  DEBUG_SERIAL.printf("[%s] connected in %lu ms\n", bms.dev.mac.c_str(),
                (unsigned long)(millis() - connectStart));
  if (!client->exchangeMTU()) {
    const int error = client->getLastError();
    DEBUG_SERIAL.printf("[%s] MTU exchange request failed rc=%d; continuing at MTU %u\n",
                  bms.dev.mac.c_str(), error, (unsigned)client->getMTU());
  } else {
    DEBUG_SERIAL.printf("[%s] MTU exchange request sent\n", bms.dev.mac.c_str());
  }

  bool good = false;
  NimBLERemoteService* service = client->getService(SVC_UUID);
  if (!service) {
    DEBUG_SERIAL.println("  service FFF0 not found, GATT dump:");
    dumpGatt(client);
  } else {
    NimBLERemoteCharacteristic* notifyCharacteristic = service->getCharacteristic(NTF_UUID);
    NimBLERemoteCharacteristic* writeCharacteristic = service->getCharacteristic(WR_UUID);
    if (!notifyCharacteristic || !writeCharacteristic) {
      DEBUG_SERIAL.println("  FFF1/FFF2 not found, GATT dump:");
      dumpGatt(client);
    } else if (!notifyCharacteristic->subscribe(true, onNotify)) {
      DEBUG_SERIAL.println("  subscribe FAILED");
    } else {
      delay(300);
      DEBUG_SERIAL.printf("[%s] current MTU %u\n", bms.dev.mac.c_str(),
                    (unsigned)client->getMTU());
      parser.resetResponses();

      static const uint8_t commands[] = {0x94, 0x90, 0x91, 0x92, 0x93, 0x98};
      for (uint8_t command : commands) {
        if (!sendCmd(writeCharacteristic, command)) {
          DEBUG_SERIAL.printf("  write %02X FAILED\n", command);
        }
        const uint32_t timeoutMs = command == 0x98 ? 500 : 800;
        if (!waitCmd(command, timeoutMs)) DEBUG_SERIAL.printf("  no reply to %02X\n", command);
        delay(80);
      }
      if (bms.alertsValid) {
        DEBUG_SERIAL.printf("[%s] alerts 0x98:", bms.dev.mac.c_str());
        for (uint8_t value : bms.alerts) DEBUG_SERIAL.printf(" %02X", value);
        DEBUG_SERIAL.println();
      } else {
        DEBUG_SERIAL.printf("[%s] alerts 0x98 unavailable\n", bms.dev.mac.c_str());
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
        DEBUG_SERIAL.printf("  cells: got %d/%d frames\n", parser.cellFrameCount(), neededFrames);
      }

      processQueuedModbusWrites(bms.dev.mac.c_str(), writeCharacteristic);
      good = parser.hasCommand(0x90);
    }
  }

  client->disconnect();
  delay(100);
  NimBLEDevice::deleteClient(client);

  bms.ok = good;
  if (good) {
    bms.lastOk = millis();
    DEBUG_SERIAL.printf("[%s] OK  %.1fV %.1fA SOC %.1f%%\n",
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
  DEBUG_SERIAL.printf("[scan] %d ms...\n", SCAN_MS);
  NimBLEScanResults results = scan->getResults(SCAN_MS, false);
  for (int i = 0; i < (int)results.getCount(); i++) {
    const NimBLEAdvertisedDevice* device = results.getDevice(i);
    std::string mac = lc(device->getAddress().toString());
    std::string id = extractIdFromMac(mac);
    Found found(device->getAddress(), mac,
                device->haveName() ? device->getName() : std::string(), id, device->getRSSI(),
                device->isAdvertisingService(NimBLEUUID(SVC_UUID)));
    DEBUG_SERIAL.printf("  %s  rssi %4d  type %d  %-20s %s\n", found.mac.c_str(), found.rssi,
                  (int)device->getAddress().getType(), found.name.c_str(),
                  found.fff0 ? "[FFF0]" : "");
    seen.push_back(found);
  }
  scan->clearResults();
  DEBUG_SERIAL.printf("[scan] %u devices\n", (unsigned)seen.size());
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
        DEBUG_SERIAL.printf("[cfg] %s NOT seen in scan (busy with phone? out of range?) - trying anyway\n",
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
  DEBUG_SERIAL.printf("[cfg] %u BMS target(s)\n", (unsigned)bms.size());
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
            DEBUG_SERIAL.printf("[cfg] target limit (%u) reached; ignoring %s\n",
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
        DEBUG_SERIAL.printf("[cfg] target limit (%u) reached; ignoring %s\n",
                      static_cast<unsigned>(MAX_BMS), device.mac.c_str());
        continue;
      }
      bms.emplace_back(device);
      ++added;
      DEBUG_SERIAL.printf("[cfg] discovered new BMS %s (%s)\n", device.mac.c_str(),
                    device.name.c_str());
    }
  }

  DEBUG_SERIAL.printf("[cfg] rescan complete: %u target(s), %u added, %u refreshed\n",
                static_cast<unsigned>(bms.size()), static_cast<unsigned>(added),
                static_cast<unsigned>(updated));
}
