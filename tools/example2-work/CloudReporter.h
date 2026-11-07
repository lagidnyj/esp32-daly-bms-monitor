#pragma once

#include <Arduino.h>

namespace CloudReporter {
void begin();
void maybeSendUpdate(const String& telemetryJson);
}
