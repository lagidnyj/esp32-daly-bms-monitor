#pragma once

// BMS device configuration structure
struct BmsDeviceConfig {
  const char* mac;   // MAC address (e.g., "02:11:22:33:44:55")
  const char* name;  // Device name (e.g., "Battery-1"), can be nullptr
  const char* id;    // Device ID (e.g., "4455"), can be nullptr for auto-generation from last 4 MAC chars
};

// Leave empty (first entry nullptr) to auto-pick devices (name starts with a known prefix or advertises FFF0). max 8 pcs
// For manual selection, add one or more device configs with your BMS details.
// Keep final entry with all nullptr as terminator.
static const struct BmsDeviceConfig BMS_DEVICES[] = {
  // Example with all fields:
  // {"02:11:22:33:44:55", "Battery-1", "4455"},
  // Example with auto-generated ID from MAC:
  // {"02:AA:BB:CC:DD:EE", "Battery-2", nullptr},
  // Example with auto-generated name from ID:
  // {"02:12:34:56:78:9A", nullptr, "789A"},
  {nullptr, nullptr, nullptr}  // Terminator
};

static const char* const BMS_NAME_PREFIXES[] = {"JHB-", "DL-"};
static const char* const WIFI_SSID = "YOUR_WIFI_SSID";
static const char* const WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
static const char* const OTA_PASSWORD = "CHANGE_ME_OTA_PASSWORD";
#define SCAN_MS           8000
#define RESCAN_INTERVAL_MS (15UL * 60UL * 1000UL)
#define CONNECT_MS        10000
#define POLL_INTERVAL_MS  10000
#define MAX_BMS           8
#define CONN_MIN          24
#define CONN_MAX          40
#define BLE_MTU           123
#define VERBOSE           1  // 1 = print raw BLE commands and response frames
#define PRINT_JSON        1  // 1 = print telemetry JSON to Serial

static const char* const SVC_UUID = "0000fff0-0000-1000-8000-00805f9b34fb";
static const char* const NTF_UUID = "0000fff1-0000-1000-8000-00805f9b34fb";
static const char* const WR_UUID  = "0000fff2-0000-1000-8000-00805f9b34fb";
