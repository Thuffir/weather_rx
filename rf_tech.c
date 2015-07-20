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
#ifdef MODULE_RFTECH_ENABLE

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/time.h>
#include "types.h"
#include "DecodePulseSpace.h"

// Pulse length in us
#define PULSE_LENGTH       662
// Space length for bit ZERO in us
#define ZERO_LENGTH       1780
// Space length for bit ONE in us
#define ONE_LENGTH        3850

// Signal timing tolerance
#define TOLERANCE          200

// Suppress identical messages within this timeframe in uS
#define SUPPRESS_TIME     1000000

// Decoded data
typedef struct {
  uint8_t id;
  uint8_t status;
  uint8_t temperatureInteger;
  uint8_t temperatureFraction;
  uint32_t timeStamp;
} RFTechData;
// Temperature Sign bit
#define TEMP_SIGN_BIT      (1 << 7)

/***********************************************************************************************************************
 * RF-Tech Message Decoder
 **********************************************************************************************************************/
static bool RFTechDecode(RFTechData *data, BitType bit)
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
      memset(data, 0, sizeof(RFTechData));
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
      data->id = (data->id << 1) | bit;
    }
    // Temperature Integer Part [8 .. 15]
    else if((bitNr >= 8) && (bitNr <= 15)) {
      data->temperatureInteger = (data->temperatureInteger << 1) | bit;
    }
    // Status [16 .. 19]
    else if((bitNr >= 16) && (bitNr <= 19)) {
      data->status = (data->status << 1) | bit;
    }
    // Temperature Fraction Part [20 .. 23]
    else if((bitNr >= 20) && (bitNr <= 23)) {
      data->temperatureFraction = (data->temperatureFraction << 1) | bit;
    }

    // Check if we have received everything
    if(bitNr == 23) {
      // Record reception Timestamp
      struct timeval tv;
      gettimeofday(&tv, NULL);
      data->timeStamp = (tv.tv_sec * 1000000) + tv.tv_usec;
      retval = true;
    }


    // Increment bit pointer
    bitNr++;
    // But not more than 36 Bits
    if(bitNr > 23) {
      bitNr = 0;
    }
  } while(reCheck);

  exit:
  return retval;
}

/***********************************************************************************************************************
 * Process Bits for Auriol
 **********************************************************************************************************************/
void RFTechProcess(uint32_t pulseLength)
{
  // Bit decoder context
  static PulseSpaceContext bitDecoderCtx = {
    .pulseMin = PULSE_LENGTH - TOLERANCE,
    .pulseMax = PULSE_LENGTH + TOLERANCE,
    .zeroMin  = ZERO_LENGTH  - TOLERANCE,
    .zeroMax  = ZERO_LENGTH  + TOLERANCE,
    .oneMin   = ONE_LENGTH   - TOLERANCE,
    .oneMax   = ONE_LENGTH   + TOLERANCE,
    .state = Idle
  };
  // Decoded data and the previous one
  static RFTechData data, prevData = { 0 };

  // Decode Messages
  if(RFTechDecode(&data, DecodePulseSpace(&bitDecoderCtx, pulseLength))) {
    // Check if a message is a duplicate of a last one
    if((data.id != prevData.id) || (data.status != prevData.status) ||
        (data.temperatureInteger != prevData.temperatureInteger) ||
        (data.temperatureFraction != prevData.temperatureFraction) ||
        ((data.timeStamp - prevData.timeStamp) >= SUPPRESS_TIME)) {
      // No duplicate, convert temperature
      double temperature;
      temperature = data.temperatureInteger & (~TEMP_SIGN_BIT);
      temperature += data.temperatureFraction / 10.0;
      // And Print
      printf("rftech %u %u %.1f\n",data.id, data.status, temperature);
      fflush(stdout);
    }
    // Remember old message
    prevData = data;
  }
}

#endif // MODULE_RFTECH_ENABLE
