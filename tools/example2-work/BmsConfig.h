#pragma once

// Leave empty to auto-pick devices (name starts with a known prefix or advertises FFF0). max 8 pcs
// For manual selection, uncomment and replace one or more example addresses with your BMS MACs.
// Keep nullptr as the final entry.
static const char* const BMS_MACS[] = {
  // "02:11:22:33:44:55",
  // "02:AA:BB:CC:DD:EE",
  // "02:12:34:56:78:9A",
  nullptr
};

static const char* const BMS_NAME_PREFIXES[] = {"JHB-", "DL-"};
static const char* const WIFI_SSID = "YOUR_WIFI_SSID";
static const char* const WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
#define SCAN_MS           8000
#define CONNECT_MS        10000
#define POLL_INTERVAL_MS  10000
#define MAX_BMS           8
#define CONN_MIN          24
#define CONN_MAX          40
#define VERBOSE           0  // 1 = print raw BLE commands and response frames
#define PRINT_JSON        0  // 1 = print telemetry JSON to Serial

static const char* const SVC_UUID = "0000fff0-0000-1000-8000-00805f9b34fb";
static const char* const NTF_UUID = "0000fff1-0000-1000-8000-00805f9b34fb";
static const char* const WR_UUID  = "0000fff2-0000-1000-8000-00805f9b34fb";
