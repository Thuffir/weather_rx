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

#include "config.h"
#ifdef MODULE_WT440H_ENABLE

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/time.h>
#include "types.h"

#ifndef ANALOG_FILTER
// Bit length in uS
#define BIT_LENGTH                2000
// +- Bit length tolerance in uS
#define BIT_LENGTH_TOLERANCE       200

// Calculated Thresholds for zeros and ones
#define BIT_LENGTH_THRES_LOW      (BIT_LENGTH - BIT_LENGTH_TOLERANCE)
#define BIT_LENGTH_THRES_HIGH     (BIT_LENGTH + BIT_LENGTH_TOLERANCE)
#define HALFBIT_LENGTH_THRES_LOW  ((BIT_LENGTH / 2) - BIT_LENGTH_TOLERANCE)
#define HALFBIT_LENGTH_THRES_HIGH ((BIT_LENGTH / 2) + BIT_LENGTH_TOLERANCE)

#else // ANALOG_FILTER
// Somewhat relaxed thresholds for the analog filter
#define BIT_LENGTH_THRES_LOW      1500
#define BIT_LENGTH_THRES_HIGH     2400
#define HALFBIT_LENGTH_THRES_LOW   500
#define HALFBIT_LENGTH_THRES_HIGH 1400
#endif // ANALOG_FILTER

// Bit length check macros
#define IS_BIT_FULL_LENGTH(length)  ((length >= BIT_LENGTH_THRES_LOW) && (length <= BIT_LENGTH_THRES_HIGH))
#define IS_BIT_HALF_LENGTH(length)  ((length >= HALFBIT_LENGTH_THRES_LOW) && (length <= HALFBIT_LENGTH_THRES_HIGH))

// Search for identical messages within this timeframe in uS
#define DUPLICATE_TIME         1000000

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
  uint8_t checksum;
  uint32_t timeStamp;
} WT440hDataType;

/***********************************************************************************************************************
 * Biphase Mark Decoder
 **********************************************************************************************************************/
static BitType BiphaseMarkDecode(uint32_t pulseLength)
{
  // We will count half bits here
  static uint8_t halfBits = 0;
  // Last bit valid or not
  static BitType lastBit = 0;
  // Return Value
  BitType bit = 0;

  // Low Pass Filter
  if(pulseLength < HALFBIT_LENGTH_THRES_LOW ) {
    goto exit;
  }

  // Check if we have a Zero
  if(IS_BIT_FULL_LENGTH(pulseLength)) {
    // Signal that we have received a zero
    bit = BIT_ZERO | BIT_VALID;
    // and reset halfbit counter
    halfBits = 0;
  }
  // Or one half of a One
  else if(IS_BIT_HALF_LENGTH(pulseLength)) {
    // Count bit halves, and check if we have received all of them
    if((++halfBits) >= 2) {
      // if all received, signal One
      bit = BIT_ONE | BIT_VALID;
      // and reset halfbit counter
      halfBits = 0;
    }
  }
  // we have something invalid
  else {
    halfBits = 0;
    lastBit = 0;
  }

  // Chek if we have a valid bit
  if(bit & BIT_VALID) {
    // And mark if it's part of a bit stream
    if(lastBit & BIT_VALID) {
      bit |= BIT_IN_STREAM;
    }
    lastBit = bit;
  }

  exit:
  return bit;
}

/***********************************************************************************************************************
 * Decode received bits into a WT440H Message
 **********************************************************************************************************************/
