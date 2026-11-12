# Quick Start: BMS Configuration with ID & Name Support

## 🚀 Get Started in 3 Steps

### Step 1: Open BmsConfig.h
```
../example2/BmsConfig.h
```

### Step 2: Choose Your Mode

#### Option A: Auto-Scan (Default - No Changes Needed)
```cpp
static const struct BmsDeviceConfig BMS_DEVICES[] = {
  {nullptr, nullptr, nullptr}
};
```
✅ Devices auto-discovered  
✅ IDs auto-generated from MAC  
✅ No names needed  

---

#### Option B: Single Device
```cpp
static const struct BmsDeviceConfig BMS_DEVICES[] = {
  {"02:11:22:33:44:55", "Battery-House", "Main"},
  {nullptr, nullptr, nullptr}
};
```
✅ Custom name and ID  
✅ MAC must exactly match your BMS device  

---

#### Option C: Multiple Devices
```cpp
static const struct BmsDeviceConfig BMS_DEVICES[] = {
  {"02:11:22:33:44:55", "Battery-House", "BH"},
  {"02:AA:BB:CC:DD:EE", "Battery-Garage", "BG"},
  {"02:12:34:56:78:9A", "Battery-Shed", "BS"},
  {nullptr, nullptr, nullptr}
};
```
✅ Custom config for each device  
✅ Always end with `{nullptr, nullptr, nullptr}`  

---

#### Option D: Mix of Auto-Generated and Custom IDs
```cpp
static const struct BmsDeviceConfig BMS_DEVICES[] = {
  {"02:11:22:33:44:55", "Battery-House", "Main"},     // All custom
  {"02:AA:BB:CC:DD:EE", "Battery-Garage", nullptr},   // Auto ID
  {"02:12:34:56:78:9A", nullptr, "789A"},            // Only ID
  {nullptr, nullptr, nullptr}
};
```

---

### Step 3: Upload to ESP32

Use Arduino IDE or your build tool. No other changes needed!

---

## 📋 Configuration Fields Explained

```cpp
{
  "02:11:22:33:44:55",    // MAC address (required if not auto-scan)
  "Battery-House",         // Device name (optional, can be nullptr)
  "Main"                   // Device ID (optional, auto-generated if nullptr)
}
```

### MAC Address
- Format: `XX:XX:XX:XX:XX:XX`
- Must match exactly (case-insensitive, will be converted to lowercase)
- Find your MAC in the serial monitor during first scan

### Name (Optional)
- Any string you want to display
- Example: "Battery-House", "Solar-Panel", "Backup-Power"
- Set to `nullptr` to skip (won't appear in JSON)

### ID (Optional)
- Short identifier (2-4 characters recommended)
- Example: "BH", "Main", "Backup"
- If `nullptr`, auto-generated from last 4 MAC characters
- Example MAC `02:11:22:33:44:55` → Auto ID: `4455`

---

## 📱 JSON Output Examples

### Auto-Scan Output
```json
{
  "02:aa:bb:cc:dd:ee": {
    "id": "ddee",
    "battery_voltage": 48.5,
    ...
  }
}
```

### Config Output (with name and custom ID)
```json
{
  "02:11:22:33:44:55": {
    "id": "Main",
    "name": "Battery-House",
    "battery_voltage": 48.5,
    ...
  }
}
```

### Config Output (auto-generated ID, with name)
```json
{
  "02:aa:bb:cc:dd:ee": {
    "id": "ddee",
    "name": "Battery-Garage",
    "battery_voltage": 48.5,
    ...
  }
}
```

---

## 🔍 How to Find Your BMS MAC Address

1. Open serial monitor (9600 baud)
2. Upload firmware with auto-scan mode:
   ```cpp
   {nullptr, nullptr, nullptr}
   ```
3. Look for scan output:
   ```
   [scan] 8000 ms...
   02:11:22:33:44:55  rssi -45  type 0  JHB-Daly [FFF0]
   [scan] 1 devices
   ```
4. Copy the MAC address (first column)

---

## ✅ Verification

### Check if Config Works
1. Serial output should show:
   ```
   [cfg] 02:11:22:33:44:55 NOT seen in scan (busy with phone? out of range?) - trying anyway
   [cfg] 1 BMS target(s)
   ```
   This means it's using your config.

2. Check dashboard JSON:
   - Auto-scan: `"id": "4455"` (no "name" field)
   - Config: `"id": "Main", "name": "Battery-House"`

---

## 🐛 Troubleshooting

### Issue: "BMS target(s): 0"
**Cause:** MAC address doesn't match  
**Fix:** Double-check MAC in config matches serial output exactly

### Issue: "NOT seen in scan (busy with phone?...)"
**Cause:** Device not found during scan, using config MAC anyway  
**Solution:** Normal if device is busy. Firmware will try to connect anyway.

### Issue: Android app shows empty name
**Cause:** Name not set in config  
**Fix:** Add name to config, or accept auto-generated ID-only display

### Issue: ID shows as dots (...)
**Cause:** Dashboard issue, not firmware  
**Fix:** Use shorter custom ID (max 4 chars recommended)

---

## 💡 Best Practices

### 1. Start with Auto-Scan
- Find your MAC addresses first
- Then add custom names if needed

### 2. Use Short IDs
- Good: `"BH"`, `"Main"`, `"789A"`
- Bad: `"BatteryHouseMainFloor"` (too long)

### 3. Use Descriptive Names
- Good: `"Battery-House"`, `"Solar-Array"`, `"Backup-UPS"`
- Bad: `"Device1"`, `"BMS1"` (not descriptive)

### 4. Keep Terminator Line
Always end with:
```cpp
{nullptr, nullptr, nullptr}
```

### 5. Test Incrementally
- Add one device at a time
- Verify JSON output in dashboard
- Then add more devices

---

## 🔄 Switching Between Modes

### From Auto-Scan to Config
```cpp
// Before (auto-scan)
{nullptr, nullptr, nullptr}

// After (config)
{"02:11:22:33:44:55", "My-BMS", "Main"},
{nullptr, nullptr, nullptr}
```

### From Config Back to Auto-Scan
```cpp
// Before (config)
{"02:11:22:33:44:55", "My-BMS", "Main"},
{nullptr, nullptr, nullptr}

// After (auto-scan)
{nullptr, nullptr, nullptr}
```

---

## 📞 Support

If something doesn't work:
1. Check serial output for error messages
2. Verify MAC addresses match exactly
3. Make sure to use the new `BMS_DEVICES` structure (not old `BMS_MACS`)
4. Test with auto-scan mode first

---

## 🎉 You're All Set!

The new configuration system is flexible and backward compatible. Start with what makes sense for your setup and adjust as needed.

Happy monitoring! 🚀
