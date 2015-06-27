/***********************************************************************************************************************
 *
 * Wireless Weather Station Receiver / Decoder for Raspberry Pi
 *
 * (C) 2015 Gergely Budai
 *
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * For more information, please refer to <http://unlicense.org/>
 *
 **********************************************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// Bit length in uS
#define BIT_LENGTH                2000
// +- Bit length tolerance in uS
#define BIT_LENGTH_TOLERANCE       200

// Suppress identical messages within this timeframe in uS
#define SUPPRESS_TIME          1000000

// Calculated Thresholds for zeros and ones
#define BIT_LENGTH_THRES_LOW      (BIT_LENGTH - BIT_LENGTH_TOLERANCE)
#define BIT_LENGTH_THRES_HIGH     (BIT_LENGTH + BIT_LENGTH_TOLERANCE)
#define HALFBIT_LENGTH_THRES_LOW  ((BIT_LENGTH / 2) - BIT_LENGTH_TOLERANCE)
#define HALFBIT_LENGTH_THRES_HIGH ((BIT_LENGTH / 2) + BIT_LENGTH_TOLERANCE)

#define IS_BIT_FULL_LENGTH(length)  ((length >= BIT_LENGTH_THRES_LOW) && (length <= BIT_LENGTH_THRES_HIGH))
#define IS_BIT_HALF_LENGTH(length)  ((length >= HALFBIT_LENGTH_THRES_LOW) && (length <= HALFBIT_LENGTH_THRES_HIGH))

// LIRC device file
#define LIRC_DEV                  "/dev/lirc_rpi"

// Bit definitions
#define BIT_ZERO                  0
#define BIT_ONE                   1
#define BIT_IN_STREAM             2
#define BIT_VALID                 4
typedef uint8_t BitType;

// LIRC Device file descriptor
int lircDev;

// Decoded WT440H Message
typedef struct {
  uint8_t houseCode;
  uint8_t channel;
  uint8_t status;
  uint8_t batteryLow;
  uint8_t humidity;
  uint8_t tempInteger;
  uint8_t tempFraction;
  uint8_t sequneceNr;
  uint32_t timeStamp;
} WT440hDataType;

/***********************************************************************************************************************
 *
 * Biphase Mark Decoder
 *
 **********************************************************************************************************************/
static BitType BiphaseMarkDecode(uint32_t pulseLength)
{
  // Internal State for bit recognition
  static enum {
    Neutral,
    HalfOneReceived
  } state = Neutral;
  static BitType lastBit = 0;
  // Return Value
  BitType bit = 0;

  // Bit recognition state machine
  switch(state) {
    // Neutral state
    case Neutral: {
      // Check if zero received
      if(IS_BIT_FULL_LENGTH(pulseLength)) {
        // Full bit length, zero received
        bit = BIT_ZERO | BIT_VALID;
      }
      // else check if the first half of a one received
      else if(IS_BIT_HALF_LENGTH(pulseLength)) {
        // Half bit length, first half of a zero received
        state = HalfOneReceived;
      }
      // No valid bit part received
      else {
        lastBit = 0;
      }
    }
    break;

    // First half of a One already received
    case HalfOneReceived: {
      // Check for second half
      if(IS_BIT_HALF_LENGTH(pulseLength)) {
        // Second half of a one received
        bit = BIT_ONE | BIT_VALID;
      }
      // Not the second half of a one, but maybe a valid Zero after an invalid one
      else if(IS_BIT_FULL_LENGTH(pulseLength)) {
        // Full bit length, zero received
        bit = BIT_ZERO | BIT_VALID;
      }
      // No valid bit part received
      else {
        lastBit = 0;
      }
      state = Neutral;
    }
    break;

    // Invalid state (should not happen)
    default: {
      perror("Invalid state");
      exit(EXIT_FAILURE);
    }
    break;
  }

  // Check if we have a valid bit
  if(bit & BIT_VALID) {
    // And mark if it's part of a bit stream
    if(lastBit & BIT_VALID) {
      bit |= BIT_IN_STREAM;
    }
    lastBit = bit;
  }

  return bit;
}

/***********************************************************************************************************************
 * Init functions
 **********************************************************************************************************************/
static void Init(void)
{
  // Open device file for reading
  lircDev = open(LIRC_DEV, O_RDONLY);
  if(lircDev == -1) {
    perror("open()");
    exit(EXIT_FAILURE);
  }
}

