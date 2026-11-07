# Example 2 modular sketch

Install NimBLE-Arduino 2.x and ArduinoJson 6 in the Arduino environment used
to build the sketch.

Set `VERBOSE` to `1` in `BmsConfig.h` to print raw BLE commands and response
frames to Serial. Set `PRINT_JSON` to `1` to print telemetry JSON to Serial.
Both are disabled by default; JSON remains available at `/fdata` and is still
sent to the cloud when cloud reporting is enabled.

## Wi-Fi and NTP

Set `WIFI_SSID` and `WIFI_PASSWORD` in `BmsConfig.h`. The sketch connects to
Wi-Fi, synchronizes the clock from NTP, and continues BLE polling if Wi-Fi is
unavailable. It retries the Wi-Fi connection periodically.

## LittleFS file manager

When the ESP32 is connected to Wi-Fi, open `http://<device-ip>/fs` to list,
upload, preview, download, or delete files. Upload one file at a time. Preview
is limited to files up to 8 KiB. Filesystem formatting is intentionally not
available in the web UI so a mistaken click cannot erase the cloud config and
other files.

Upload an `index.html` file to LittleFS and it will be served at
`http://<device-ip>/`. Related CSS, JavaScript, and image files are served from
their matching paths. The current telemetry JSON is available at
`http://<device-ip>/data` and `http://<device-ip>/fdata`, and can be loaded by
the page with `fetch('/data').then(response => response.json())`. Both
endpoints return the latest completed polling cycle and send
`Cache-Control: no-store`.

The sketch also requests experimental A5 alert data (`0x98`). Each device's
JSON contains `alerts: null` if there was no reply, or an object with the raw
eight-byte response (`raw`), recognized active alert names (`active`), and
set bits without a known label (`unknown_bits`). The bit labels come from a
generic DALY UART implementation and may not match every BMS or BLE module.
Serial prints the raw response bytes when available. Set `PRINT_JSON` to `1`
to print the decoded alert object in Serial as part of the full telemetry JSON.

The file manager has no login or password. Use it only on a trusted local
network; anyone who can reach the ESP32 can read, upload, or delete filesystem
files, including the cloud configuration.

## Micro IoT Cloud

Cloud reporting is disabled by default. Copy `config.json.example` to
`config.json`, then set `cloud_enabled`, the full HTTPS `server_url`,
`device_id`, and `device_token`. Upload `config.json` to the ESP32 LittleFS
filesystem using the filesystem upload tool for your Arduino setup. The cloud
interval is 60-180 seconds and defaults to 60 seconds. You can upload
`config.json` directly through the LittleFS file manager at `/fs`.

After at least one BMS responds successfully, the sketch sends the same
MAC-keyed telemetry JSON printed to Serial, wrapped in the Micro IoT Cloud
envelope containing `device_id`, `device_token`, and `payload`. Failed requests
are retried after 30 seconds. The device token is not printed.

HTTPS certificate verification is disabled, as in `firmware/daly-profile`.
This encrypts traffic but does not authenticate the server; a network
attacker could impersonate the cloud endpoint and capture or alter the
request, including its device token. Use this mode only if you accept that
risk. Keep populated `config.json` files private and do not commit them.
