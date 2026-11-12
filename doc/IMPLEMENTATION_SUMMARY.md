# BMS Configuration Implementation Summary

## Changes Made

### 1. **BmsConfig.h** - Configuration Structure
**Before:**
```cpp
static const char* const BMS_MACS[] = {
  // "02:11:22:33:44:55",
  nullptr
};
```

**After:**
```cpp
struct BmsDeviceConfig {
  const char* mac;   // MAC address (e.g., "02:11:22:33:44:55")
  const char* name;  // Device name (e.g., "Battery-1"), can be nullptr
  const char* id;    // Device ID (e.g., "4455"), can be nullptr
};

static const struct BmsDeviceConfig BMS_DEVICES[] = {
  {nullptr, nullptr, nullptr}  // Terminator
};
```

**Benefits:**
- Structured configuration instead of flat array
- Support for names and IDs
- Backward compatible with auto-scan mode

---

### 2. **BmsTypes.h** - Device Data Structure
**Before:**
```cpp
struct Found {
  NimBLEAddress addr;
  std::string mac;
  std::string name;
  int rssi;
  bool fff0;

  Found(const NimBLEAddress& address, const std::string& deviceMac,
        const std::string& deviceName, int signal, bool advertisesFff0)
    : addr(address), mac(deviceMac), name(deviceName), 
      rssi(signal), fff0(advertisesFff0) {}
};
```

**After:**
```cpp
struct Found {
  NimBLEAddress addr;
  std::string mac;
  std::string name;
  std::string id;    // NEW: Device ID (last 4 MAC chars or from config)
  int rssi;
  bool fff0;

  Found(const NimBLEAddress& address, const std::string& deviceMac,
        const std::string& deviceName, const std::string& deviceId, 
        int signal, bool advertisesFff0)
    : addr(address), mac(deviceMac), name(deviceName), id(deviceId),
      rssi(signal), fff0(advertisesFff0) {}
};
```

**Benefits:**
- Store device ID alongside MAC and name
- ID can be auto-generated or configured

---

### 3. **BmsOutput.cpp** - JSON Generation
**Before:**
```cpp
String buildJson(const std::vector<Bms>& bms) {
  String json = "{";
  for (size_t i = 0; i < bms.size(); i++) {
    const Bms& device = bms[i];
    if (i) json += ',';
    const bool connected = device.lastOk != 0 && device.consecutiveFailures < 3;
    json += "\"" + String(device.dev.mac.c_str()) + "\":{";
    json += "\"bms_type\":\"DALY A5\"";
    json += ",\"connected\":" + String(connected ? "true" : "false");
    // ... rest of fields
```

**After:**
```cpp
String buildJson(const std::vector<Bms>& bms) {
  String json = "{";
  for (size_t i = 0; i < bms.size(); i++) {
    const Bms& device = bms[i];
    if (i) json += ',';
    const bool connected = device.lastOk != 0 && device.consecutiveFailures < 3;
    json += "\"" + String(device.dev.mac.c_str()) + "\":{";
    json += "\"bms_type\":\"DALY A5\"";
    json += ",\"connected\":" + String(connected ? "true" : "false");
    
    // Add id if available
    if (!device.dev.id.empty()) {
      json += ",\"id\":\"" + String(device.dev.id.c_str()) + "\"";
    }
    
    // Add name if available
    if (!device.dev.name.empty()) {
      json += ",\"name\":\"" + String(device.dev.name.c_str()) + "\"";
    }
    // ... rest of fields
```

**Key Change:**
- `id` is always included if available
- `name` is only included if not empty
- This matches the 3-rule specification

---

### 4. **BmsBle.cpp** - Device Discovery & Configuration

#### New Function: `extractIdFromMac()`
```cpp
// Extract last 4 characters of MAC address as device ID (format: 1122)
static std::string extractIdFromMac(const std::string& mac) {
  if (mac.length() >= 5) {
    std::string id;
    id += mac[mac.length() - 5];   // Third char after last colon
    id += mac[mac.length() - 4];   // Fourth char after last colon
    id += mac[mac.length() - 2];   // First char after colon+1
    id += mac[mac.length() - 1];   // Last char
    return id;
  }
  return "";
}
```

**Example:** MAC `02:11:22:33:44:55` → ID `4455`

#### Updated: `doScan()`
```cpp
void doScan() {
  // ... scan setup ...
  for (int i = 0; i < (int)results.getCount(); i++) {
    const NimBLEAdvertisedDevice* device = results.getDevice(i);
    std::string mac = lc(device->getAddress().toString());
    std::string id = extractIdFromMac(mac);  // NEW: Auto-generate ID
    Found found(device->getAddress(), mac,
                device->haveName() ? device->getName() : std::string(), 
                id,  // NEW parameter
                device->getRSSI(),
                device->isAdvertisingService(NimBLEUUID(SVC_UUID)));
    // ... rest
  }
}
```

