#include "BmsOutput.h"

#include <Arduino.h>

#include "BmsConfig.h"

namespace {
const char* const ALERT_NAMES[8][8] = {
  {
    "unit_over_voltage_warning_1",
    "unit_over_voltage_warning_1",
    "unit_over_voltage_warning_1",
    "unit_over_voltage_warning_2",
    "total_voltage_high_alarm_1",
    "total_voltage_high_alarm_2",
    "total_voltage_low_alarm_1",
    "total_voltage_low_alarm_2"
  },
  {
    "charge_temperature_high_alarm_1",
    "charge_temperature_high_alarm_2",
    "charge_temperature_low_alarm_1",
    "charge_temperature_low_alarm_2",
    "discharge_temperature_high_alarm_1",
    "discharge_temperature_high_alarm_2",
    "discharge_temperature_low_alarm_1",
    "discharge_temperature_low_alarm_2"
  },
  {
    "charge_overcurrent_alarm_1",
    "charge_overcurrent_alarm_2",
    "discharge_overcurrent_alarm_1",
    "discharge_overcurrent_alarm_2",
    "soc_high_alarm_1",
    "soc_high_alarm_2",
    "soc_low_alarm_1",
    "soc_low_alarm_2"
  },
  {
    "cell_voltage_difference_alarm_1",
    "cell_voltage_difference_alarm_2",
    "temperature_difference_alarm_1",
    "temperature_difference_alarm_2",
    nullptr, nullptr, nullptr, nullptr
  },
  {
    "charge_mos_overtemperature_warning",
    "discharge_mos_overtemperature_warning",
    "charge_mos_temperature_sensor_fault",
    "discharge_mos_temperature_sensor_fault",
    "charge_mos_stuck_fault",
    "discharge_mos_stuck_fault",
    "charge_mos_breaker_fault",
    "discharge_mos_breaker_fault"
  },
  {
    "afe_fault",
    "cell_collection_fault",
    "temperature_sensor_fault",
    "eeprom_fault",
    "rtc_fault",
    "precharge_fault",
    "vehicle_communication_fault",
    "internal_communication_fault"
  },
  {
    "current_module_fault",
    "main_voltage_detection_fault",
    "short_circuit_protection_fault",
    "low_voltage_no_charge",
    nullptr, nullptr, nullptr, nullptr
  },
  {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}
};

void appendAlertsJson(String& json, const Bms& device) {
  if (!device.alertsValid) {
    json += "null";
    return;
  }

  static const char HEX_DIGITS[] = "0123456789ABCDEF";
  json += "{\"raw\":\"";
  for (uint8_t value : device.alerts) {
    json += HEX_DIGITS[value >> 4];
    json += HEX_DIGITS[value & 0x0F];
  }
  json += "\",\"active\":[";
  bool first = true;
  for (uint8_t byte = 0; byte < 8; byte++) {
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (!(device.alerts[byte] & (1U << bit)) || !ALERT_NAMES[byte][bit]) continue;
      if (!first) json += ',';
      json += "\"" + String(ALERT_NAMES[byte][bit]) + "\"";
      first = false;
    }
  }
  json += "],\"unknown_bits\":[";
  first = true;
  for (uint8_t byte = 0; byte < 8; byte++) {
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (!(device.alerts[byte] & (1U << bit)) || ALERT_NAMES[byte][bit]) continue;
      if (!first) json += ',';
      json += "{\"byte\":" + String(byte) + ",\"bit\":" + String(bit) + "}";
      first = false;
    }
  }
  json += "]}";
}
}

String buildJson(const std::vector<Bms>& bms) {
  const uint32_t now = millis();
  String json = "{";
  for (size_t i = 0; i < bms.size(); i++) {
    const Bms& device = bms[i];
    if (i) json += ',';
    const bool connected = device.lastOk != 0 &&
                           now - device.lastOk < POLL_INTERVAL_MS * 3UL;
    json += "\"" + String(device.dev.mac.c_str()) + "\":{";
    json += "\"bms_type\":\"DALY A5\"";
    json += ",\"connected\":" + String(connected ? "true" : "false");
    json += ",\"battery_voltage\":" + String(device.v, 1);
    json += ",\"battery_power\":" + String(device.v * device.a, 1);
    json += ",\"charge_current\":" + String(device.a, 1);
    json += ",\"percent_remain\":" + String(device.soc, 1);
    json += ",\"capacity_remain\":" + String(device.remMah / 1000.0f, 2);
    json += ",\"cycle_count\":" + String(device.cycles);
    json += ",\"battery_t1\":" + String(device.tMin);
    json += ",\"battery_t2\":" + String(device.tMax);
    json += ",\"alerts\":";
    appendAlertsJson(json, device);

    json += ",\"cell_voltages\":[";
    uint32_t sumMv = 0;
    uint16_t minMv = UINT16_MAX;
    uint16_t maxMv = 0;
    uint8_t validCells = 0;
    const uint8_t cellCount = device.cells < 48 ? device.cells : 48;
    for (uint8_t cell = 0; cell < cellCount; cell++) {
      if (cell) json += ',';
      json += String(device.mv[cell] / 1000.0f, 3);
      if (device.mv[cell] != 0) {
        sumMv += device.mv[cell];
        if (device.mv[cell] < minMv) minMv = device.mv[cell];
        if (device.mv[cell] > maxMv) maxMv = device.mv[cell];
        validCells++;
      }
    }
    json += "]";
    const float averageCellVoltage =
        validCells ? sumMv / (1000.0f * validCells) : 0.0f;
    const float deltaCellVoltage =
        validCells ? (maxMv - minMv) / 1000.0f : 0.0f;
    json += ",\"average_cell_voltage\":" + String(averageCellVoltage, 3);
    json += ",\"delta_cell_voltage\":" + String(deltaCellVoltage, 3);
    json += ",\"charge\":" + String(device.chg ? "true" : "false");
    json += ",\"discharge\":" + String(device.dis ? "true" : "false");
    json += ",\"cell_count\":" + String(cellCount);
    json += "}";
  }
  json += "}";
  return json;
}
