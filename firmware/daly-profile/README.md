# DALY firmware profile

This is a read-only DALY A5 profile. It is a separate Arduino sketch so the
JK firmware fork remains unchanged. This copy and its web assets live in this
repository under `firmware/daly-profile/`.

## Setup

1. Open `firmware/daly-profile/daly-profile.ino` in Arduino IDE.
2. Install the same ESP32 core, NimBLE-Arduino, and ArduinoJson versions used
   by the main sketch.
3. Set `WIFI_SSID`, `WIFI_PASSWORD`, and `BMS_MAC` near the top of the sketch.
4. Select the same Wi-Fi/BLE-capable ESP32 board and upload the sketch.
5. Copy `firmware/daly-profile/config.json.example` to a file named
   `config.json`. In a browser, open the ESP32's `/fs` page and upload
   `config.json`, `firmware/daly-profile/web/index_lite.html`, and
   `firmware/daly-profile/web/style.css`. Keep the HTML and CSS filenames; the
   root page will then open the lite UI.

## Micro IoT Cloud

Cloud reporting is disabled by default. Copy `config.json.example` to
`config.json`, then set the following fields before uploading it to LittleFS:

- `cloud_enabled`: set to `true` to enable reporting.
- `server_url`: the full HTTPS URL of the cloud write endpoint, ending in
  `/backend/api.php`. The hostname can be changed without rebuilding.
- `device_id` and `device_token`: the device ID and its individual write token
  configured in Micro IoT Cloud.
- `cloud_interval_sec`: cloud update interval from 60 to 180 seconds; defaults
  to 60 when omitted or invalid.

After connecting to Wi-Fi, the sketch synchronizes its clock with NTP and
posts a JSON envelope after the first successful BMS poll, then at the
configured cloud interval. BMS polling and the local web UI continue at their
own faster rate. If a cloud POST fails, the sketch retries after 30 seconds,
after a subsequent successful BMS poll. The envelope contains `device_id`,
`device_token`, and the same telemetry object served by `/data` as `payload`.
NTP is not required for cloud reporting. HTTP errors are reported in Serial,
and the device token is never logged.

HTTPS encrypts the connection, but server certificate verification is disabled
to avoid requiring CA certificate setup. A network attacker could impersonate
the cloud server and capture or alter requests, including the device token.
Use only on a network where you accept this risk.

If LittleFS is reformatted, upload `config.json` again. Keep the populated
configuration private because it contains the device write token.
Only upload it from a trusted local network; the ESP32's `/fs` page does not
authenticate users.

The `/data` endpoint uses the main sketch's JSON field names for telemetry
available from the verified DALY A5 commands. The DALY profile does not expose
JK settings, wire resistance, MOS temperature, balance telemetry, or controls;
those JSON fields are `null`. `/control` returns HTTP 501 and never sends
JK-format writes to the DALY.

The sketch performs one BLE connection attempt per poll and preserves the
advertised address type from scanning. It does not reset the BLE host or retry
connect immediately after a failure.