#### Updated: `buildTargets()`
```cpp
void buildTargets() {
  bms.clear();
  if (BMS_DEVICES[0].mac != nullptr) {
    // Config-based mode: use configured devices
    for (int i = 0; BMS_DEVICES[i].mac != nullptr && bms.size() < MAX_BMS; i++) {
      const std::string cfgMac = lc(BMS_DEVICES[i].mac);
      // Auto-generate ID if not provided
      const std::string cfgId = BMS_DEVICES[i].id ? 
        std::string(BMS_DEVICES[i].id) : extractIdFromMac(cfgMac);
      // Use configured name if provided, otherwise empty
      const std::string cfgName = BMS_DEVICES[i].name ? 
        std::string(BMS_DEVICES[i].name) : std::string();
      
      // Apply config to discovered device or create new entry
      // ...
    }
  } else {
    // Auto-scan mode: discover devices by name prefix or FFF0 service
    // ... existing logic unchanged
  }
}
```

#### Updated: `scanAndUpdateTargets()`
Similar logic as `buildTargets()` but for re-scanning

---

## JSON Output Examples

### Auto-Scan Mode (No Configuration)
```json
{
  "02:aa:bb:cc:dd:ee": {
    "bms_type": "DALY A5",
    "connected": true,
    "id": "ddee",
    "battery_voltage": 48.5,
    "battery_power": 9.7,
    "charge_current": 0.2,
    "percent_remain": 65.4,
    "capacity_remain": 130.80,
    "cycle_count": 145,
    "battery_t1": 25,
    "battery_t2": 26,
    "alerts": null,
    "cell_voltages": [3.251, 3.260, 3.255, ...],
    "average_cell_voltage": 3.256,
    "delta_cell_voltage": 0.009,
    "charge": true,
    "discharge": false,
    "cell_count": 16
  }
}
```

**Note:** `name` field is missing (not empty string, completely absent)

### Config Mode (Manual Configuration)
```json
{
  "02:11:22:33:44:55": {
    "bms_type": "DALY A5",
    "connected": true,
    "id": "Main",
    "name": "Battery-House",
    "battery_voltage": 48.5,
    "battery_power": 9.7,
    "charge_current": 0.2,
    "percent_remain": 65.4,
    "capacity_remain": 130.80,
    "cycle_count": 145,
    "battery_t1": 25,
    "battery_t2": 26,
    "alerts": null,
    "cell_voltages": [3.251, 3.260, 3.255, ...],
    "average_cell_voltage": 3.256,
    "delta_cell_voltage": 0.009,
    "charge": true,
    "discharge": false,
    "cell_count": 16
  }
}
```

**Note:** Both `id` and `name` fields are present

---

## Three Operating Rules Implemented

### Rule 1: Auto-Scan Mode (No MAC Configuration)
✅ **ID Formation:** Auto-generated from last 4 MAC characters  
✅ **Name:** Empty (not included in JSON)  
✅ **Source:** From BLE scanner discovery  

### Rule 2: Configuration Mode (MAC in BMS_DEVICES)
✅ **ID Formation:** From config, or auto-generated if nullptr  
✅ **Name:** From config, or omitted if nullptr  
✅ **Source:** From BMS_DEVICES array  

### Rule 3: Optional Fields
✅ **Name:** Only included in JSON if not empty  
✅ **ID:** Always included if MAC is known  
✅ **Backward Compatibility:** Android parser handles missing fields  

---

## File Modifications Summary

| File | Changes | Lines |
|------|---------|-------|
| BmsConfig.h | Changed array structure, added comments | ~30 |
| BmsTypes.h | Added `id` field to `Found` struct, updated constructor | ~15 |
| BmsOutput.cpp | Added conditional JSON output for `id` and `name` | ~10 |
| BmsBle.cpp | Added `extractIdFromMac()` function, updated scan and config logic | ~100 |

**Total Changes:** ~155 lines modified/added

---

## Testing Recommendations

1. **Auto-Scan Mode:**
   - Keep `BMS_DEVICES[0].mac = nullptr`
   - Verify IDs are generated from MAC
   - Verify names are omitted from JSON

2. **Config Mode:**
   - Set up `BMS_DEVICES` with custom configs
   - Test with all fields, auto-generated ID, auto-generated name
   - Verify JSON includes both `id` and `name` when configured

3. **Android Parser:**
   - No changes needed (already uses `optString()`)
   - Test with old JSON (without `id`/`name`)
   - Test with new JSON (with `id`/`name`)

---

## Benefits

✅ **User-Friendly Identification:** Use custom names instead of long MACs  
✅ **Flexible IDs:** Short 4-char IDs auto-generated or custom  
✅ **Backward Compatible:** Auto-scan mode unchanged  
✅ **Optional Fields:** No JSON bloat from empty values  
✅ **Dashboard Friendly:** Clean names and IDs for display  
✅ **No Breaking Changes:** Existing Android app works without modification  

---

## Migration Path

**From old config:**
```cpp
static const char* const BMS_MACS[] = {
  "02:11:22:33:44:55",
  nullptr
};
```

**To new config:**
```cpp
static const struct BmsDeviceConfig BMS_DEVICES[] = {
  {"02:11:22:33:44:55", "Battery-House", "Main"},
  {nullptr, nullptr, nullptr}
};
```

The new format supports everything the old format could do, plus names and custom IDs.
