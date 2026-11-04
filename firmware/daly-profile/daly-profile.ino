#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <NimBLEDevice.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cstring>
#include <string>

static constexpr char WIFI_SSID[] = "YOUR_WIFI_SSID";
static constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
static constexpr char BMS_MAC[] = "YOUR_BMS_MAC";
static constexpr uint32_t SCAN_DURATION_MS = 8000;
static constexpr uint32_t CONNECT_TIMEOUT_MS = 10000;
static constexpr uint32_t POLL_INTERVAL_MS = 10000;
static constexpr uint8_t CELL_CAPACITY = 48;

static constexpr char SERVICE_UUID[] = "0000fff0-0000-1000-8000-00805f9b34fb";
static constexpr char NOTIFY_UUID[] = "0000fff1-0000-1000-8000-00805f9b34fb";
static constexpr char WRITE_UUID[] = "0000fff2-0000-1000-8000-00805f9b34fb";

static WebServer server(80);
static NimBLEAddress bmsAddress;
static NimBLEClient* client = nullptr;
static NimBLERemoteCharacteristic* writeCharacteristic = nullptr;
static uint32_t nextPollAt = 0;
static uint32_t lastGoodPollAt = 0;

struct Telemetry {
  float voltage = 0;
  float current = 0;
  float soc = 0;
  uint32_t remainingMah = 0;
  uint16_t cycles = 0;
  uint8_t cellCount = 0;
  uint8_t temperatureCount = 0;
  bool charge = false;
  bool discharge = false;
  int8_t minTemperature = 0;
  int8_t maxTemperature = 0;
  uint16_t cellMv[CELL_CAPACITY] = {};
};

static Telemetry telemetry;
static bool hasAddress = false;
static bool receivedCommand[256] = {};
static bool receivedCellFrame[16] = {};
static uint8_t cellFrameCount = 0;
static int cellFrameBase = -1;

static uint8_t receiveBuffer[512];
static size_t receiveLength = 0;
static uint8_t parseBuffer[256];
static size_t parseLength = 0;
static portMUX_TYPE receiveMux = portMUX_INITIALIZER_UNLOCKED;

static bool checksumValid(const uint8_t* frame) {
  uint8_t sum = 0;
  for (size_t i = 0; i < 12; ++i) {
    sum += frame[i];
  }
  return sum == frame[12];
}

static void parseFrame(const uint8_t* frame) {
  const uint8_t command = frame[2];
  const uint8_t* payload = frame + 4;
  receivedCommand[command] = true;

  switch (command) {
    case 0x90: {
      telemetry.voltage = ((payload[0] << 8) | payload[1]) / 10.0f;
      const int rawCurrent = (payload[4] << 8) | payload[5];
      telemetry.current = (rawCurrent - 30000) / 10.0f;
      telemetry.soc = ((payload[6] << 8) | payload[7]) / 10.0f;
      break;
    }
    case 0x92:
      telemetry.maxTemperature = static_cast<int8_t>(payload[0] - 40);
      telemetry.minTemperature = static_cast<int8_t>(payload[2] - 40);
      break;
    case 0x93:
      telemetry.charge = payload[1] != 0;
      telemetry.discharge = payload[2] != 0;
      telemetry.remainingMah = (static_cast<uint32_t>(payload[4]) << 24) |
                               (static_cast<uint32_t>(payload[5]) << 16) |
                               (static_cast<uint32_t>(payload[6]) << 8) | payload[7];
      break;
    case 0x94:
      telemetry.cellCount = payload[0] > CELL_CAPACITY ? CELL_CAPACITY : payload[0];
      telemetry.temperatureCount = payload[1];
      telemetry.cycles = (payload[5] << 8) | payload[6];
      break;
    case 0x95: {
      const int frameNumber = payload[0];
      if (cellFrameBase < 0) {
        cellFrameBase = frameNumber == 0 ? 0 : 1;
      }
      const int frameIndex = frameNumber - cellFrameBase;
      if (frameIndex < 0 || frameIndex >= 16) {
        break;
      }
      for (int i = 0; i < 3; ++i) {
        const int cellIndex = frameIndex * 3 + i;
        if (cellIndex < CELL_CAPACITY) {
          telemetry.cellMv[cellIndex] = (payload[1 + i * 2] << 8) | payload[2 + i * 2];
        }
      }
      if (!receivedCellFrame[frameIndex]) {
        receivedCellFrame[frameIndex] = true;
        ++cellFrameCount;
      }
      break;
    }
    default:
      break;
  }
}

