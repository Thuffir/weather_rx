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
#include "types.h"

#define PULSE_LENGTH       662
#define ZERO_LENGTH       1780
#define ONE_LENGTH        3850

#define TOLERANCE          200

#define IS_PULSE(length)  ((length >= (PULSE_LENGTH - TOLERANCE)) && (length <= (PULSE_LENGTH + TOLERANCE)))
#define IS_ZERO(length)   ((length >= (ZERO_LENGTH - TOLERANCE)) && (length <= (ZERO_LENGTH + TOLERANCE)))
#define IS_ONE(length)    ((length >= (ONE_LENGTH - TOLERANCE)) && (length <= (ONE_LENGTH + TOLERANCE)))


/***********************************************************************************************************************
 *
 *
 *
 **********************************************************************************************************************/
static BitType PulseSpaceDecode(uint32_t pulseLength)
{
  static enum {
    Idle,
    PulseReceived
  } state = Idle;

  // Return Value
  BitType bit = 0;
  static BitType inStream = 0;

  switch(state) {
    case Idle: {
      if(IS_PULSE(pulseLength)) {
        state = PulseReceived;
      }
      else {
        inStream = 0;
      }
    }
    break;

    case PulseReceived: {
      if(IS_ZERO(pulseLength)) {
        bit = BIT_ZERO | BIT_VALID | inStream;
        inStream = BIT_IN_STREAM;
      }
      else if(IS_ONE(pulseLength)) {
        bit = BIT_ONE | BIT_VALID | inStream;
        inStream = BIT_IN_STREAM;
      }
      else {
        inStream = 0;
      }

      state = Idle;
    }
    break;

    default: {
      state = Idle;
    }
    break;
  }

  return bit;
}

/***********************************************************************************************************************
 *
 *
 *
 **********************************************************************************************************************/
void AuriolProcess(uint32_t lircData)
{
  BitType bit;

  bit = PulseSpaceDecode(lircData);
  if(bit & BIT_VALID) {
    if(!(bit & BIT_IN_STREAM)) {
      printf("\n");
    }
    printf("%u ", bit & BIT_ONE);
    fflush(stdout);
  }
}
