#include "flash_persistence.h"
#include "audio_user_dsp.h"
#include "usbd_core.h"
#include <stdbool.h>

#define FLASH_LAYOUT_TAG 0x31335145

extern USBD_HandleTypeDef USBD_Device;

void FlashPersistence_Write()
{
  FLASH_EraseInitTypeDef eraseInitStruct;
  eraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORS;
  eraseInitStruct.Sector = FLASH_SECTOR_10;
  eraseInitStruct.NbSectors = 1;
  eraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;
  uint32_t sectorError = 0;
    
  USBD_LL_Suspend(&USBD_Device);
  USBD_Stop(&USBD_Device);
  HAL_FLASH_Unlock();
  HAL_FLASHEx_Erase(&eraseInitStruct, &sectorError);
  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_USER_START_ADDR, FLASH_LAYOUT_TAG);
  for(uint32_t i = 0; i < EQ_BAND_COUNT; i++)
  {
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_USER_START_ADDR + (i + 1) * 4, (uint32_t)(int32_t)eqGains[i]);
  }
  HAL_FLASH_Lock();
  USBD_LL_Resume(&USBD_Device);
  USBD_Start(&USBD_Device);
}

void FlashPersistence_Restore()
{
  volatile uint32_t* words = (volatile uint32_t*)FLASH_USER_START_ADDR;
  // An erased sector reads 0xFFFFFFFF and older firmware stored knob Y positions; neither carries the tag.
  bool isLayoutValid = words[0] == FLASH_LAYOUT_TAG;
  for(uint32_t i = 0; i < EQ_BAND_COUNT; i++)
  {
    int32_t gain = (int32_t)words[i + 1];
    if(isLayoutValid && gain >= EQ_GAIN_MIN_DB && gain <= EQ_GAIN_MAX_DB)
      eqGains[i] = gain;
    else
      eqGains[i] = 0;
  }
}
