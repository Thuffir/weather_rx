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

#include <stdbool.h>
#include <string.h>
#include <sys/time.h>
#include <stdio.h>
#include "gt9000.h"
#include "types.h"

#ifdef MODULE_GT9000_ENABLE

#ifndef ANALOG_FILTER
#define START1_SHORT_LEN       400
#define START1_LONG_LEN       2300
#define START2_SHORT_LEN      3000
#define START2_LONG_LEN       7200
#define SHORT_LEN              400
#define LONG_LEN              1100
#define TOLERANCE              200

#define SHORT_LENGTH_MIN      (SHORT_LEN - TOLERANCE)
#define SHORT_LENGTH_MAX      (SHORT_LEN + TOLERANCE)
#define LONG_LENGTH_MIN       (LONG_LEN - TOLERANCE)
#define LONG_LENGTH_MAX       (LONG_LEN + TOLERANCE)

#else // ANALOG_FILTER
#define START1_SHORT_LEN       600
#define START1_LONG_LEN       2050
#define START2_SHORT_LEN      3260
#define START2_LONG_LEN       6920
#define TOLERANCE              200

#define SHORT_LENGTH_MIN       100
#define SHORT_LENGTH_MAX       700
#define LONG_LENGTH_MIN        800
#define LONG_LENGTH_MAX       1500

#endif // ANALOG_FILTER

#define START1_SHORT_LEN_MIN  (START1_SHORT_LEN - TOLERANCE)
#define START1_SHORT_LEN_MAX  (START1_SHORT_LEN + TOLERANCE)
#define START1_LONG_LEN_MIN   (START1_LONG_LEN - TOLERANCE)
#define START1_LONG_LEN_MAX   (START1_LONG_LEN + TOLERANCE)

#define START2_SHORT_LEN_MIN  (START2_SHORT_LEN - TOLERANCE)
#define START2_SHORT_LEN_MAX  (START2_SHORT_LEN + TOLERANCE)
#define START2_LONG_LEN_MIN   (START2_LONG_LEN - TOLERANCE)
#define START2_LONG_LEN_MAX   (START2_LONG_LEN + TOLERANCE)

#define MIN_LENGTH            SHORT_LENGTH_MIN

// Pulse length check macros
#define IS_PULSE_SHORT(length)  ((length >= SHORT_LENGTH_MIN) && (length <= SHORT_LENGTH_MAX))
#define IS_PULSE_LONG(length)   ((length >= LONG_LENGTH_MIN) && (length <= LONG_LENGTH_MAX))
// Start 1
#define IS_START1_SHORT(length) ((length >= START1_SHORT_LEN_MIN) && (length <= START1_SHORT_LEN_MAX))
#define IS_START1_LONG(length)  ((length >= START1_LONG_LEN_MIN) && (length <= START1_LONG_LEN_MAX))
// Start 2
#define IS_START2_SHORT(length) ((length >= START2_SHORT_LEN_MIN) && (length <= START2_SHORT_LEN_MAX))
#define IS_START2_LONG(length)  ((length >= START2_LONG_LEN_MIN) && (length <= START2_LONG_LEN_MAX))

// Search for identical messages within this timeframe in uS
#define DUPLICATE_TIME     1000000

// Invalid channel
#define CH_INVALID    255

// Decoded data
typedef struct {
  uint8_t channel;
  uint16_t code;
  uint32_t timeStamp;
} GT9000Data;

// Code Groups
typedef enum {
  CodeGroupA = 0,
  CodeGroupB = 1,
  CodeNotFound
} CodeGroupType;

typedef enum {
  Off = 0,
  On  = 1,
  Invalid
} StateType;

/***********************************************************************************************************************
 * Bit Decoder
 **********************************************************************************************************************/
static BitType GT9000BitDecode(uint32_t pulseLength)
{
  // Internal State
  static enum {
    Idle,
    Start1ShortReceived,
    Start1LongReceived,
    Start2ShortReceived,
    Start2LongReceived,
    BitReception,
    HalfZeroReceived,
    HalfOneReceived
  } state = Idle;
  // Are bits in a stream (no interruptions between)
  static BitType inStream = 0;
  // Return Value
  BitType bit = 0;
  // Recheck bit
  bool again = false;

  // Low pass filter
  if(pulseLength < MIN_LENGTH) {
    goto exit;
  }

  do {
    // Only recheck once
    again = false;
    // Bit reception state machine
    switch(state) {
      // No Start Mark received yet
      case Idle: {
        inStream = 0;
        // Start Mark 1
        if(IS_START1_SHORT(pulseLength)) {
          state = Start1ShortReceived;
        }
        // Start Mark 2
        else if(IS_START2_SHORT(pulseLength)) {
          state = Start2ShortReceived;
        }
        // else we stay in this state
      }
      break;

      // First pulse of start 1 bit received
      case Start1ShortReceived: {
        if(IS_START1_LONG(pulseLength)) {
          // Valid start bit received
          state = BitReception;
        }
        else {
          state = Idle;
          again = true;
        }
      }
      break;

      // First pulse of start 2 bit received
      case Start2ShortReceived: {
        if(IS_START2_LONG(pulseLength)) {
          // Valid start bit received
          state = BitReception;
        }
        else {
          state = Idle;
          again = true;
        }
      }
      break;

      // Start bit received, bit reception state
      case BitReception: {
        // First half of a Zero
        if(IS_PULSE_SHORT(pulseLength)) {
          state = HalfZeroReceived;
        }
        // First half of a One
        else if(IS_PULSE_LONG(pulseLength)) {
          state = HalfOneReceived;
        }
        else {
          state = Idle;
          again = true;
        }
      }
      break;

      // First half of a Zero received
      case HalfZeroReceived: {
        if(IS_PULSE_LONG(pulseLength)) {
          bit = BIT_ZERO | BIT_VALID | inStream;
          inStream = BIT_IN_STREAM;
          state = BitReception;
        }
        else {
          // Here we don't go to idle state, since the first half of a zero could be the first half of a type 1 start
          // bit, so we recheck it.
          state = Start1ShortReceived;
          inStream = 0;
          again = true;
        }
      }
      break;

      // First half of a One received
      case HalfOneReceived: {
        if(IS_PULSE_SHORT(pulseLength)) {
          bit = BIT_ONE | BIT_VALID | inStream;
          inStream = BIT_IN_STREAM;
          state = BitReception;
        }
        else {
          state = Idle;
          again = true;
        }
      }
      break;

      // Invalid state (should not happen)
      default: {
        state = Idle;
      }
      break;
    }
  } while(again == true);

  exit:
  return bit;
}

