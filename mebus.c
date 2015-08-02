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
#ifdef MODULE_MEBUS_ENABLE

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
#define ZERO_LENGTH       1000
// Space length for bit ONE in us
#define ONE_LENGTH        2000

#else // ANALOG_FILTER
// The Analog filter alters the pulse / space timings

// Pulse length in us
#define PULSE_LENGTH       662
// Space length for bit ZERO in us
#define ZERO_LENGTH        780
// Space length for bit ONE in us
#define ONE_LENGTH        1850

#endif // ANALOG_FILTER

// Signal timing tolerance
#define TOLERANCE          200

// Search for identical messages within this timeframe in uS
#define DUPLICATE_TIME     1000000

// Decoded data
typedef struct {
  uint8_t id;
  uint8_t status;
  uint16_t temperature;
  uint8_t humidity;
  uint32_t timeStamp;
} MebusData;

/***********************************************************************************************************************
 * Mebus Message Decoder
 **********************************************************************************************************************/
static bool MebusDecode(MebusData *data, BitType bit)
{
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
      memset(data, 0, sizeof(MebusData));
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

    // ID [0 .. 13]
    if(bitNr <= 13) {
      data->id = (data->id << 1) | bit;
    }
    // Temperature [14 .. 23]
    else if((bitNr >= 14) && (bitNr <= 23)) {
      data->temperature = (data->temperature << 1) | bit;
    }
    // Status [24 .. 28]
    else if((bitNr >= 24) && (bitNr <= 28)) {
      data->status = (data->status << 1) | bit;
    }
    // Humidity [29..35]
    else if((bitNr >= 29) && (bitNr <= 35)) {
      data->humidity = (data->humidity << 1) | bit;
    }

    // Check if we have received everything
    if(bitNr == 35) {
      // Record reception Timestamp
      struct timeval tv;
      gettimeofday(&tv, NULL);
      data->timeStamp = (tv.tv_sec * 1000000) + tv.tv_usec;
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
 * Check if two messages are equal
 **********************************************************************************************************************/
static bool MebusIsMessageEqual(MebusData *msg1, MebusData *msg2)
{
  return(
      (msg1->id          == msg2->id         ) &&
      (msg1->status      == msg2->status     ) &&
      (msg1->temperature == msg2->temperature) &&
      (msg1->humidity    == msg2->humidity   ) &&
      // Messages can only be equal within the duplicate timeframe
      ((msg1->timeStamp - msg2->timeStamp) < DUPLICATE_TIME)
      );
}

/***********************************************************************************************************************
 * Process Bits for Mebus YD8220B
 **********************************************************************************************************************/
void MebusProcess(uint32_t pulseLength)
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
  static MebusData data, prevData = { 0 };
  // We will lock on one successful message duplicate
  static bool lock = false;

  // Decode Messages
  if(MebusDecode(&data, DecodePulseSpace(&bitDecoderCtx, pulseLength))) {
    // Release lock if we are outside the time frame
    if((data.timeStamp - prevData.timeStamp) >= DUPLICATE_TIME) {
      lock = false;
    }
    // Check for two successive duplicate messages
    if(!lock && MebusIsMessageEqual(&data, &prevData)) {
      // Set lock
      lock = true;
      // Convert temperature
      double temperature;
      temperature = data.temperature / 10.0;
      // And Print
      printf("mebus %u %u %.1f %u\n",data.id, data.status, temperature, data.humidity);
      fflush(stdout);
    }
    // Remember old message
    prevData = data;
  }
}

#endif // MODULE_MEBUS_ENABLE