static void onNotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
  portENTER_CRITICAL(&receiveMux);
  if (receiveLength + length <= sizeof(receiveBuffer)) {
    memcpy(receiveBuffer + receiveLength, data, length);
    receiveLength += length;
  }
  portEXIT_CRITICAL(&receiveMux);
}

static void processNotifications() {
  uint8_t incoming[sizeof(receiveBuffer)];
  size_t incomingLength;
  portENTER_CRITICAL(&receiveMux);
  incomingLength = receiveLength;
  memcpy(incoming, receiveBuffer, incomingLength);
  receiveLength = 0;
  portEXIT_CRITICAL(&receiveMux);

  if (incomingLength == 0) {
    return;
  }
  if (parseLength + incomingLength > sizeof(parseBuffer)) {
    parseLength = 0;
  }
  memcpy(parseBuffer + parseLength, incoming, incomingLength);
  parseLength += incomingLength;

  while (parseLength >= 13) {
    if (parseBuffer[0] != 0xA5 || !checksumValid(parseBuffer)) {
      memmove(parseBuffer, parseBuffer + 1, --parseLength);
      continue;
    }
    parseFrame(parseBuffer);
    parseLength -= 13;
    memmove(parseBuffer, parseBuffer + 13, parseLength);
  }
}

static bool waitForCommand(uint8_t command, uint32_t timeoutMs) {
  const uint32_t startedAt = millis();
  while (millis() - startedAt < timeoutMs) {
    server.handleClient();
    processNotifications();
    if (receivedCommand[command]) {
      return true;
    }
    delay(10);
  }
  processNotifications();
  return receivedCommand[command];
}

static bool sendCommand(uint8_t command) {
  uint8_t frame[13] = {0xA5, 0x40, command, 0x08};
  uint8_t sum = 0;
  for (size_t i = 0; i < 12; ++i) {
    sum += frame[i];
  }
  frame[12] = sum;
  receivedCommand[command] = false;
  return writeCharacteristic->writeValue(frame, sizeof(frame), writeCharacteristic->canWrite());
}

static bool scanForBms() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(90);

  Serial.printf("Scanning for DALY %s...\n", BMS_MAC);
  NimBLEScanResults results = scan->getResults(SCAN_DURATION_MS, false);
  bool found = false;
  for (int i = 0; i < results.getCount(); ++i) {
    const NimBLEAdvertisedDevice* device = results.getDevice(i);
    if (device == nullptr) {
      continue;
    }
    const NimBLEAddress address = device->getAddress();
    Serial.printf("BLE %s type=%u RSSI=%d\n", address.toString().c_str(),
                  address.getType(), device->getRSSI());
    if (address.toString() == BMS_MAC) {
      bmsAddress = address;
      hasAddress = true;
      found = true;
      break;
    }
  }
  scan->stop();
  scan->clearResults();
  Serial.println(found ? "DALY found." : "DALY not found.");
  return found;
}

