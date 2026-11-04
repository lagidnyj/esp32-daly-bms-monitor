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
5. In a browser, open the ESP32's `/fs` page and upload
   `firmware/daly-profile/web/index_lite.html` and
   `firmware/daly-profile/web/style.css`. Keep their original filenames; the
   root page will then open the lite UI.

The `/data` endpoint uses the main sketch's JSON field names for telemetry
available from the verified DALY A5 commands. The DALY profile does not expose
JK settings, wire resistance, MOS temperature, balance telemetry, or controls;
those JSON fields are `null`. `/control` returns HTTP 501 and never sends
JK-format writes to the DALY.

The sketch performs one BLE connection attempt per poll and preserves the
advertised address type from scanning. It does not reset the BLE host or retry
connect immediately after a failure.