///***********************************************************************************************************************
// * Decode received bits into a WT440H Message
// **********************************************************************************************************************/
//static uint8_t RxData(WT440hDataType *data, BitType bit)
//{
//  // Preamble bits
//  static const uint8_t preamble[] = {1, 1, 0, 0};
//  // Bit number counter
//  static uint8_t bitNr = 0;
//  // Previous bit timestamp and bit length
//  uint32_t prevTimeStamp = 0, bitLength;
//
//  // Clear all data at the beginning
//  if(bitNr == 0) {
//    memset(&data, 0, sizeof(data));
//  }
//  else {
//    // Only the first bit can have the bit timeout flag set
//    if(bit & BIT_TIMEOUT) {
//      // printf("Bit timeout bit %u\n", bitNr);
//      bitNr = 0;
//      goto exit;
//    }
//  }
//  // Remove flags
//  bit &= BIT_ONE;
//
//  // Preamble [0 .. 3]
//  if(bitNr <= 3) {
//    if(bit != preamble[bitNr]) {
//      // printf("Wrong preamble %u at bit %u\n", bitInfo.bit, bitNr);
//      bitNr = 0;
//      checksum = 0;
//      memset(&data, 0, sizeof(data));
//      goto exit;
//    }
//  }
//  // Housecode [4 .. 7]
//  else if((bitNr >= 4) && (bitNr <= 7)) {
//    data.houseCode = (data.houseCode << 1) | bit;
//  }
//  // Channel [8 .. 9]
//  else if((bitNr >= 8) && (bitNr <= 9)) {
//    data.channel = (data.channel << 1) | bit;
//  }
//  // Status [10 .. 11]
//  else if((bitNr >= 10) && (bitNr <= 11)) {
//    data.status = (data.status << 1) | bit;
//  }
//  // Battery Low [12]
//  else if(bitNr == 12) {
//    data.batteryLow = bit;
//  }
//  // Humidity [13 .. 19]
//  else if((bitNr >= 13) && (bitNr <= 19)) {
//    data.humidity = (data.humidity << 1) | bit;
//  }
//  // Temperature (Integer part) [20 .. 27]
//  else if((bitNr >= 20) && (bitNr <= 27)) {
//    data.tempInteger = (data.tempInteger << 1) | bit;
//  }
//  // Temperature (Fractional part) [28 .. 31]
//  else if((bitNr >= 28) && (bitNr <= 31)) {
//    data.tempFraction = (data.tempFraction << 1) | bit;
//  }
//  // Message Sequence [32 .. 33]
//  else if((bitNr >= 32) && (bitNr <= 33)) {
//    data.sequneceNr = (data.sequneceNr << 1) | bit;
//  }
//
//  // Update checksum
//  checksum ^= bit << (bitNr & 1);
//  // and check checksum if appropriate
//  if(bitNr == 35) {
//    // If checksum correct
//    if(checksum == 0) {
//      // Record reception Timestamp
//      data.timeStamp = timeStamp;
//    }
//    // Checksum error
//    else {
//      // printf("Checksum error\n");
//      bitNr = 0;
//      checksum = 0;
//      memset(&data, 0, sizeof(data));
//      goto exit;
//    }
//  }
//
//  // Increment bit pointer
//  bitNr++;
//
//  exit:
//  return data;
//}
//
///***********************************************************************************************************************
// * Main
// **********************************************************************************************************************/
//int main(void)
//{
//  // Decoded WT440H data and the previous one
//  WT440hDataType data, prevData = { 0 };
//
//  // Do init stuff
//  Init();
//
//  // Receive and decode messages
//  while(1) {
//    // Wait for Message
//    data = RxData();
//    // Check if a message is a duplicate of a last one
//    if((data.houseCode != prevData.houseCode) || (data.channel != prevData.channel) ||
//        (data.status != prevData.status) || (data.batteryLow != prevData.batteryLow) ||
//        (data.humidity != prevData.humidity) || (data.tempInteger != prevData.tempInteger) ||
//        (data.tempFraction != prevData.tempFraction) || ((data.timeStamp - prevData.timeStamp) >= SUPPRESS_TIME)) {
//      // If no duplicate, print
//      printf("%u %u %u %u %u %.1f\n", data.houseCode, data.channel + 1, data.status, data.batteryLow, data.humidity,
//        ((double)data.tempInteger - (double)50) + ((double)data.tempFraction / (double)16));
//      fflush(stdout);
//    }
//    // Remember old message
//    prevData = data;
//  }
//
//  return 0;
//}

int main(void)
{
  uint32_t lircData;
  BitType bit;
  Init();

  while(1) {
    // Read Lirc data
    if(read(lircDev, &lircData, sizeof(lircData)) != sizeof(lircData)) {
      printf("read()");
      exit(EXIT_FAILURE);
    }
    lircData &= 0xFFFFFF;
    bit = BiphaseMarkDecode(lircData);
    if(bit & BIT_VALID) {
      printf("%u %u\n", bit & BIT_IN_STREAM, bit & BIT_ONE);
    }
  }

  return 0;
}
