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
#include <unistd.h>
#include <fcntl.h>

#include "types.h"
#include "config.h"
#include "wt440h.h"
#include "auriol.h"
#include "rf_tech.h"
#include "mebus.h"
#include "ws1700.h"
#include "gt9000.h"

// Pulse length bits in lirc data
#define LIRC_LENGTH_MASK  0xFFFFFF

/***********************************************************************************************************************
 * Main
 **********************************************************************************************************************/
int main(int argc, char *argv[])
{
  // Lirc Device file name
  char *lircName = DEFAULT_LIRC_DEV;
  // LIRC Device file descriptor
  int lircDev;
  // Data from lirc driver
  uint32_t lircData;

  // Check lirc devide name exists on command line
  if(argc == 2) {
    lircName = argv[1];
  }

  // Open device file for reading
  lircDev = open(lircName, O_RDONLY);
  if(lircDev == -1) {
    perror("open()");
    exit(EXIT_FAILURE);
  }

  // Receive and decode messages
  while(1) {
    // Wait and read data from lirc
    if(read(lircDev, &lircData, sizeof(lircData)) != sizeof(lircData)) {
      perror("read()");
      exit(EXIT_FAILURE);
    }
    // Leave only the pulse length information
    lircData &= LIRC_LENGTH_MASK;

    // WT440H Messages
    WT440hProcess(lircData);
    // Auriol Messages
    AuriolProcess(lircData);
    // Mebus Messages
    MebusProcess(lircData);
    // RF-Tech Messages
    RFTechProcess(lircData);
    // WS 1700 Messages
    Ws1700Process(lircData);
    // GT-9000 Remote
    GT9000Process(lircData);
  }

  return 0;
}
