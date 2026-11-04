#include <NimBLEDevice.h>

static constexpr uint32_t SCAN_DURATION_MS = 10000;
static constexpr uint32_t RETRY_DELAY_MS = 3000;
static constexpr char TARGET_ADDRESS[] = "YOUR_BMS_MAC";

static NimBLEClient* client = nullptr;
static uint32_t next_scan_at = 0;
static bool connection_attempted = false;

class ProbeClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* connected_client) override {
    Serial.printf("BLE event: connected to %s\n", connected_client->getPeerAddress().toString().c_str());
  }

  void onConnectFail(NimBLEClient* failed_client, int reason) override {
    Serial.printf("BLE event: connect failed, reason=%d (0x%04X), NimBLE error=%d\n", reason, reason,
                  failed_client->getLastError());
  }

  void onDisconnect(NimBLEClient* disconnected_client, int reason) override {
    Serial.printf("BLE event: disconnected, reason=%d (0x%04X)\n", reason, reason);
  }
};

static ProbeClientCallbacks client_callbacks;

static void on_notify(NimBLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool is_notify) {
  Serial.printf("[%lu ms] %s %s len=%u: ", millis(), is_notify ? "notify" : "indicate",
                characteristic->getUUID().toString().c_str(), static_cast<unsigned>(length));
  for (size_t index = 0; index < length; ++index) {
    Serial.printf("%02X", data[index]);
    if (index + 1 < length) {
      Serial.write(' ');
    }
  }
  Serial.println();
}

static bool scan_for_target(NimBLEAddress& target_address) {
  NimBLEScan* scanner = NimBLEDevice::getScan();
  scanner->setActiveScan(true);
  scanner->setInterval(100);
  scanner->setWindow(90);

  Serial.printf("Scanning for %s...\n", TARGET_ADDRESS);
  NimBLEScanResults results = scanner->getResults(SCAN_DURATION_MS, false);

  bool found = false;
  for (int index = 0; index < results.getCount(); ++index) {
    const NimBLEAdvertisedDevice* device = results.getDevice(index);
    if (device == nullptr) {
      continue;
    }

    const NimBLEAddress address = device->getAddress();
    const std::string name = device->getName();
    Serial.printf("BLE: name='%s' address=%s type=%u RSSI=%d dBm\n", name.c_str(), address.toString().c_str(),
                  address.getType(), device->getRSSI());

    if (address.toString() == TARGET_ADDRESS) {
      target_address = address;
      found = true;
      break;
    }
  }

  const bool scan_stop_ok = scanner->stop();
  Serial.printf("Explicit scan stop: %s; isScanning=%d\n", scan_stop_ok ? "ok" : "failed",
                scanner->isScanning());
  delay(200);
  scanner->clearResults();
  Serial.println(found ? "Target advertisement found." : "Target was not advertising during this scan.");
  return found;
}

static bool connect_and_subscribe(const NimBLEAddress& address) {
  client = NimBLEDevice::createClient();
  if (client == nullptr) {
    Serial.println("Could not create BLE client.");
    return false;
  }
  client->setClientCallbacks(&client_callbacks, false);

  Serial.printf("Connecting to %s (advertised address type=%u)...\n", address.toString().c_str(), address.getType());
  if (!client->connect(address)) {
    const int error = client->getLastError();
    const char* description = error == 2 ? "operation already in progress" : "see NimBLE host error codes";
    Serial.printf("BLE connection failed: NimBLE error=%d (0x%04X, %s)\n", error, error, description);
    NimBLEDevice::deleteClient(client);
    client = nullptr;
    return false;
  }

  Serial.printf("Connected to %s\n", TARGET_ADDRESS);
  const auto& services = client->getServices(true);
  Serial.printf("Services: %u\n", static_cast<unsigned>(services.size()));

  size_t subscription_count = 0;
  for (auto* service : services) {
    if (service == nullptr) {
      continue;
    }

    Serial.printf("Service %s\n", service->getUUID().toString().c_str());
    const auto& characteristics = service->getCharacteristics(true);
    for (auto* characteristic : characteristics) {
      if (characteristic == nullptr) {
        continue;
      }

      Serial.printf("  Characteristic %s read=%d write=%d writeNR=%d notify=%d indicate=%d\n",
                    characteristic->getUUID().toString().c_str(), characteristic->canRead(),
                    characteristic->canWrite(), characteristic->canWriteNoResponse(),
                    characteristic->canNotify(), characteristic->canIndicate());

      if (characteristic->canNotify() || characteristic->canIndicate()) {
        const bool notifications = characteristic->canNotify();
        if (characteristic->subscribe(notifications, on_notify)) {
          ++subscription_count;
          Serial.printf("  Subscribed using %s.\n", notifications ? "notify" : "indicate");
        } else {
          Serial.println("  Subscription failed.");
        }
      }
    }
  }

  Serial.printf("Subscriptions active: %u. Waiting for frames; no BMS commands will be sent.\n",
                static_cast<unsigned>(subscription_count));
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("DALY BLE probe");
  Serial.println("Disconnect the phone/BMS app while probing.");
  NimBLEDevice::init("DALY-BLE-Probe");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  next_scan_at = millis();
}

void loop() {
  if (client != nullptr && client->isConnected()) {
    delay(100);
    return;
  }

  if (client != nullptr) {
    NimBLEDevice::deleteClient(client);
    client = nullptr;
    Serial.println("BLE connection lost.");
    connection_attempted = true;
  }

  if (connection_attempted) {
    delay(1000);
    return;
  }

  if (static_cast<int32_t>(millis() - next_scan_at) < 0) {
    delay(100);
    return;
  }

  NimBLEAddress address;
  if (scan_for_target(address)) {
    connection_attempted = true;
    if (!connect_and_subscribe(address)) {
      Serial.println("Single connection attempt finished. Reset the ESP32 to retry.");
    }
  } else {
    next_scan_at = millis() + RETRY_DELAY_MS;
  }
}