# DALY BLE Protocol Research

Research notes for adapting the JK-based monitor to DALY while keeping its
existing data model and behavior where practical. No firmware changes are made
as part of this research note.

## Sources

- [syssi/esphome-daly-bms](https://github.com/syssi/esphome-daly-bms): ESPHome BLE implementation and supported-device notes.
- [BLE component](https://github.com/syssi/esphome-daly-bms/tree/main/components/daly_bms_ble): GATT transport, request queue, frame handling, decoders, and control entities.
- [D2 vs. P81 register map](https://github.com/syssi/esphome-daly-bms/blob/main/docs/protocol-register-map.md): field/register comparison.
- [BLE protocol tests](https://github.com/syssi/esphome-daly-bms/tree/main/tests/components/daly_bms_ble): sample frames and decoder tests for D2 and P81.
- [BMS_BLE-HA DALY plugin](https://github.com/patman15/BMS_BLE-HA/blob/main/custom_components/bms_ble/plugins/daly_bms.py): another BLE implementation; its README lists DALY devices advertised as `DL-…`.
- [User-supplied PlatformIO A5 example](../tools/examle): BLE polling/parser example for a different MAC address.
- A local copy of DALY's UART/RS-485 protocol manual was consulted; the document
  is not redistributed in this repository.

The upstream component README also references the vendor protocol spreadsheet,
`docs/dalyModbusProtocol.xlsx`, as the source for its D2 field definitions.

## BLE and Frame Formats

- Upstream uses BLE service UUID `0xFFF0`, notification characteristic `0xFFF1`, and command/write characteristic `0xFFF2`.
- BLE is the transport. The application payload is a register-oriented protocol; the code builds requests, serializes them through a command queue, validates response frames, and dispatches them to decoders.
- D2 frames start with `0xD2`; common requests use function `0x03` (read) and `0x06` (write), followed by an address/value or register count and CRC16.
- The separate P81/DL variant sends requests beginning with `0x81` and receives responses beginning with `0x51`. Its register blocks and response lengths differ from D2.
- The component defaults to D2 and allows selecting P81. Its README lists common K/H/M/S series as likely D2-compatible, an ESS-DL example using P81, and J/T/A/U/W/ND series as unsupported by this component because they use `0xA5` frames.
- The target BMS for this project is the DALY TM series. The sources checked so far do not explicitly name TM; the separate Home Assistant integration lists DALY BLE devices advertising as `DL-…`, but that does not establish TM compatibility.
- Do not assume the BLE protocol from the DALY brand or the BMS being a modern model alone. Confirm the exact model/module and observed BLE device/protocol first.

## User-Supplied A5 Example

The PlatformIO example in [`tools/examle`](../tools/examle) uses the same common
BLE service and characteristics (`FFF0` service, `FFF1` notify, `FFF2` write),
but its application protocol is different from D2/P81:

- Frames are 13 bytes: `A5 40 <command> 08 <8 payload bytes> <sum>`; the checksum is the low byte of the sum of the first 12 bytes.
- Requests use commands `0x90`-`0x95` and `0x97`; the sample's parser interprets matching response commands for pack values, cell extrema, temperature extrema, state/MOSFETs, configuration, per-cell voltages, and balancing bits.
- This matches the `0xA5` protocol family that the Syssi ESPHome README says its D2/P81 component does not support. The example's BMS address differs from the module observed for this project, so it does not yet prove the TM BMS uses A5.
- The example scans first and connects using the discovered `NimBLEAddress`, preserving the advertised address type. That is a better diagnostic than guessing public/random for a direct MAC connection.
- The example appears written for an older NimBLE-Arduino API (`NimBLEAdvertisedDeviceCallbacks`, one-argument `onDisconnect`). Its `setConnectTimeout(5)` also needs version checking: NimBLE 2.x measures timeout arguments in milliseconds, making `5` far too short.
- Its notify subscription result is ignored, and the parser consumes each 13-byte chunk after checking only the start byte; a bad checksum drops that chunk without trying to resynchronize within it. Treat it as a useful protocol lead, not a validated production parser.

For the TM battery, the next decisive evidence is a raw BLE capture from its own advertised device: service/characteristic UUIDs, address type, and at least one request/response pair. Until that is available, A5 is a plausible branch to test, not a confirmed TM protocol.

## Connection Topology

- A DALY BMS may expose UART internally or through a connector, with an add-on Wi-Fi/BLE module attached to that UART. In this setup, the ESP32 connects over BLE to the module; UART is the module-to-BMS link, not necessarily the ESP32-to-BMS link.
- A separate BLE dongle may communicate with the BMS over UART on its BMS-facing side. Whether it forwards UART frames unchanged over BLE, or wraps/transforms them, must be verified for that dongle.
- Some BMS units may have Bluetooth integrated. Its GATT service, framing, and relationship to the UART protocol are not established yet.
- Therefore, the UART/RS-485 PDF may describe the payload protocol used behind a BLE module, but it does not by itself establish the BLE GATT transport or prove that a given BLE product forwards those bytes transparently.
- The project owner reports that integrated modules advertise as `JHB-<MAC>` and newer modules as `DL-<MAC>`, with the MAC apparently encoded without colons. External adapters may also use these names; the exact relationship between name, module generation, and BMS model is not yet verified.
- Treat these prefixes as useful BLE discovery/matching clues only. They do not prove that `JHB-…` and `DL-…` use the same GATT characteristics or payload protocol.

## Data Available

For supported D2 devices, the implementation decodes or publishes:

- Pack voltage, current, power, and SOC.
- Per-cell voltages (up to 32), cell count, min/max/average voltage, and min/max cell indices.
- Battery temperatures, min/max temperatures, temperature probe indices, and MOSFET temperature.
- Charge/discharge state, remaining capacity, cycle count, balancing state/current, and MOSFET states.
- Error bitmask and human-readable alarm strings from the status frame.
- Version information and configurable settings from separate responses.

P81 provides a similar core set, with up to 48 cell slots and additional status
fields such as pre-charge MOSFET, energy counter, and temperature probe
indices. The register addresses are not interchangeable with D2: for example,
SOC is in a different register block, and many status registers shift by
different amounts.

Cell resistance is not part of the comparable decoded field set in this
implementation, so omitting it from a shared JK/DALY output model looks
reasonable unless the target DALY model provides it separately.

## Alarms and Controls

- D2 alarm flags are combined into an error bitmask and mapped to text labels.
- P81 alarm registers (`0x00A4` onward) are currently logged as raw frames in this upstream implementation; they are not yet decoded into equivalent alarm entities there.
- The balancer switch is at `0x00CF` for both D2 and P81 and has read/write handling in the component.
- D2 exposes charging and discharging MOSFET switches at settings addresses `0x00A5` and `0x00A6`.
- The D2 settings map includes an SOC setting at `0x00A7`; generic setting-register write support exists. P81's corresponding settings block is shifted, but this implementation does not decode its settings blocks into the same setting entities. Do not treat SOC adjustment or MOSFET writes as confirmed for P81 without checking the target BMS and the actual firmware path.
- These are BMS control writes, not just telemetry. Any port should make them explicit, protocol-specific, and opt-in.

## Implications for This Project

The likely reuse boundary is the BMS data/transport adapter: retain the JK
monitor's normalized output and downstream behavior, and add DALY BLE
connection, framing, protocol selection, and field mapping behind it. The
ESPHome component is a valuable behavioral reference, but importing it
wholesale would also import ESPHome-specific component/entity abstractions.

Before implementation, identify the exact DALY model and BLE variant, then
compare its captured frames against both this upstream implementation and the
official PDF. In particular, verify alarm support and write behavior for the
target model before promising those fields or controls.

## Open Verification

The local PDF is named as a UART/RS-485 protocol document and is seven pages.
Its text has not yet been extracted in this environment, so equivalence between
its serial framing and the BLE payloads above remains unverified. The next
useful step is to extract/inspect it and compare frame headers, register maps,
and CRC rules with the BLE implementation, then check whether the target BLE
module exposes that same payload or uses a separate BLE protocol.