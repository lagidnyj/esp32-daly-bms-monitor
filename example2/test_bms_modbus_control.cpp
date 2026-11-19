#ifndef ARDUINO
#include "BmsModbusControl.h"

#include <assert.h>
#include <string.h>

using namespace BmsModbusControl;

static void assertFrame(const uint8_t* actual, const uint8_t* expected, size_t length) {
  assert(memcmp(actual, expected, length) == 0);
  assert(hasValidCrc(actual, length));
}

int main() {
  uint8_t frame[FRAME_LENGTH];
  const uint8_t capturedChargeOn[] = {0x81, 0x06, 0x01, 0x21, 0x00, 0x01, 0x06, 0x3C};
  const uint8_t capturedChargeOnReply[] = {0x51, 0x06, 0x01, 0x21, 0x00, 0x01, 0x15, 0xAC};
  const uint8_t capturedChargeOff[] = {0x81, 0x06, 0x01, 0x21, 0x00, 0x00, 0xC7, 0xFC};
  const uint8_t capturedDischargeOn[] = {0x81, 0x06, 0x01, 0x22, 0x00, 0x01, 0xF6, 0x3C};
  const uint8_t capturedDischargeOff[] = {0x81, 0x06, 0x01, 0x22, 0x00, 0x00, 0x37, 0xFC};
  const uint8_t capturedSoc[] = {0x81, 0x06, 0x01, 0x16, 0x03, 0xDE, 0xF6, 0x9A};
  const uint8_t capturedSocRead[] = {0x51, 0x03, 0x02, 0x03, 0xDE, 0xF8, 0xE0};
  const uint8_t capturedRated[] = {0x81, 0x10, 0x01, 0x09, 0x00, 0x02, 0x00, 0x02, 0xE6, 0x30, 0x21, 0x52};
  const uint8_t capturedRatedReply[] = {0x51, 0x10, 0x01, 0x09, 0x00, 0x02, 0x9C, 0x66};
  const uint8_t capturedActualReply[] = {0x51, 0x10, 0x01, 0x0B, 0x00, 0x02, 0x3D, 0xA6};
  const uint8_t capturedRatedRead[] = {0x51, 0x03, 0x04, 0x00, 0x02, 0xE6, 0x30, 0x41, 0x82};
  const uint8_t capturedSleep[] = {0x81, 0x06, 0x01, 0x15, 0x0E, 0x10, 0x83, 0x9E};
  const uint8_t capturedSleepReply[] = {0x51, 0x06, 0x01, 0x15, 0x0E, 0x10, 0x90, 0x0E};
  const uint8_t capturedSleepRead[] = {0x51, 0x03, 0x02, 0x0E, 0x10, 0x7D, 0xE4};
  const uint8_t chargeOn[] = {0x51, 0x06, 0x01, 0x21, 0x00, 0x01, 0x15, 0xAC};
  const uint8_t chargeOff[] = {0x51, 0x06, 0x01, 0x21, 0x00, 0x00, 0xD4, 0x6C};
  const uint8_t dischargeOn[] = {0xD2, 0x06, 0x01, 0x22, 0x00, 0x01, 0xFA, 0x5F};
  const uint8_t dischargeOff[] = {0xD2, 0x06, 0x01, 0x22, 0x00, 0x00, 0x3B, 0x9F};
  const uint8_t soc845[] = {0x51, 0x06, 0x01, 0x16, 0x03, 0x4D, 0xA5, 0x67};
  const uint8_t verifyMos[] = {0xD2, 0x03, 0x00, 0x52, 0x00, 0x02, 0x76, 0x79};
  const uint8_t verifyMosBle[] = {0x81, 0x03, 0x00, 0x52, 0x00, 0x02, 0x7A, 0x1A};

  buildWriteSingleRegister(0x51, 0x0121, 1, frame);
  assertFrame(frame, chargeOn, sizeof(chargeOn));
  buildWriteSingleRegister(0x51, 0x0121, 0, frame);
  assertFrame(frame, chargeOff, sizeof(chargeOff));
  buildWriteSingleRegister(0xD2, 0x0122, 1, frame);
  assertFrame(frame, dischargeOn, sizeof(dischargeOn));
  buildWriteSingleRegister(0xD2, 0x0122, 0, frame);
  assertFrame(frame, dischargeOff, sizeof(dischargeOff));
  buildWriteSingleRegister(0x51, 0x0116, 845, frame);
  assertFrame(frame, soc845, sizeof(soc845));
  buildReadHoldingRegisters(0xD2, 0x0052, 2, frame);
  assertFrame(frame, verifyMos, sizeof(verifyMos));
  buildReadHoldingRegisters(0x81, 0x0052, 2, frame);
  assertFrame(frame, verifyMosBle, sizeof(verifyMosBle));

  buildWriteSingleRegister(0x81, 0x0121, 1, frame);
  assertFrame(frame, capturedChargeOn, sizeof(capturedChargeOn));
  assert(matchesWriteResponse(frame, capturedChargeOnReply,
                              sizeof(capturedChargeOnReply), 0x51));
  assert(!matchesWriteResponse(frame, capturedChargeOnReply,
                               sizeof(capturedChargeOnReply), 0x81));
  uint8_t alteredReply[FRAME_LENGTH];
  memcpy(alteredReply, capturedChargeOnReply, sizeof(alteredReply));
  alteredReply[5] = 0;
  assert(!matchesWriteResponse(frame, alteredReply, sizeof(alteredReply), 0x51));
  memcpy(alteredReply, capturedChargeOnReply, sizeof(alteredReply));
  alteredReply[7] ^= 1;
  assert(!matchesWriteResponse(frame, alteredReply, sizeof(alteredReply), 0x51));
  buildWriteSingleRegister(0x51, 0x0122, 1, alteredReply);
  assert(hasValidCrc(alteredReply, sizeof(alteredReply)));
  assert(!matchesWriteResponse(frame, alteredReply, sizeof(alteredReply), 0x51));
  assert(!matchesWriteResponse(frame, capturedChargeOnReply, 7, 0x51));
  buildWriteSingleRegister(0x81, 0x0121, 0, frame);
  assertFrame(frame, capturedChargeOff, sizeof(capturedChargeOff));
  assert(!matchesWriteResponse(frame, capturedChargeOnReply,
                               sizeof(capturedChargeOnReply), 0x51));
  buildWriteSingleRegister(0x81, 0x0122, 1, frame);
  assertFrame(frame, capturedDischargeOn, sizeof(capturedDischargeOn));
  buildWriteSingleRegister(0x81, 0x0122, 0, frame);
  assertFrame(frame, capturedDischargeOff, sizeof(capturedDischargeOff));
  buildWriteSingleRegister(0x81, 0x0116, 990, frame);
  assertFrame(frame, capturedSoc, sizeof(capturedSoc));

  uint8_t capacityFrame[WRITE_UINT32_FRAME_LENGTH];
  buildWriteUint32(0x81, 0x0109, 190000, capacityFrame);
  assertFrame(capacityFrame, capturedRated, sizeof(capturedRated));
  assert(matchesWriteUint32Response(capacityFrame, capturedRatedReply,
                                   sizeof(capturedRatedReply), 0x51));
  assert(!matchesWriteUint32Response(capacityFrame, capturedRatedReply,
                                    sizeof(capturedRatedReply), 0x81));
  assert(hasValidCrc(capturedActualReply, sizeof(capturedActualReply)));
  assert(!matchesWriteUint32Response(capacityFrame, capturedActualReply,
                                    sizeof(capturedActualReply), 0x51));
  uint8_t corruptCapacityReply[FRAME_LENGTH];
  memcpy(corruptCapacityReply, capturedRatedReply, sizeof(corruptCapacityReply));
  corruptCapacityReply[3] ^= 1;
  assert(!matchesWriteUint32Response(capacityFrame, corruptCapacityReply,
                                    sizeof(corruptCapacityReply), 0x51));
  buildWriteSingleRegister(0x81, 0x0115, 3600, frame);
  assertFrame(frame, capturedSleep, sizeof(capturedSleep));
  assert(matchesWriteResponse(frame, capturedSleepReply,
                              sizeof(capturedSleepReply), 0x51));

  uint16_t readValues[2] = {};
  assert(parseReadHoldingResponse(capturedRatedRead, sizeof(capturedRatedRead),
                                  0x51, 2, readValues));
  assert(((static_cast<uint32_t>(readValues[0]) << 16) | readValues[1]) == 190000);
  assert(parseReadHoldingResponse(capturedSleepRead, sizeof(capturedSleepRead),
                                  0x51, 1, readValues));
  assert(readValues[0] == 3600);
  assert(parseReadHoldingResponse(capturedSocRead, sizeof(capturedSocRead),
                                  0x51, 1, readValues));
  assert(readValues[0] == 990);
  uint8_t mosRead[] = {0x51, 0x03, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
  const uint16_t mosReadCrc = crc16(mosRead, sizeof(mosRead) - 2);
  mosRead[7] = static_cast<uint8_t>(mosReadCrc);
  mosRead[8] = static_cast<uint8_t>(mosReadCrc >> 8);
  assert(parseReadHoldingResponse(mosRead, sizeof(mosRead), 0x51, 2, readValues));
  assert(readValues[0] == 1 && readValues[1] == 0);
  assert(!parseReadHoldingResponse(capturedSocRead, sizeof(capturedSocRead),
                                   0x81, 1, readValues));
  assert(!parseReadHoldingResponse(capturedSocRead, sizeof(capturedSocRead),
                                   0x51, 2, readValues));
  uint8_t corruptRead[sizeof(capturedSocRead)];
  memcpy(corruptRead, capturedSocRead, sizeof(corruptRead));
  corruptRead[4] ^= 1;
  assert(!parseReadHoldingResponse(corruptRead, sizeof(corruptRead),
                                   0x51, 1, readValues));

  uint32_t commandId = 0;
  assert(enqueue("AA:BB:CC:DD:EE:FF", 0x0121, 1, true, commandId));
  assert(!enqueue("not-a-mac", 0x0121, 1, true, commandId));
  assert(!enqueue("aa:bb:cc:dd:ee:ff", 0x0121, 2, true, commandId));

  Command command = {};
  assert(!takeNextFor("00:11:22:33:44:55", command));
  assert(takeNextFor("aa:bb:cc:dd:ee:ff", command));
  assert(command.reg == 0x0121 && command.value == 1 && command.verifyResult);
  assert(strcmp(command.targetMac, "aa:bb:cc:dd:ee:ff") == 0);

  Result result = {};
  assert(getResult(commandId, result));
  assert(result.state == CommandState::Sending);
  setState(commandId, CommandState::Verified, true, 1);
  assert(getResult(commandId, result));
  assert(result.state == CommandState::Verified && result.hasActualValue &&
         result.actualValue == 1);

    assert(enqueue("aa:bb:cc:dd:ee:ff", 0x0116, 0, true, commandId));
    assert(takeNextFor("aa:bb:cc:dd:ee:ff", command));
    assert(command.reg == 0x0116 && command.value == 0);
    setState(commandId, CommandState::Verified, true, 0);
    assert(enqueue("aa:bb:cc:dd:ee:ff", 0x0116, 1000, true, commandId));
    assert(takeNextFor("aa:bb:cc:dd:ee:ff", command));
    assert(command.reg == 0x0116 && command.value == 1000);
    setState(commandId, CommandState::Verified, true, 1000);
    assert(!enqueue("aa:bb:cc:dd:ee:ff", 0x0116, 1001, true, commandId));
    assert(!enqueue("aa:bb:cc:dd:ee:ff", 0x0119, 1, true, commandId));
  assert(enqueue("aa:bb:cc:dd:ee:ff", 0x0109, 190000, true, commandId));
  assert(takeNextFor("aa:bb:cc:dd:ee:ff", command));
  assert(command.reg == 0x0109 && command.value == 190000);
  setState(commandId, CommandState::Verified, true, 190000);
  assert(getResult(commandId, result));
  assert(result.actualValue == 190000 && result.requestedValue == 190000);
  assert(!enqueue("aa:bb:cc:dd:ee:ff", 0x0109, 0, true, commandId));
  assert(!enqueue("aa:bb:cc:dd:ee:ff", 0x010B, 190000, true, commandId));
  assert(enqueue("aa:bb:cc:dd:ee:ff", 0x0109, UINT32_MAX, true, commandId));
  assert(takeNextFor("aa:bb:cc:dd:ee:ff", command));
  assert(command.value == UINT32_MAX);
  setState(commandId, CommandState::Verified, true, UINT32_MAX);
  assert(getResult(commandId, result));
  assert(result.actualValue == UINT32_MAX && result.requestedValue == UINT32_MAX);
  assert(enqueue("aa:bb:cc:dd:ee:ff", 0x0115, 6552, true, commandId));
  assert(takeNextFor("aa:bb:cc:dd:ee:ff", command));
  assert(command.reg == 0x0115 && command.value == 6552);
  setState(commandId, CommandState::Verified, true, 6552);
  assert(!enqueue("aa:bb:cc:dd:ee:ff", 0x0115, 0, true, commandId));
  assert(!enqueue("aa:bb:cc:dd:ee:ff", 0x0115, 6553, true, commandId));
  return 0;
}
#endif