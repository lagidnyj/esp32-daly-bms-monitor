#include "DalyParser.h"

#include <Arduino.h>
#include <string.h>

#include "BmsConfig.h"

void DalyParser::begin(Bms& bms) {
  current_ = &bms;
  accLen_ = 0;
}

void DalyParser::resetResponses() {
  memset(gotCmd_, 0, sizeof(gotCmd_));
  memset(f95_, 0, sizeof(f95_));
  frames95_ = 0;
  base95_ = -1;
}

void DalyParser::clearCommand(uint8_t command) {
  gotCmd_[command] = false;
}

bool DalyParser::hasCommand(uint8_t command) const {
  return gotCmd_[command];
}

int DalyParser::cellFrameCount() const {
  return frames95_;
}

bool DalyParser::checksumOk(const uint8_t* frame) {
  uint8_t sum = 0;
  for (int i = 0; i < 12; i++) sum += frame[i];
  return sum == frame[12];
}

void DalyParser::parseFrame(const uint8_t* frame) {
  const uint8_t command = frame[2];
  const uint8_t* payload = frame + 4;
  if (VERBOSE) {
    Serial.printf("  <- %02X:", command);
    for (int i = 0; i < 13; i++) Serial.printf(" %02X", frame[i]);
    Serial.println();
  }

  gotCmd_[command] = true;
  if (!current_) return;
  Bms& bms = *current_;
  switch (command) {
    case 0x90:
      bms.v = ((payload[0] << 8) | payload[1]) / 10.0f;
      bms.a = (((payload[4] << 8) | payload[5]) - 30000) / 10.0f;
      bms.soc = ((payload[6] << 8) | payload[7]) / 10.0f;
      break;
    case 0x91:
      bms.maxMv = (payload[0] << 8) | payload[1];
      bms.minMv = (payload[3] << 8) | payload[4];
      break;
    case 0x92:
      bms.tMax = payload[0] - 40;
      bms.tMin = payload[2] - 40;
      break;
    case 0x93:
      bms.state = payload[0];
      bms.chg = payload[1];
      bms.dis = payload[2];
      bms.remMah = ((uint32_t)payload[4] << 24) | ((uint32_t)payload[5] << 16) |
                   (payload[6] << 8) | payload[7];
      break;
    case 0x94:
      bms.cells = payload[0];
      bms.temps = payload[1];
      bms.cycles = (payload[5] << 8) | payload[6];
      break;
    case 0x95: {
      const int number = payload[0];
      if (base95_ < 0) base95_ = (number == 0) ? 0 : 1;
      const int index = number - base95_;
      if (index < 0 || index >= 16) break;
      for (int i = 0; i < 3; i++) {
        const int cellIndex = index * 3 + i;
        if (cellIndex < 48) {
          bms.mv[cellIndex] = (payload[1 + 2 * i] << 8) | payload[2 + 2 * i];
        }
      }
      if (!f95_[index]) {
        f95_[index] = true;
        frames95_++;
      }
      break;
    }
    case 0x98:
      memcpy(bms.alerts, payload, sizeof(bms.alerts));
      bms.alertsValid = true;
      break;
    default:
      break;
  }
}

void DalyParser::feed(const uint8_t* data, size_t length) {
  if (!length) return;
  if (accLen_ + length > sizeof(acc_)) accLen_ = 0;
  memcpy(acc_ + accLen_, data, length);
  accLen_ += length;

  while (accLen_ >= 13) {
    if (acc_[0] != 0xA5 || !checksumOk(acc_)) {
      memmove(acc_, acc_ + 1, accLen_ - 1);
      accLen_--;
      continue;
    }
    parseFrame(acc_);
    memmove(acc_, acc_ + 13, accLen_ - 13);
    accLen_ -= 13;
  }
}
