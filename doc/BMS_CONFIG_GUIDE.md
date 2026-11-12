# BMS Configuration Guide - ID & Name Support

## Overview

The BMS monitoring system now supports automatic ID generation and optional device names. This makes it easier to identify devices on dashboards and in monitoring applications.

## Three Operating Rules

### Rule 1: Auto-Scan Mode (No Configuration)
When `BMS_DEVICES` is empty (first entry is `nullptr`):
- **ID**: Generated automatically from last 4 characters of MAC address
- **Name**: Empty (not included in JSON)
- **MAC**: Obtained from BLE scanner during discovery

**Example JSON output:**
```json
{
  "02:aa:bb:cc:dd:ee": {
    "bms_type": "DALY A5",
    "connected": true,
    "id": "ddee",
    "battery_voltage": 48.5,
    ...
  }
}
```

### Rule 2: Configuration Mode (Manual MAC)
When `BMS_DEVICES` has entries with MAC addresses:
- **ID**: Taken from config (or auto-generated from MAC if `nullptr`)
- **Name**: Taken from config (only included if provided)
- **MAC**: From config or discovered in scan

**Example JSON output:**
```json
{
  "02:11:22:33:44:55": {
    "bms_type": "DALY A5",
    "connected": true,
    "id": "Main",
    "name": "Battery-House",
    "battery_voltage": 48.5,
    ...
  }
}
```

### Rule 3: Optional Fields
- If `name` is not provided → not included in JSON (no empty strings)
- If `id` is not provided → auto-generated from MAC
- Android parser uses `optString("name", "")` and `optString("id", "")` to handle missing fields

## Configuration Examples

### Example 1: Auto-Scan Mode (Default)
```cpp
static const struct BmsDeviceConfig BMS_DEVICES[] = {
  {nullptr, nullptr, nullptr}  // Terminator
};
```
Device discovery is automatic based on name prefixes (JHB-, DL-) or FFF0 service advertisement.

### Example 2: All Fields Configured
```cpp
static const struct BmsDeviceConfig BMS_DEVICES[] = {
  {"02:11:22:33:44:55", "Battery-House", "Main"},
  {"02:AA:BB:CC:DD:EE", "Battery-Garage", "Garage"},
  {nullptr, nullptr, nullptr}  // Terminator
};
```
All devices have custom names and IDs.

### Example 3: Auto-Generated ID from MAC
```cpp
static const struct BmsDeviceConfig BMS_DEVICES[] = {
  {"02:11:22:33:44:55", "Battery-1", nullptr},  // ID will be "4455"
  {"02:AA:BB:CC:DD:EE", "Battery-2", nullptr},  // ID will be "ddee"
  {nullptr, nullptr, nullptr}  // Terminator
};
```

### Example 4: Auto-Generated Name, Custom ID
```cpp
static const struct BmsDeviceConfig BMS_DEVICES[] = {
  {"02:12:34:56:78:9A", nullptr, "789A"},  // Name will be empty, ID is "789A"
  {nullptr, nullptr, nullptr}  // Terminator
};
```

### Example 5: Mixed Configuration
```cpp
static const struct BmsDeviceConfig BMS_DEVICES[] = {
  {"02:11:22:33:44:55", "Battery-House", "BH"},    // All fields
  {"02:AA:BB:CC:DD:EE", "Battery-Garage", nullptr},  // Auto ID from MAC
  {"02:12:34:56:78:9A", nullptr, "789A"},          // Auto name, custom ID
  {nullptr, nullptr, nullptr}  // Terminator
};
```

## ID Format

IDs are generated from the **last 4 characters** of the MAC address, excluding colons.

**Examples:**
- MAC: `02:11:22:33:44:55` → ID: `4455`
- MAC: `02:AA:BB:CC:DD:EE` → ID: `ddee`
- MAC: `02:12:34:56:78:9A` → ID: `789A`

## JSON Output Behavior

### Auto-Scan JSON (Rule 1)
```json
{
  "mac_address": {
    "bms_type": "DALY A5",
    "connected": true,
    "id": "ddee",
    "battery_voltage": 48.5,
    ...
  }
}
```
- Always includes `id` (from MAC)
- Never includes `name` (empty)

### Config Mode JSON (Rule 2)
```json
{
  "mac_address": {
    "bms_type": "DALY A5",
    "connected": true,
    "id": "Main",
    "name": "Battery-House",
    "battery_voltage": 48.5,
    ...
  }
}
```
- Includes `id` (from config or auto-generated)
- Includes `name` (if provided in config)

## Android Parser Integration

The Android application already supports optional fields:
```kotlin
val name = d.optString("name", "")   // Empty if field not present
val id   = d.optString("id", "")     // Empty if field not present
val mac  = d.optString("mac", "")    // Always present
```

No changes needed on the Android side—the parser automatically handles both old and new JSON formats.

## Serial Output Examples

### Auto-Scan Discovery
```
[scan] 8000 ms...
  02:aa:bb:cc:dd:ee  rssi -45  type 0  Device123         [FFF0]
[scan] 1 devices
[cfg] 1 BMS target(s)
```

### Config-Based Discovery
```
[cfg] 02:11:22:33:44:55 NOT seen in scan (busy with phone? out of range?) - trying anyway
[cfg] 1 BMS target(s)
```

### Connection and Poll
```
[02:11:22:33:44:55] connecting (Battery-House, addrType 0)...
[02:11:22:33:44:55] connected in 245 ms
[02:11:22:33:44:55] OK  48.5V 0.2A SOC 65.4%
```

## Migration from Old Config

Old format (still works if you had it):
```cpp
static const char* const BMS_MACS[] = {
  "02:11:22:33:44:55",
  nullptr
};
```

New format:
```cpp
static const struct BmsDeviceConfig BMS_DEVICES[] = {
  {"02:11:22:33:44:55", "Battery-1", "Main"},
  {nullptr, nullptr, nullptr}
};
```

Benefits of the new format:
- ✅ Device names for better dashboard display
- ✅ Short IDs instead of full MACs
- ✅ Backward compatible with auto-scan
- ✅ Flexible configuration (any combination of fields)
- ✅ No breaking changes to JSON protocol