static bool pollBms() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (scan->isScanning()) {
    scan->stop();
    for (int i = 0; i < 50 && scan->isScanning(); ++i) {
      delay(20);
    }
  }
  if (scan->isScanning()) {
    Serial.println("BLE scan still active; skipping this poll.");
    return false;
  }

  client = NimBLEDevice::createClient();
  if (client == nullptr) {
    Serial.println("Could not create BLE client.");
    return false;
  }
  client->setConnectTimeout(CONNECT_TIMEOUT_MS);
  Serial.printf("Connecting to %s (addrType %u)...\n", BMS_MAC, bmsAddress.getType());
  if (!client->connect(bmsAddress, true, false, false)) {
    Serial.printf("DALY connection failed: rc=%d\n", client->getLastError());
    NimBLEDevice::deleteClient(client);
    client = nullptr;
    return false;
  }

  NimBLERemoteService* service = client->getService(SERVICE_UUID);
  NimBLERemoteCharacteristic* notifyCharacteristic =
      service == nullptr ? nullptr : service->getCharacteristic(NOTIFY_UUID);
  writeCharacteristic = service == nullptr ? nullptr : service->getCharacteristic(WRITE_UUID);
  bool success = false;

  if (notifyCharacteristic == nullptr || writeCharacteristic == nullptr ||
      !notifyCharacteristic->subscribe(true, onNotify)) {
    Serial.println("DALY service/characteristics missing or notify subscription failed.");
  } else {
    delay(200);
    memset(receivedCommand, 0, sizeof(receivedCommand));
    memset(receivedCellFrame, 0, sizeof(receivedCellFrame));
    memset(telemetry.cellMv, 0, sizeof(telemetry.cellMv));
    cellFrameCount = 0;
    cellFrameBase = -1;
    parseLength = 0;
    portENTER_CRITICAL(&receiveMux);
    receiveLength = 0;
    portEXIT_CRITICAL(&receiveMux);

    static constexpr uint8_t COMMANDS[] = {0x94, 0x90, 0x91, 0x92, 0x93};
    for (uint8_t command : COMMANDS) {
      if (!sendCommand(command) || !waitForCommand(command, 800)) {
        Serial.printf("No response to DALY command %02X\n", command);
      }
      delay(80);
    }

    if (telemetry.cellCount > 0) {
      const uint8_t neededFrames = (telemetry.cellCount + 2) / 3;
      if (sendCommand(0x95)) {
        const uint32_t startedAt = millis();
        while (millis() - startedAt < 1500 && cellFrameCount < neededFrames) {
          server.handleClient();
          processNotifications();
          delay(10);
        }
      }
    }
    success = receivedCommand[0x90];
    if (success) {
      lastGoodPollAt = millis();
    }
  }

  client->disconnect();
  delay(100);
  NimBLEDevice::deleteClient(client);
  client = nullptr;
  writeCharacteristic = nullptr;
  return success;
}

