#pragma once

#include <stddef.h>
#include <stdint.h>

#include "BmsTypes.h"

class DalyParser {
public:
  void begin(Bms& bms);
  void resetResponses();
  void clearCommand(uint8_t command);
  bool hasCommand(uint8_t command) const;
  int cellFrameCount() const;
  void feed(const uint8_t* data, size_t length);

private:
  static bool checksumOk(const uint8_t* frame);
  void parseFrame(const uint8_t* frame);

  Bms* current_ = nullptr;
  bool gotCmd_[256] = {};
  bool f95_[16] = {};
  int frames95_ = 0;
  int base95_ = -1;
  uint8_t acc_[256] = {};
  size_t accLen_ = 0;
};
