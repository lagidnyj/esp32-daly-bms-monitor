#pragma once

#include <stddef.h>
#include <stdint.h>

namespace BmsModbusControl {

constexpr size_t MAX_TARGET_MAC_LENGTH = 17;
constexpr size_t FRAME_LENGTH = 8;
constexpr size_t WRITE_UINT32_FRAME_LENGTH = 12;

enum class CommandState : uint8_t {
  Empty,
  Queued,
  Sending,
  Acknowledged,
  Verified,
  Mismatch,
  Failed
};

struct Command {
  uint32_t id;
  char targetMac[MAX_TARGET_MAC_LENGTH + 1];
  uint16_t reg;
  uint32_t value;
  bool verifyResult;
};

struct Result {
  uint32_t id;
  CommandState state;
  uint16_t reg;
  uint32_t requestedValue;
  uint32_t actualValue;
  bool hasActualValue;
};

uint16_t crc16(const uint8_t* data, size_t length);
void buildWriteSingleRegister(uint8_t slave, uint16_t reg, uint16_t value,
                              uint8_t out[FRAME_LENGTH]);
void buildWriteUint32(uint8_t slave, uint16_t reg, uint32_t value,
                      uint8_t out[WRITE_UINT32_FRAME_LENGTH]);
void buildReadHoldingRegisters(uint8_t slave, uint16_t startReg, uint16_t count,
                               uint8_t out[FRAME_LENGTH]);
bool hasValidCrc(const uint8_t* frame, size_t length);
bool matchesWriteResponse(const uint8_t request[FRAME_LENGTH],
                          const uint8_t* response, size_t length,
                          uint8_t responseAddress);
bool matchesWriteUint32Response(const uint8_t request[WRITE_UINT32_FRAME_LENGTH],
                                const uint8_t* response, size_t length,
                                uint8_t responseAddress);
bool parseReadHoldingResponse(const uint8_t* response, size_t length,
                              uint8_t responseAddress, uint8_t registerCount,
                              uint16_t* values);

bool enqueue(const char* targetMac, uint16_t reg, uint32_t value,
             bool verifyResult, uint32_t& commandId);
bool takeNextFor(const char* targetMac, Command& command);
void setState(uint32_t commandId, CommandState state,
              bool hasActualValue = false, uint32_t actualValue = 0);
bool getResult(uint32_t commandId, Result& result);
bool hasQueuedCommands();
const char* stateName(CommandState state);

}