static void handleData() {
  DynamicJsonDocument doc(4096);
  JsonObject device = doc.createNestedObject(BMS_MAC);
  device["bms_type"] = "DALY A5";
  device["connected"] = lastGoodPollAt != 0 && millis() - lastGoodPollAt < POLL_INTERVAL_MS * 3;
  device["battery_voltage"] = telemetry.voltage;
  device["battery_power"] = telemetry.voltage * telemetry.current;
  device["charge_current"] = telemetry.current;
  device["percent_remain"] = telemetry.soc;
  device["capacity_remain"] = telemetry.remainingMah / 1000.0f;
  device["nominal_capacity"] = nullptr;
  device["cycle_count"] = telemetry.cycles;
  device["cycle_capacity"] = nullptr;
  device["uptime"] = "—";
  device["battery_t1"] = telemetry.minTemperature;
  device["battery_t2"] = telemetry.maxTemperature;
  device["mos_temp"] = nullptr;

  JsonArray cellVoltages = device.createNestedArray("cell_voltages");
  uint32_t sumMv = 0;
  uint16_t minMv = UINT16_MAX;
  uint16_t maxMv = 0;
  for (uint8_t i = 0; i < telemetry.cellCount; ++i) {
    const uint16_t millivolts = telemetry.cellMv[i];
    cellVoltages.add(millivolts / 1000.0f);
    if (millivolts != 0) {
      sumMv += millivolts;
      if (millivolts < minMv) minMv = millivolts;
      if (millivolts > maxMv) maxMv = millivolts;
    }
  }
  uint8_t validCells = 0;
  for (uint8_t i = 0; i < telemetry.cellCount; ++i) {
    if (telemetry.cellMv[i] != 0) ++validCells;
  }
  device["average_cell_voltage"] = validCells == 0 ? 0 : sumMv / (1000.0f * validCells);
  device["delta_cell_voltage"] = minMv == UINT16_MAX ? 0 : (maxMv - minMv) / 1000.0f;
  device.createNestedArray("wire_resist");
  device["charge"] = telemetry.charge;
  device["discharge"] = telemetry.discharge;
  device["balance"] = nullptr;
  device["balancing_action"] = nullptr;
  device["balance_curr"] = nullptr;
  device["cell_count"] = telemetry.cellCount;
  device["total_battery_capacity"] = nullptr;
  device["balance_trigger_voltage"] = nullptr;
  device["balance_starting_voltage"] = nullptr;
  device["max_charge_current"] = nullptr;
  device["max_discharge_current"] = nullptr;
  device["max_balance_current"] = nullptr;
  device["cell_undervoltage_protection"] = nullptr;
  device["cell_undervoltage_recovery"] = nullptr;
  device["cell_overvoltage_protection"] = nullptr;
  device["cell_overvoltage_recovery"] = nullptr;
  device["power_off_voltage"] = nullptr;
  device["charge_overcurrent_protection_delay"] = nullptr;
  device["charge_overcurrent_protection_recovery_time"] = nullptr;
  device["discharge_overcurrent_protection_delay"] = nullptr;
  device["discharge_overcurrent_protection_recovery_time"] = nullptr;
  device["short_circuit_protection_recovery_time"] = nullptr;
  device["charge_overtemperature_protection"] = nullptr;
  device["charge_overtemperature_protection_recovery"] = nullptr;
  device["discharge_overtemperature_protection"] = nullptr;
  device["discharge_overtemperature_protection_recovery"] = nullptr;
  device["charge_undertemperature_protection"] = nullptr;
  device["charge_undertemperature_protection_recovery"] = nullptr;
  device["power_tube_overtemperature_protection"] = nullptr;
  device["power_tube_overtemperature_protection_recovery"] = nullptr;
  device["short_circuit_protection_delay"] = nullptr;

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

static void handleControl() {
  server.send(501, "text/plain", "DALY profile is read-only; JK control commands are disabled.");
}

static void handleRoot() {
  if (LittleFS.exists("/index_lite.html")) {
    server.sendHeader("Location", "/lite");
    server.send(302, "text/plain", "Redirecting");
    return;
  }
  if (LittleFS.exists("/index.html")) {
    server.sendHeader("Location", "/main");
    server.send(302, "text/plain", "Redirecting");
    return;
  }
  server.send(200, "text/html",
              "<p>DALY profile is running. Upload index_lite.html and style.css at "
              "<a href='/fs'>/fs</a>. Telemetry: <a href='/data'>/data</a>.</p>");
}

static File uploadFile;

static void handleUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;
    uploadFile = LittleFS.open(filename, "w");
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) uploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
  }
}

static void handleFileUploadComplete() {
  server.send(200, "text/plain", "Upload complete. Return to /");
}

static void handleFileManager() {
  server.send(200, "text/html",
              "<form method='POST' action='/upload' enctype='multipart/form-data'>"
              "<input type='file' name='file'><button>Upload</button></form>");
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.printf("\nWi-Fi connected: %s\n", WiFi.localIP().toString().c_str());

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed.");
  }
  server.serveStatic("/main", LittleFS, "/index.html");
  server.serveStatic("/lite", LittleFS, "/index_lite.html");
  server.serveStatic("/style.css", LittleFS, "/style.css", "text/css");
  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/control", HTTP_POST, handleControl);
  server.on("/fs", HTTP_GET, handleFileManager);
  server.on("/upload", HTTP_POST, handleFileUploadComplete, handleUpload);
  server.begin();

  NimBLEDevice::init("DALY-Monitor");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  hasAddress = scanForBms();
  nextPollAt = millis();
}

void loop() {
  server.handleClient();
  if (static_cast<int32_t>(millis() - nextPollAt) < 0) {
    delay(10);
    return;
  }

  if (!hasAddress) {
    hasAddress = scanForBms();
  }
  if (hasAddress && !pollBms()) {
    hasAddress = false;
  }
  nextPollAt = millis() + POLL_INTERVAL_MS;
}
