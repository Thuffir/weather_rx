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

#include "DecodePulseSpace.h"

/***********************************************************************************************************************
 * Pulse / Space Length Decoder
 **********************************************************************************************************************/
BitType DecodePulseSpace(PulseSpaceContext *ctx, uint32_t pulseLength)
{
  // Return Value
  BitType bit = 0;

  // Bit reception state machine
  switch(ctx->state) {
    // No pulse received yet
    case Idle: {
      // Check for pulse
      if((pulseLength >= ctx->pulseMin) && (pulseLength <= ctx->pulseMax)) {
        ctx->state = PulseReceived;
      }
      // else Following bit not in stream
      else {
        ctx->inStream = 0;
      }
    }
    break;

    // Pulse received before
    case PulseReceived: {
      // check for zero
      if((pulseLength >= ctx->zeroMin) && (pulseLength <= ctx->zeroMax)) {
        bit = BIT_ZERO | BIT_VALID | (ctx->inStream);
        ctx->inStream = BIT_IN_STREAM;
      }
      // else check for one
      else if((pulseLength >= ctx->oneMin) && (pulseLength <= ctx->oneMax)) {
        bit = BIT_ONE | BIT_VALID | (ctx->inStream);
        ctx->inStream = BIT_IN_STREAM;
      }
      // else Following bit not in stream
      else {
        ctx->inStream = 0;
      }

      ctx->state = Idle;
    }
    break;

    // Invalid state (should not happen)
    default: {
      ctx->state = Idle;
    }
    break;
  }

  return bit;
}
