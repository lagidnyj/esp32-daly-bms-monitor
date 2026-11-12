# Example 2 modular sketch

Install NimBLE-Arduino 2.x and ArduinoJson 6 in the Arduino environment used
to build the sketch. The sketch requests a BLE MTU of 123. It connects without
automatic MTU exchange, then requests exchange separately and prints the
current MTU after notification subscription. A rejected exchange does not
abort polling.

`VERBOSE` and `PRINT_JSON` are set to `1` in `BmsConfig.h` for protocol
diagnostics: raw response frames (including `0x98`) and parsed telemetry JSON
are printed to Serial at 115200 baud. Set either to `0` to reduce output.
JSON remains available at `/fdata` and is still sent to the cloud when cloud
reporting is enabled.

The UI preserves the last BMS status during temporary `/data` request failures
and shows a `data delayed` badge after 30 seconds without a successful update.
A BMS is marked offline after three consecutive failed BLE polling attempts;
a successful poll clears the failure count.
Each BMS card displays decoded alerts in Ukrainian below the cell voltages,
including unknown alert bits with their byte/bit positions. Raw alert bytes can
be expanded for diagnostics.

The sketch scans for new BMS devices at startup and every 15 minutes while
there is room below `MAX_BMS`. Periodic scans happen after polling and may pause
polling for the configured scan duration. Newly discovered devices are added
without clearing telemetry for existing targets. Press `s` in Serial to trigger
an immediate discovery scan. When `BMS_MACS` is explicitly configured, scans
only refresh those configured targets; they do not add unlisted devices.

## Wi-Fi and NTP

On first boot, the device starts a protected setup access point when no Wi-Fi
credentials are stored. Connect to the `Daly-BMS-<suffix>` network; its
password and the setup URL are printed to Serial at 115200 baud. Open
`http://192.168.4.1/wifi` to configure the network. The setup page is also
available at `http://<device-ip>/wifi` during normal operation, so credentials
can be changed without a reset button. Saving credentials stores them in NVS
and restarts the device. Stored credentials take precedence over the optional
`WIFI_SSID` and `WIFI_PASSWORD` defaults in `BmsConfig.h`.

The setup access point has a five-minute timeout, after which the device
restarts and begins setup again. If Wi-Fi has connected successfully since
boot, a later network outage only triggers periodic reconnect attempts; it does
not start the setup AP. If the device starts while the router is unavailable,
it tries the saved network for 30 seconds before starting the setup AP. If the
network returns while setup is active, the device reconnects and stops the AP.
BLE polling continues without Wi-Fi. Once connected, the sketch synchronizes
the clock from NTP.

## Partition table

`partitions.csv` is a custom 4 MiB flash layout with two 1.75 MiB OTA app
slots and a 448 KiB filesystem. Select the board's **Custom** partition scheme
when building this sketch; the CSV must remain in the sketch directory. This
table assumes the selected board has 4 MiB flash. The current 1,403,224-byte
pre-OTA firmware image fits in either slot, leaving about 422 KiB for the OTA
code and future growth. Verify the final compiled image remains below 1.75 MiB.

## LittleFS file manager

When the ESP32 is connected to Wi-Fi, open `http://<device-ip>/fs` to list,
upload, preview, download, or delete files. Upload one file at a time. Preview
is limited to files up to 8 KiB. Filesystem formatting is intentionally not
available in the web UI so a mistaken click cannot erase the cloud config and
other files.

The sketch includes default `index.html` and `bms.css` assets (about 18 KiB of
flash). At startup, each is copied to LittleFS only if that file is missing;
existing files are preserved. If either is deleted, the default is restored
after the next device restart. The page is served at `http://<device-ip>/`,
and related CSS, JavaScript, and image files are served from their matching
paths. The current telemetry JSON is available at
`http://<device-ip>/data` and `http://<device-ip>/fdata`, and can be loaded by
the page with `fetch('/data').then(response => response.json())`. Both
endpoints return the latest completed polling cycle and send
`Cache-Control: no-store`.

### Browser requirements

The UI requires a modern browser with `fetch`, ES2020 JavaScript syntax, and
`AbortSignal.timeout()`. Minimum browser versions for these features are:

- Chrome and Chromium-based browsers: 103+
- Firefox: 100+
- Safari on macOS and iOS: 16.0+

Internet Explorer is not supported. Older or embedded browsers may load the
page but fail to run its telemetry refresh script. The CSS has a `100vh`
fallback for browsers that do not support the newer `100dvh` viewport unit.

## Firmware updates

Open `http://<device-ip>/update` and authenticate with the username `vendor`
and the password set in `OTA_PASSWORD` in `BmsConfig.h`. OTA is disabled until
the placeholder is replaced with a password of at least 16 characters. Choose
the compiled firmware `.bin` file; it is written to the inactive OTA slot and
the device restarts after the image is accepted. Web requests are serviced
between BMS polling transactions, so an upload may wait for a current BLE
transaction to finish. The custom partition table must be installed and both
OTA slots must be large enough for the firmware.

The password is compiled into the firmware. Do not commit a personalized
password to a shared repository. OTA uses HTTP Basic Authentication over plain
HTTP, so use it only on a trusted local network; the password and firmware
transfer are not encrypted in transit.

The sketch also requests experimental A5 alert data (`0x98`). Each device's
JSON contains `alerts: null` if there was no reply, or an object with the raw
eight-byte response (`raw`), recognized active alert names (`active`), and
set bits without a known label (`unknown_bits`). The bit labels come from a
generic DALY UART implementation and may not match every BMS or BLE module.
Serial prints the raw response bytes when available. Set `PRINT_JSON` to `1`
to print the decoded alert object in Serial as part of the full telemetry JSON.

The file manager and Wi-Fi setup page have no login or password. Use them only
on a trusted local network; anyone who can reach the ESP32 can read, upload, or
delete filesystem files (including cloud configuration) or change Wi-Fi
credentials.

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
