#include "BmsModbusControl.h"

#include <string.h>

namespace BmsModbusControl {
namespace {
constexpr size_t QUEUE_CAPACITY = 8;

struct Entry {
  Command command;
  Result result;
  uint32_t order;
  bool occupied;
};

Entry entries[QUEUE_CAPACITY] = {};
uint32_t nextCommandId = 1;
uint32_t nextOrder = 1;

bool normalizeMac(const char* input, char output[MAX_TARGET_MAC_LENGTH + 1]) {
  if (input == nullptr || strlen(input) != MAX_TARGET_MAC_LENGTH) return false;
  for (size_t i = 0; i < MAX_TARGET_MAC_LENGTH; i++) {
    const char ch = input[i];
    if (i == 2 || i == 5 || i == 8 || i == 11 || i == 14) {
      if (ch != ':') return false;
      output[i] = ch;
    } else if (ch >= '0' && ch <= '9') {
      output[i] = ch;
    } else if (ch >= 'a' && ch <= 'f') {
      output[i] = ch;
    } else if (ch >= 'A' && ch <= 'F') {
      output[i] = static_cast<char>(ch - 'A' + 'a');
    } else {
      return false;
    }
  }
  output[MAX_TARGET_MAC_LENGTH] = '\0';
  return true;
}

bool isTerminal(CommandState state) {
  return state == CommandState::Acknowledged || state == CommandState::Verified ||
         state == CommandState::Mismatch || state == CommandState::Failed;
}

Entry* findEntry(uint32_t commandId) {
  for (Entry& entry : entries) {
    if (entry.occupied && entry.command.id == commandId) return &entry;
  }
  return nullptr;
}
}

uint16_t crc16(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 1) ? static_cast<uint16_t>((crc >> 1) ^ 0xA001)
                      : static_cast<uint16_t>(crc >> 1);
    }
  }
  return crc;
}

void buildWriteSingleRegister(uint8_t slave, uint16_t reg, uint16_t value,
                              uint8_t out[FRAME_LENGTH]) {
  out[0] = slave;
  out[1] = 0x06;
  out[2] = static_cast<uint8_t>(reg >> 8);
  out[3] = static_cast<uint8_t>(reg);
  out[4] = static_cast<uint8_t>(value >> 8);
  out[5] = static_cast<uint8_t>(value);
  const uint16_t crc = crc16(out, 6);
  out[6] = static_cast<uint8_t>(crc);
  out[7] = static_cast<uint8_t>(crc >> 8);
}

void buildReadHoldingRegisters(uint8_t slave, uint16_t startReg, uint16_t count,
                               uint8_t out[FRAME_LENGTH]) {
  out[0] = slave;
  out[1] = 0x03;
  out[2] = static_cast<uint8_t>(startReg >> 8);
  out[3] = static_cast<uint8_t>(startReg);
  out[4] = static_cast<uint8_t>(count >> 8);
  out[5] = static_cast<uint8_t>(count);
  const uint16_t crc = crc16(out, 6);
  out[6] = static_cast<uint8_t>(crc);
  out[7] = static_cast<uint8_t>(crc >> 8);
}

void buildWriteUint32(uint8_t slave, uint16_t reg, uint32_t value,
                      uint8_t out[WRITE_UINT32_FRAME_LENGTH]) {
  out[0] = slave;
  out[1] = 0x10;
  out[2] = static_cast<uint8_t>(reg >> 8);
  out[3] = static_cast<uint8_t>(reg);
  out[4] = 0;
  out[5] = 2;
  out[6] = static_cast<uint8_t>(value >> 24);
  out[7] = static_cast<uint8_t>(value >> 16);
  out[8] = static_cast<uint8_t>(value >> 8);
  out[9] = static_cast<uint8_t>(value);
  const uint16_t crc = crc16(out, 10);
  out[10] = static_cast<uint8_t>(crc);
  out[11] = static_cast<uint8_t>(crc >> 8);
}

bool hasValidCrc(const uint8_t* frame, size_t length) {
  if (frame == nullptr || length < 4) return false;
  const uint16_t expected = crc16(frame, length - 2);
  return frame[length - 2] == static_cast<uint8_t>(expected) &&
         frame[length - 1] == static_cast<uint8_t>(expected >> 8);
}