static bool WT440hDecode(WT440hDataType *data, BitType bit)
{
  // Preamble bits
  static const uint8_t preamble[] = {1, 1, 0, 0};
  // Bit number counter
  static uint8_t bitNr = 0;
  // Return value
  bool retval = false;
  // Recheck some bits
  bool reCheck;

  // Only process valid bits
  if(!(bit & BIT_VALID)) {
    goto exit;
  }

  do {
    // Only Recheck once
    reCheck = false;

    // Clear all data at the beginning
    if(bitNr == 0) {
      memset(data, 0, sizeof(WT440hDataType));
    }
    else {
      // All bits except the first must be in a bit stream
      if(!(bit & BIT_IN_STREAM)) {
//        printf("Bit not in stream: %u\n", bitNr);
        bitNr = 0;
        // Check again this bit, maybe it's the start of a new telegram
        reCheck = true;
        continue;
      }
    }
    // Remove flags
    bit &= BIT_ONE;

    // Preamble [0 .. 3]
    if(bitNr <= 3) {
      if(bit != preamble[bitNr]) {
        //      printf("Wrong preamble %u at bit %u\n", bit, bitNr);
        bitNr = 0;
        goto exit;
      }
    }
    // Housecode [4 .. 7]
    else if((bitNr >= 4) && (bitNr <= 7)) {
      data->houseCode = (data->houseCode << 1) | bit;
    }
    // Channel [8 .. 9]
    else if((bitNr >= 8) && (bitNr <= 9)) {
      data->channel = (data->channel << 1) | bit;
    }
    // Status [10 .. 11]
    else if((bitNr >= 10) && (bitNr <= 11)) {
      data->status = (data->status << 1) | bit;
    }
    // Battery Low [12]
    else if(bitNr == 12) {
      data->batteryLow = bit;
    }
    // Humidity [13 .. 19]
    else if((bitNr >= 13) && (bitNr <= 19)) {
      data->humidity = (data->humidity << 1) | bit;
    }
    // Temperature (Integer part) [20 .. 27]
    else if((bitNr >= 20) && (bitNr <= 27)) {
      data->tempInteger = (data->tempInteger << 1) | bit;
    }
    // Temperature (Fractional part) [28 .. 31]
    else if((bitNr >= 28) && (bitNr <= 31)) {
      data->tempFraction = (data->tempFraction << 1) | bit;
    }
    // Message Sequence [32 .. 33]
    else if((bitNr >= 32) && (bitNr <= 33)) {
      data->sequneceNr = (data->sequneceNr << 1) | bit;
    }

    // Update checksum
    data->checksum ^= bit << (bitNr & 1);
    // and check checksum if appropriate
    if(bitNr == 35) {
      // If checksum correct
      if(data->checksum == 0) {
        // Record reception Timestamp
        struct timeval tv;
        gettimeofday(&tv, NULL);
        data->timeStamp = (tv.tv_sec * 1000000) + tv.tv_usec;
        retval = true;
      }
      // Checksum error
      else {
//        printf("Checksum error\n");
      }
    }

    // Increment bit pointer
    bitNr++;
    // But not more than 36 Bits
    if(bitNr > 35) {
      bitNr = 0;
    }
  } while(reCheck);

  exit:
  return retval;
}

/***********************************************************************************************************************
 * Check if two messages are equal
 **********************************************************************************************************************/
static bool WT440hIsMessageEqual(WT440hDataType *msg1, WT440hDataType *msg2)
{
  return(
      (msg1->houseCode    == msg2->houseCode)    &&
      (msg1->channel      == msg2->channel)      &&
      (msg1->status       == msg2->status)       &&
      (msg1->batteryLow   == msg2->batteryLow)   &&
      (msg1->humidity     == msg2->humidity)     &&
      (msg1->tempInteger  == msg2->tempInteger)  &&
      (msg1->tempFraction == msg2->tempFraction) &&
      // Messages can only be equal within the duplicate timeframe
      ((msg1->timeStamp - msg2->timeStamp) < DUPLICATE_TIME)
      );
}

/***********************************************************************************************************************
 * Process Bits for WT440H
 **********************************************************************************************************************/
void WT440hProcess(uint32_t lircData)
{
  // Decoded WT440H data and the previous one
  static WT440hDataType data, prevData = { 0 };
  // We will lock on one successful message duplicate
  static bool lock = false;

  // WT440H Messages
  if(WT440hDecode(&data, BiphaseMarkDecode(lircData))) {
    // Check if actual and previous messages are equal
    bool equal = WT440hIsMessageEqual(&data, &prevData);
    // If messages are different
    if(!equal) {
      // Release lock
      lock = false;
    }
    // Check for two successive duplicate messages
    if(!lock && equal) {
      double temperature;
      // Set lock
      lock = true;
      // Convert temperature integer part (off by 50 degrees)
      temperature = data.tempInteger - 50.0;
      // Convert friction part
      temperature += data.tempFraction / 16.0;

      // And Print
      printf("wt440h %u %u %u %u %u %.1f\n", data.houseCode, data.channel + 1, data.status, data.batteryLow,
        data.humidity, temperature);
      fflush(stdout);
    }
    // Remember old message
    prevData = data;
  }
}

#endif // MODULE_WT440H_ENABLE
