// ============================================================================
// BMS CONFIGURATION EXAMPLES FOR BmsConfig.h
// ============================================================================
// Copy these examples and paste them into the BMS_DEVICES array in BmsConfig.h
// ============================================================================

// EXAMPLE 1: Auto-Scan Mode (Default - No Manual Configuration)
// Devices are auto-discovered by name prefix (JHB-, DL-) or FFF0 service
// ID is auto-generated from MAC (last 4 chars)
// Name is empty (not included in JSON)
static const struct BmsDeviceConfig BMS_DEVICES[] = {
  {nullptr, nullptr, nullptr}
};

// ============================================================================

// EXAMPLE 2: Single Device with All Fields
// MAC: 02:11:22:33:44:55
// Name: "Battery-House" 
// ID: "Main"
static const struct BmsDeviceConfig BMS_DEVICES[] = {
  {"02:11:22:33:44:55", "Battery-House", "Main"},
  {nullptr, nullptr, nullptr}
};

// JSON Output:
// {"02:11:22:33:44:55":{"bms_type":"DALY A5","connected":true,"id":"Main","name":"Battery-House",...}}

// ============================================================================

// EXAMPLE 3: Two Devices with All Fields
static const struct BmsDeviceConfig BMS_DEVICES[] = {
  {"02:11:22:33:44:55", "Battery-House", "BH"},
  {"02:AA:BB:CC:DD:EE", "Battery-Garage", "BG"},
  {nullptr, nullptr, nullptr}
};

// ============================================================================

// EXAMPLE 4: Two Devices with Auto-Generated IDs
// Names are provided, IDs will be auto-generated from MAC
// MAC 02:11:22:33:44:55 → ID will be "4455"
// MAC 02:AA:BB:CC:DD:EE → ID will be "ddee"
static const struct BmsDeviceConfig BMS_DEVICES[] = {
  {"02:11:22:33:44:55", "Battery-House", nullptr},
  {"02:AA:BB:CC:DD:EE", "Battery-Garage", nullptr},
  {nullptr, nullptr, nullptr}
};

// JSON Output:
// {
//   "02:11:22:33:44:55":{"bms_type":"DALY A5","connected":true,"id":"4455","name":"Battery-House",...},
//   "02:aa:bb:cc:dd:ee":{"bms_type":"DALY A5","connected":true,"id":"ddee","name":"Battery-Garage",...}
// }

// ============================================================================

// EXAMPLE 5: Three Devices - Mixed Configuration
// First device: all fields
// Second device: auto-generated ID
// Third device: custom ID, auto-generated name (empty)
static const struct BmsDeviceConfig BMS_DEVICES[] = {
  {"02:11:22:33:44:55", "Battery-House", "Main"},
  {"02:AA:BB:CC:DD:EE", "Battery-Garage", nullptr},      // ID auto-generated: "ddee"
  {"02:12:34:56:78:9A", nullptr, "789A"},               // Name omitted from JSON
  {nullptr, nullptr, nullptr}
};

// ============================================================================

// HOW TO USE:
// 1. Replace the BMS_DEVICES array in BmsConfig.h with one of the examples above
// 2. Update the MAC addresses to match your actual BMS devices
// 3. Customize names and IDs as needed
// 4. Leave the terminat entry {nullptr, nullptr, nullptr} at the end
// 5. If all MAC addresses are nullptr, auto-scan mode is enabled

// ============================================================================

// ID GENERATION RULE:
// If id (third field) is nullptr, it will be auto-generated from the last 4 characters of MAC
// Format: last 4 hex chars of MAC, excluding colons
//
// Examples:
// MAC: 02:11:22:33:44:55 → Auto ID: 4455
// MAC: 02:AA:BB:CC:DD:EE → Auto ID: ddee
// MAC: 02:12:34:56:78:9A → Auto ID: 789A
// MAC: A1:B2:C3:D4:E5:F6 → Auto ID: f5f6

// ============================================================================

// JSON RULES:
// 1. "id" is ALWAYS included if MAC is in config (auto-generated if nullptr)
// 2. "name" is ONLY included if provided (omitted if nullptr or empty)
// 3. "mac" is always the JSON object key and in the JSON if included elsewhere
//
// Auto-Scan JSON Example:
// {"02:aa:bb:cc:dd:ee":{"bms_type":"DALY A5","connected":true,"id":"ddee","battery_voltage":48.5,...}}
//
// Config Mode JSON Example:
// {"02:11:22:33:44:55":{"bms_type":"DALY A5","connected":true,"id":"Main","name":"Battery-House","battery_voltage":48.5,...}}

// ============================================================================