bool matchesWriteResponse(const uint8_t request[FRAME_LENGTH],
           const uint8_t* response, size_t length,
           uint8_t responseAddress) {
  return request != nullptr && response != nullptr && length == FRAME_LENGTH &&
    request[1] == 0x06 && response[0] == responseAddress &&
    hasValidCrc(response, length) &&
    memcmp(response + 1, request + 1, 5) == 0;
}

  bool matchesWriteUint32Response(const uint8_t request[WRITE_UINT32_FRAME_LENGTH],
                  const uint8_t* response, size_t length,
                  uint8_t responseAddress) {
    return request != nullptr && response != nullptr && length == FRAME_LENGTH &&
       request[1] == 0x10 && response[0] == responseAddress &&
       hasValidCrc(response, length) &&
       memcmp(response + 1, request + 1, 5) == 0;
  }

bool parseReadHoldingResponse(const uint8_t* response, size_t length,
                              uint8_t responseAddress, uint8_t registerCount,
                              uint16_t* values) {
  if (response == nullptr || values == nullptr || registerCount == 0 ||
      registerCount > 125 || length != static_cast<size_t>(registerCount) * 2 + 5 ||
      response[0] != responseAddress || response[1] != 0x03 ||
      response[2] != registerCount * 2 || !hasValidCrc(response, length)) {
    return false;
  }
  for (uint8_t index = 0; index < registerCount; index++) {
    values[index] = static_cast<uint16_t>((response[3 + index * 2] << 8) |
                                           response[4 + index * 2]);
  }
  return true;
}

bool enqueue(const char* targetMac, uint16_t reg, uint32_t value,
             bool verifyResult, uint32_t& commandId) {
  char normalizedMac[MAX_TARGET_MAC_LENGTH + 1];
  if (!normalizeMac(targetMac, normalizedMac) ||
    !(((reg == 0x0121 || reg == 0x0122) && value <= 1) ||
      (reg == 0x0116 && value <= 1000) ||
      (reg == 0x0115 && value >= 1 && value <= 6552) ||
      (reg == 0x0109 && value >= 1))) {
    return false;
  }

  Entry* available = nullptr;
  for (Entry& entry : entries) {
    if (!entry.occupied || isTerminal(entry.result.state)) {
      if (available == nullptr || !entry.occupied || entry.order < available->order) {
        available = &entry;
      }
    }
  }
  if (available == nullptr) return false;

  available->command.id = nextCommandId++;
  memcpy(available->command.targetMac, normalizedMac, sizeof(normalizedMac));
  available->command.reg = reg;
  available->command.value = value;
  available->command.verifyResult = verifyResult;
  available->result = {available->command.id, CommandState::Queued, reg, value, 0, false};
  available->order = nextOrder++;
  available->occupied = true;
  commandId = available->command.id;
  return true;
}

bool takeNextFor(const char* targetMac, Command& command) {
  char normalizedMac[MAX_TARGET_MAC_LENGTH + 1];
  if (!normalizeMac(targetMac, normalizedMac)) return false;

  Entry* next = nullptr;
  for (Entry& entry : entries) {
    if (!entry.occupied || entry.result.state != CommandState::Queued ||
        strcmp(entry.command.targetMac, normalizedMac) != 0) {
      continue;
    }
    if (next == nullptr || entry.order < next->order) next = &entry;
  }
  if (next == nullptr) return false;

  command = next->command;
  next->result.state = CommandState::Sending;
  return true;
}

void setState(uint32_t commandId, CommandState state, bool hasActualValue,
              uint32_t actualValue) {
  Entry* entry = findEntry(commandId);
  if (entry == nullptr) return;
  entry->result.state = state;
  entry->result.hasActualValue = hasActualValue;
  entry->result.actualValue = actualValue;
}

bool getResult(uint32_t commandId, Result& result) {
  Entry* entry = findEntry(commandId);
  if (entry == nullptr) return false;
  result = entry->result;
  return true;
}

bool hasQueuedCommands() {
  for (const Entry& entry : entries) {
    if (entry.occupied && entry.result.state == CommandState::Queued) return true;
  }
  return false;
}

const char* stateName(CommandState state) {
  switch (state) {
    case CommandState::Queued: return "queued";
    case CommandState::Sending: return "sending";
    case CommandState::Acknowledged: return "acknowledged";
    case CommandState::Verified: return "verified";
    case CommandState::Mismatch: return "mismatch";
    case CommandState::Failed: return "failed";
    case CommandState::Empty:
    default: return "empty";
  }
}

}