# BMS Configuration Architecture

## Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                         ESP32 Firmware                          │
└─────────────────────────────────────────────────────────────────┘
                              ▼
                     ┌────────────────┐
                     │   doScan()     │
                     │                │
                     │ • Scan BLE     │
                     │ • Extract ID   │
                     │   from MAC     │
                     └────────────────┘
                              ▼
                     ┌────────────────┐
                     │    Found       │
                     │   Objects      │
                     │ • mac: string  │
                     │ • id: string   │
                     │ • name: string │
                     └────────────────┘
                              ▼
        ┌─────────────────────┴─────────────────────┐
        │                                           │
        ▼                                           ▼
┌──────────────────┐                       ┌──────────────────┐
│  buildTargets()  │                       │ BMS_DEVICES      │
│                  │                       │ Config Array     │
│ Check Config:    │                       │                  │
│ MAC != nullptr?  │◄──────────────────────┤ [0] = MAC,name,  │
│                  │                       │     id           │
└──────────────────┘                       │ [...] ...        │
        ▼                                   │ [n] = nullptr    │
     YES/NO                                 └──────────────────┘
    /      \
   /        \
  ▼          ▼
Config     Auto-Scan
Mode       Mode
  │            │
  │            ▼
  │      ┌──────────────────┐
  │      │ Use Discovered   │
  │      │ Devices:         │
  │      │ • By Name Prefix │
  │      │ • By FFF0 Service│
  │      │ • Auto ID        │
  │      │ • Empty Name     │
  │      └──────────────────┘
  │            │
  ▼            ▼
┌──────────────────────────────────────┐
│  Create/Update Bms Objects           │
│  (vector<Bms>)                       │
│  • dev: Found object                 │
│  • ok, voltage, current, etc.        │
└──────────────────────────────────────┘
        │
        │
        ▼
┌──────────────────────────────────────┐
│    pollOne(Bms& device)              │
│  • Connect via BLE                   │
│  • Exchange data                     │
│  • Poll telemetry                    │
└──────────────────────────────────────┘
        │
        │
        ▼
┌──────────────────────────────────────┐
│    buildJson()                       │
│  • Create JSON object                │
│  • Include id (always)               │
│  • Include name (if not empty)       │
│  • Include telemetry fields          │
└──────────────────────────────────────┘
        │
        │
        ▼
┌──────────────────────────────────────┐
│  JSON Output to Monitor/Dashboard    │
│                                      │
│  {                                   │
│    "mac": {                          │
│      "id": "Main",                   │
│      "name": "Battery-House",        │
│      "connected": true,              │
│      "battery_voltage": 48.5,        │
│      ...                             │
│    }                                 │
│  }                                   │
└──────────────────────────────────────┘
        │
        │
        ▼
    Android App
    Dashboard
    Web UI
```

## Mode Decision Tree

```
                    Start
                      │
                      ▼
            BMS_DEVICES[0].mac?
               /           \
              /             \
            YES              NO
             │               │
             ▼               ▼
        ┌─────────┐    ┌──────────────┐
        │ CONFIG  │    │ AUTO-SCAN    │
        │ MODE    │    │ MODE         │
        └─────────┘    └──────────────┘
            │               │
            │               ├─→ Find devices by:
            │               │   • Name prefix
            ├─→ Use config  │   • FFF0 service
            │   MAC address │
            │               ├─→ Extract ID from MAC
            ├─→ Auto ID or  │
            │   use config  ├─→ No name (empty)
            │   ID          │
            ├─→ Use config  └─→ JSON: id only
            │   name or     │
            │   omit        │
            │               │
            └─→ JSON:       │
                id + name   │
                (optional)  │
```

## Configuration Structure

```cpp
struct BmsDeviceConfig {
    const char* mac;    // "02:11:22:33:44:55"
    const char* name;   // "Battery-House" or nullptr
    const char* id;     // "Main" or nullptr (auto-gen if nullptr)
};

static const struct BmsDeviceConfig BMS_DEVICES[] = {
    // Device 1: Full configuration
    {"02:11:22:33:44:55", "Battery-House", "Main"},
    
    // Device 2: Auto-generated ID
    {"02:AA:BB:CC:DD:EE", "Battery-Garage", nullptr},
    
    // Device 3: Custom ID, no name
    {"02:12:34:56:78:9A", nullptr, "789A"},
    
    // Terminator (required)
    {nullptr, nullptr, nullptr}
};
```

## Data Structure Relationships

```
BmsDeviceConfig (Config Array)
    │
    ├─ mac (const char*)
    ├─ name (const char*)
    └─ id (const char*)
        │
        ▼ (read during buildTargets)
        │
Found (Discovered Device)
    │
    ├─ addr (NimBLEAddress)
    ├─ mac (std::string) ◄─── copied from BmsDeviceConfig.mac
    ├─ name (std::string) ◄─── from BmsDeviceConfig.name or empty
    ├─ id (std::string) ◄────── from config or extractIdFromMac()
    ├─ rssi (int)
    └─ fff0 (bool)
        │
        ▼
Bms (Active Device)
    │
    ├─ dev (Found)
    │   └─ (contains mac, name, id)
    ├─ ok (bool)
    ├─ lastOk (uint32_t)
    ├─ v, a, soc (float) - telemetry
    ├─ cells[], mv[] - cell voltages
    └─ alerts[]
        │
        ▼ (in buildJson)
        │
JSON Object
    │
    └─ "mac": {
        ├─ "id": "..." (always if known)
        ├─ "name": "..." (only if not empty)
        ├─ "battery_voltage": ...
        ├─ "charge_current": ...
        └─ ...
    }
```

## ID Extraction Logic

```
MAC Address: 02:11:22:33:44:55
Position:    01234567890123456

