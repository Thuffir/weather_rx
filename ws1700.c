/***********************************************************************************************************************
 *
 * Wireless Weather Station Receiver / Decoder for Raspberry Pi
 *
 * (C) 2015 Gergely Budai, Jens Hoffmann
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
#ifdef MODULE_WS1700_ENABLE

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/time.h>
#include "types.h"
#include "DecodePulseSpace.h"

#ifndef ANALOG_FILTER

// Pulse length in us
#define PULSE_LENGTH       530
// Space length for bit ZERO in us
#define ZERO_LENGTH       1855
// Space length for bit ONE in us
#define ONE_LENGTH        3975

#else // ANALOG_FILTER
// The Analog filter alters the pulse / space timings

// Pulse length in us
#define PULSE_LENGTH       662
// Space length for bit ZERO in us
#define ZERO_LENGTH       1780
// Space length for bit ONE in us
#define ONE_LENGTH        3850

#endif // ANALOG_FILTER

// Signal timing tolerance
#define TOLERANCE          200

// Suppress identical messages within this timeframe in uS
#define SUPPRESS_TIME     1000000

// Decoded data
typedef struct {
  uint8_t id;
  uint8_t battery;
  uint8_t txMode;
  uint8_t channel;
  int16_t temperature;
  uint8_t humidity;
  uint32_t timeStamp;
} Ws1700Data;

/***********************************************************************************************************************
 * Ws1700 Message Decoder
 **********************************************************************************************************************/
static bool Ws1700Decode(Ws1700Data *data, BitType bit)
{
  // Preamble bits
  static const uint8_t preamble[] = {0, 1, 0, 1};
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
      memset(data, 0, sizeof(Ws1700Data));
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
    // ID [4 .. 11]
    if((bitNr >= 4) && (bitNr <= 11)) {
      data->id = (data->id << 1) | bit;
    }
    // Battery [12]
    else if(bitNr == 12) {
      data->battery = bit;
    }
    // TX Mode [13]
    else if(bitNr == 13) {
      data->txMode = bit;
    }
    // Channel [14..15]
    else if((bitNr >= 14) && (bitNr <= 15)) {
      data->channel = (data->channel << 1) | bit;
    }
    // Temperature [16 .. 27]
    else if((bitNr >= 16) && (bitNr <= 27)) {
      data->temperature = (data->temperature << 1) | bit;
    }
    // Humidity [28..35]
    else if((bitNr >= 28) && (bitNr <= 35)) {
      data->humidity = (data->humidity << 1) | bit;
    }

    // Check if we have received everything
    if(bitNr == 35) {
      // Record reception Timestamp
      struct timeval tv;
      gettimeofday(&tv, NULL);
      data->timeStamp = (tv.tv_sec * 1000000) + tv.tv_usec;
      // Make the 12 bit temperature a 16 bit value
      if(data->temperature & 0x800) {
        data->temperature |= 0xF000;
      }
      retval = true;
    }


    // Increment bit pointer
    bitNr++;
    // But not more than 36 Bits
    if(bitNr > 36) {
      bitNr = 0;
    }
  } while(reCheck);

  exit:
  return retval;
}

/***********************************************************************************************************************
 * Process Bits for WS1700
 **********************************************************************************************************************/
void Ws1700Process(uint32_t pulseLength)
{
  // Bit decoder context
  static PulseSpaceContext bitDecoderCtx = {
    .pulseMin = PULSE_LENGTH - TOLERANCE,
    .pulseMax = PULSE_LENGTH + TOLERANCE,
    .zeroMin  = ZERO_LENGTH  - TOLERANCE,
    .zeroMax  = ZERO_LENGTH  + TOLERANCE,
    .oneMin   = ONE_LENGTH   - TOLERANCE,
    .oneMax   = ONE_LENGTH   + TOLERANCE,
    .state = Idle,
    .inStream = 0
  };
  // Decoded data and the previous one
  static Ws1700Data data, prevData = { 0 };

  // Decode Messages
  if(Ws1700Decode(&data, DecodePulseSpace(&bitDecoderCtx, pulseLength))) {
    // Check if a message is a duplicate of a last one
    if((data.id != prevData.id) || (data.battery != prevData.battery) ||
        (data.channel != prevData.channel) ||
        (data.txMode != prevData.txMode) ||
        (data.temperature != prevData.temperature) ||
        (data.humidity != prevData.humidity) ||
        ((data.timeStamp - prevData.timeStamp) >= SUPPRESS_TIME)) {
      // No duplicate, convert temperature
      double temperature;
      temperature = data.temperature / 10.0;
      // And Print
      printf("ws1700 %u %u %u %u %.1f %u\n",data.id, data.channel, data.battery, data.txMode, temperature, data.humidity);
      fflush(stdout);
    }
    // Remember old message
    prevData = data;
  }
}

#endif // MODULE_WS1700_ENABLE
