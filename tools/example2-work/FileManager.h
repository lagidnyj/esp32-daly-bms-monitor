#pragma once

#include <Arduino.h>

namespace FileManager {
void begin();
void loop();
void setTelemetryJson(const String& json);
}