/***********************************************************************************************************************
 * Message Decoder
 **********************************************************************************************************************/
static bool GT9000Decode(GT9000Data *data, BitType bit)
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
      memset(data, 0, sizeof(GT9000Data));
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
    // Code [4 .. 19]
    else if((bitNr >= 4) && (bitNr <= 19)) {
      data->code = (data->code << 1) | bit;
    }
    // Channel [20 .. 22]
    else if((bitNr >= 20) && (bitNr <= 22)) {
      data->channel = (data->channel << 1) | bit;
    }

    // Check if we have received everything
    if(bitNr == 22) {
      // Record reception Timestamp
      struct timeval tv;
      gettimeofday(&tv, NULL);
      data->timeStamp = (tv.tv_sec * 1000000) + tv.tv_usec;
      retval = true;
    }

    // Increment bit pointer
    bitNr++;
    // But not more than 24 Bits
    if(bitNr > 23) {
      bitNr = 0;
    }
  } while(reCheck);

  exit:
  return retval;
}

/***********************************************************************************************************************
 * Check if two messages are equal
 **********************************************************************************************************************/
static bool GT9000IsMessageEqual(GT9000Data *msg1, GT9000Data *msg2)
{
  return(
      (msg1->channel == msg2->channel) &&
      (msg1->code    == msg2->code   ) &&
      // Messages can only be equal within the duplicate timeframe
      ((msg1->timeStamp - msg2->timeStamp) < DUPLICATE_TIME)
  );
}

/***********************************************************************************************************************
 * Convert received channel code to channel number
 **********************************************************************************************************************/
static uint8_t GT9000convertChannel(uint8_t channel)
{
  static const uint8_t channelTable[] = { 0, 3, 1, CH_INVALID, CH_INVALID, 4, 2 };

  if(channel >= (sizeof(channelTable) / sizeof(channelTable[0]))) {
    return CH_INVALID;
  }

  return channelTable[channel];
}

/***********************************************************************************************************************
 * Look up code group
 **********************************************************************************************************************/
static CodeGroupType GT9000LookUpCode(uint16_t code){
  static const uint16_t groupA[] = { 0x8F24, 0xC357, 0x57DB, 0xE5C3 };
  static const uint16_t groupB[] = { 0xBABA, 0x1842, 0x6D01, 0x42F9 };
  uint8_t i;

  // Look up Group A
  for(i = 0; i < (sizeof(groupA) / sizeof(groupA[0])); i++) {
    if(code == groupA[i]) {
      return CodeGroupA;
    }
  }

  // Look up Group B
  for(i = 0; i < (sizeof(groupB) / sizeof(groupB[0])); i++) {
    if(code == groupB[i]) {
      return CodeGroupB;
    }
  }

  // Not found
  return CodeNotFound;
}

/***********************************************************************************************************************
 * Map Code group and channel to action
 **********************************************************************************************************************/
static StateType GT9000MapCodeToFunction(uint8_t channel, uint16_t code)
{
  static const StateType stateTable[2][5] = {
    // Channel Map:    0    1    2    3    4
    [CodeGroupA] = { On,   On,  On, Off, Off },
    [CodeGroupB] = { Off, Off, Off,  On,  On }
  };

  // Get Code Group
  CodeGroupType codeGroup = GT9000LookUpCode(code);

  if((channel >= 5) || (codeGroup == CodeNotFound)) {
    return Invalid;
  }

  return(stateTable[codeGroup][channel]);
}

/***********************************************************************************************************************
 * Process Messages
 **********************************************************************************************************************/
void GT9000Process(uint32_t lircData)
{
  // Decoded data and the previous one
  static GT9000Data data, prevData = { 0 };
  // We will lock on one successful message duplicate
  static bool lock = false;

  // Decode Messages
  if(GT9000Decode(&data, GT9000BitDecode(lircData))) {
    // Check if actual and previous messages are equal
    bool equal = GT9000IsMessageEqual(&data, &prevData);
    // If messages are different
    if(!equal) {
      // Release lock
      lock = false;
    }
    // Check for two successive duplicate messages
    if(!lock && equal) {
      // Convert Channel
      uint8_t channel = GT9000convertChannel(data.channel);
      // Set lock
      lock = true;
      // Print
      printf("gt9000 %u %u \n",channel, GT9000MapCodeToFunction(channel, data.code));
      fflush(stdout);
    }
    // Remember old message
    prevData = data;
  }
}

#endif // MODULE_GT9000_ENABLE
