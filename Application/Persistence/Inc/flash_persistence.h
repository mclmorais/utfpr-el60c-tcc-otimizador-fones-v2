#ifndef __FLASH_PERSISTENCE_H
#define __FLASH_PERSISTENCE_H

// includes 
#include <stdint.h>
#include "stm32f7xx_hal.h"

// Bank 2 in dual-bank mode, so erasing it does not stall code running from bank 1; the smallest sector there (16 KB) keeps the erase short.
#define FLASH_USER_START_ADDR 0x08100000
#define FLASH_USER_SECTOR FLASH_SECTOR_12

void FlashPersistence_Write();
void FlashPersistence_Restore();


#endif // __FLASH_PERSISTENCE_H
