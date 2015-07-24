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
#ifdef MODULE_AURIOL_ENABLE

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/time.h>
#include "types.h"
#include "DecodePulseSpace.h"

#ifndef ANALOG_FILTER

// Pulse length in us
#define PULSE_LENGTH       500
// Space length for bit ZERO in us
#define ZERO_LENGTH       2000
// Space length for bit ONE in us
#define ONE_LENGTH        4000

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
  uint8_t status;
  uint8_t button;
  int16_t temperature;
  uint8_t humidity;
  uint8_t checksum;
  uint32_t timeStamp;
} AuriolData;

/***********************************************************************************************************************
 * Auriol Message Decoder
 **********************************************************************************************************************/
static bool AuriolDecode(AuriolData *data, BitType bit)
{
  // Bit number counter
  static uint8_t bitNr = 0;
  // Checksum calculation
  static uint8_t checksum = 0;
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
      memset(data, 0, sizeof(AuriolData));
      data->checksum = 0xF;
      checksum = 0;
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

    // ID [0 .. 7]
    if(bitNr <= 7) {
      data->id = (data->id >> 1) | (bit << 7);
    }
    // Battery [8]
    else if(bitNr == 8) {
      data->battery = bit;
    }
    // Status [9 .. 10]
    else if((bitNr >= 9) && (bitNr <= 10)) {
      data->status = (data->status >> 1) | (bit << 1);
    }
    // Button [11]
    else if(bitNr == 11) {
      data->button = bit;
    }
    // Temperature [12 .. 23]
    else if((bitNr >= 12) && (bitNr <= 23)) {
      data->temperature = (data->temperature >> 1) | (bit << 11);
    }
    // Humidity [24 .. 31]
    else if((bitNr >= 24) && (bitNr <= 31)) {
      data->humidity = (data->humidity >> 1) | (bit << 7);
    }

    // Update checksum
    checksum = (checksum >> 1) | (bit << 3);
    if(((bitNr + 1) & 3) == 0) {
      data->checksum = (data->checksum - checksum) & 0xF;
    }

    // and check checksum if appropriate
    if(bitNr == 35) {
      // If checksum and packet type correct
      if((data->checksum == 0) && (data->status != 3)) {
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
 * Process Bits for Auriol
 **********************************************************************************************************************/
void AuriolProcess(uint32_t pulseLength)
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
  // Decoded Auriol data and the previous one
  static AuriolData data, prevData = { 0 };

  // Auriol Messages
  if(AuriolDecode(&data, DecodePulseSpace(&bitDecoderCtx, pulseLength))) {
    // Check if a message is a duplicate of a last one
    if((data.id != prevData.id) || (data.battery != prevData.battery) || (data.status != prevData.status) ||
        (data.button == 1) || (data.temperature != prevData.temperature) || (data.humidity != prevData.humidity) ||
        ((data.timeStamp - prevData.timeStamp) >= SUPPRESS_TIME)) {
      // No duplicate, convert temperature
      double temperature = data.temperature / 10.0;
      // And Print
      printf("auriol %u %u %u %u %.1f %x\n",
        data.id, data.battery, data.status, data.button, temperature, data.humidity);
      fflush(stdout);
    }
    // Remember old message
    prevData = data;
  }
}

#endif // MODULE_AURIOL_ENABLE