String length = 17

Extract positions:
- mac[17-5] = mac[12] = '4'
- mac[17-4] = mac[13] = '4'
- mac[17-2] = mac[15] = '5'
- mac[17-1] = mac[16] = '5'

Result: "4455"

Why this works:
- MAC format: XX:XX:XX:XX:XX:XX (17 chars total)
- Positions: 0-1, 2 (colon), 3-4, 5 (colon), 6-7, 8 (colon), 
            9-10, 11 (colon), 12-13, 14 (colon), 15-16
- Last byte: positions 15-16
- Second-to-last byte: positions 12-13
```

## Function Call Flow

```
setup()
  └─ NimBLEDevice::init()
  └─ scanAndUpdateTargets()
       └─ doScan()
            └─ Found.extractIdFromMac(mac)
       └─ buildTargets()
            ├─ Check BMS_DEVICES[0].mac
            └─ Create Bms vector

loop()
  └─ Get current millis
  └─ If poll time:
       └─ pollOne(Bms& device)
  └─ If rescan time:
       └─ scanAndUpdateTargets()
  └─ If report time:
       └─ buildJson()
            └─ Include id/name conditionally
       └─ Send to monitor/cloud
```

## Memory Layout (Conceptual)

```
Flash Memory (Program):
┌───────────────────────────────┐
│ BMS_DEVICES config array      │ (const)
│ ┌─ Device 1                   │
│ │  "02:11:22:33:44:55"        │
│ │  "Battery-House"            │
│ │  "Main"                     │
│ ├─ Device 2                   │
│ │  "02:AA:BB:CC:DD:EE"        │
│ │  "Battery-Garage"           │
│ │  nullptr                    │
│ └─ Terminator                 │
└───────────────────────────────┘

SRAM (Runtime):
┌───────────────────────────────┐
│ seen (vector<Found>)          │ (from scan)
│ ├─ Found[0]                   │
│ │  mac = "02:aa:bb:cc:dd:ee"  │
│ │  id = "ddee" (auto-gen)    │
│ │  name = "Device123"         │
│ └─ ...                        │
└───────────────────────────────┘

SRAM (Runtime):
┌───────────────────────────────┐
│ bms (vector<Bms>)             │ (active devices)
│ ├─ Bms[0]                     │
│ │  dev.mac = "02:11:22:33:44:55"
│ │  dev.id = "Main"            │
│ │  dev.name = "Battery-House" │
│ │  v = 48.5                   │
│ │  a = 0.2                    │
│ │  cells[], mv[], alerts[]    │
│ └─ ...                        │
└───────────────────────────────┘

JSON Output (String):
┌───────────────────────────────┐
│ {"02:11:22:33:44:55":{        │
│ "id":"Main",                  │
│ "name":"Battery-House",       │
│ "battery_voltage":48.5,       │
│ ...                           │
│ }}                            │
└───────────────────────────────┘
```

## State Transitions

```
Device Lifecycle:

1. DISCOVERY
   BLE Scan → Found objects created
   ID extracted from MAC
   Names from BLE advertisement or config
   
2. TARGET SELECTION (buildTargets)
   Auto-Scan: Filter by name prefix or FFF0
   Config: Use configured MACs
   
3. INITIALIZATION (buildTargets cont.)
   Create Bms objects
   Store device references
   
4. ACTIVE POLLING (pollOne)
   Connect via BLE
   Query telemetry
   Update Bms fields
   
5. REPORTING (buildJson)
   Serialize to JSON
   Include id (always)
   Include name (if configured)
   Send to monitor/dashboard
   
6. REFRESH (scanAndUpdateTargets)
   Re-scan devices
   Update RSSI
   Reapply config
   Maintain poll state
```

## Configuration Decision Matrix

```
┌──────────────┬──────────┬──────────┬─────────────────┐
│ Scenario     │ MAC      │ Name     │ ID              │
├──────────────┼──────────┼──────────┼─────────────────┤
│ Auto-Scan    │ Discover │ (empty)  │ Auto from MAC   │
│ (default)    │ from BLE │          │ (last 4 chars)  │
├──────────────┼──────────┼──────────┼─────────────────┤
│ Config       │ Config   │ Config   │ Config          │
│ (all fields) │          │          │                 │
├──────────────┼──────────┼──────────┼─────────────────┤
│ Config       │ Config   │ Config   │ Auto from MAC   │
│ (auto ID)    │          │          │ (nullptr)       │
├──────────────┼──────────┼──────────┼─────────────────┤
│ Config       │ Config   │ (empty)  │ Config          │
│ (no name)    │          │ (nullptr)│                 │
└──────────────┴──────────┴──────────┴─────────────────┘

JSON Output:

Auto-Scan:      {"mac": {..., "id": "ddee"}}
Config Full:    {"mac": {..., "id": "Main", "name": "Battery-House"}}
Config Auto ID: {"mac": {..., "id": "ddee", "name": "Battery-Garage"}}
Config No Name: {"mac": {..., "id": "789A"}}
```

## Benefits of This Architecture

```
Modularity:
  ├─ Configuration separate from discovery
  ├─ ID generation isolated in extractIdFromMac()
  ├─ JSON output conditional for optional fields
  └─ Device polling independent of configuration

Flexibility:
  ├─ Can use auto-scan or config mode
  ├─ Can mix auto-generated and custom values
  ├─ Can add more devices without code changes
  └─ Can switch modes by changing config

Efficiency:
  ├─ ID extracted once per scan
  ├─ No redundant string operations
  ├─ Configuration loaded at compile time
  └─ JSON output optimized for size

Maintainability:
  ├─ Clear separation of concerns
  ├─ Consistent naming patterns
  ├─ Documented field purposes
  └─ Testable components
```

