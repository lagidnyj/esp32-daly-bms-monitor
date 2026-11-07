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
- User-supplied local DALY UART/485 Communications Protocol V1.2 and D2
  Modbus protocol spreadsheets were consulted as references; the files are
  not included in this repository.
- [matthewgream/DalyBMSInterface](https://github.com/matthewgream/DalyBMSInterface): modular Arduino/C++ DALY interface, combining documented and reverse-engineered commands.
- [dreadnought/python-daly-bms](https://github.com/dreadnought/python-daly-bms): UART/Bluetooth library with separate protocol implementations and decoded errors.
- [Sinowealth implementation](https://github.com/dreadnought/python-daly-bms/blob/main/dalybms/daly_sinowealth.py) and [A5 error text map](https://github.com/dreadnought/python-daly-bms/blob/main/dalybms/error_codes.py).

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

For the TM battery, the next decisive evidence is a raw BLE capture from its own advertised device: service/characteristic UUIDs, address type, and at least one request/response pair. The supplied UART manual now independently documents the A5 serial protocol, but does not yet prove the TM module uses it over BLE.

## Supplied UART/RS-485 A5 Protocol

The supplied V1.2 UART/485 manual confirms an A5 message family, independently
of the D2/P81 Modbus protocol:

- Physical UART settings are 9600 baud, 8 data bits, no parity, 1 stop bit.
- Host requests use `A5 40 <data-id> 08 <8 reserved bytes> <checksum>`; BMS replies use `A5 01 <data-id> 08 <8 data bytes> <checksum>`. The checksum is the low byte of the sum of the preceding 12 bytes.
- The manual documents data IDs `0x90` through `0x98`. `0x96` returns individual battery temperatures, seven values per frame, with frame numbers starting at zero and a 40-degree offset. It describes up to 21 readings across three frames (despite the heading saying sensors 1-16).
- `0x98` is an eight-byte battery-failure/alarm bitfield, including MOS temperature alarms and MOS temperature sensor faults. The manual does not describe a live MOSFET temperature value in these A5 telemetry responses.
- The manual describes `0x93` byte 3 as BMS life / 0-255 cycles and marks bytes 5-7 of `0x94` reserved. However, the project owner has verified that the current `0x94` bytes 5-6 cycle-count decoding reports the correct cycle count on the target BMS. Preserve the observed working mapping unless new target-device evidence contradicts it.
- The manual does not list a software- or hardware-version query among A5 IDs `0x90`-`0x98`.

The two supplied XLSX files describe D2 Modbus frames (`0xD2`, function
`0x03`/`0x06`/`0x10`, CRC-16), not the fixed-length A5 frames. Their D2
register map lists software version at `0xA9`-`0xB0` and hardware version at
`0xB1`-`0xB8`. These addresses must not be sent as A5 data IDs or assumed to
work through the A5 BLE module.

The A5 UART document makes `0x96` and `0x98` concrete commands to investigate
for this project. However, the UART manual alone does not prove that the
particular BLE module forwards those UART messages unchanged. Confirm by
requesting these IDs over BLE and recording the responses before adding them
to the production parser. The `0x96` readings could provide individual sensor
temperatures; `0x98` can provide alarm flags but not the MOSFET temperature
itself according to this manual.

## Additional DALY Implementations and Alerts

The two additional repositories provide useful independent references, but
they cover more than one DALY protocol family:

- `python-daly-bms`'s standard UART implementation identifies the A5 `0x98`
  response as battery-failure status and maps its bits to alarm text in
  `error_codes.py`. The documented categories include voltage, temperature,
  current, SOC, voltage/temperature difference, MOS temperature/sensor/MOS
  faults, AFE/collection, EEPROM/RTC, precharge, communications, and current
  module faults. This aligns with the supplied V1.2 manual and with the user's
  observation that the official app displays alerts.
- The same repository's standard UART implementation requests individual
  sensor temperatures with `0x96`, cell balancing with `0x97`, and errors with
  `0x98`. Its Bluetooth adapter instead requests `0x95` for temperatures,
  `0x96` for balancing, and `0x97` for errors, while using a different BLE
  write/notification setup. Treat that command mapping as adapter/protocol
  specific; do not copy it blindly into this sketch's FFF0/FFF1/FFF2 A5
  transport. Confirm raw request/response IDs from the target module.
- `python-daly-bms/daly_sinowealth.py` explicitly targets the BMStool /
  Sinowealth variant. It reads a different register-style command set (for
  example, cell voltages `0x01`-`0x0A`, temperatures `0x0C`-`0x0F`, cycles
  `0x14`, pack status `0x15`, and battery status `0x16`). Those IDs and its
  status-bit labels are not interchangeable with A5 data IDs.
- `DalyBMSInterface` documents A5 status/diagnostic commands `0x90`-`0x98`
  and also implements additional information and threshold commands that its
  README describes as reverse-engineered. Its command names/availability do
  not establish that every DALY model or BLE module supports those commands.

Taken together, the references make A5 alarm parsing via `0x98` and individual
temperature parsing via `0x96` promising next steps. The alert list in the
Python implementation is a mapping of the A5 failure bits, not evidence that
the different Sinowealth `0x16` status register has the same layout. Likewise,
the repositories do not establish that the user's BLE module exposes all
UART-documented or reverse-engineered commands unchanged.

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

The supplied UART manual confirms the A5 serial framing and documents
commands `0x96` and `0x98`. Still verify that the target BLE module forwards
these A5 requests and replies unchanged, confirm the cycle-count field against
a real frame, and identify whether the target hardware exposes any proprietary
version query beyond the documented A5 IDs. The D2 Modbus XLSX register map is
not evidence that its version registers are available over this A5 BLE link.