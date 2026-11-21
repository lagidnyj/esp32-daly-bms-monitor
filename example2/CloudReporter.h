#pragma once

#include <Arduino.h>

namespace CloudReporter {
enum class Status {
  NotConfigured,
  Waiting,
  Ok,
  Error
};

void begin();
void maybeSendUpdate(const String& telemetryJson);
Status status();
}
