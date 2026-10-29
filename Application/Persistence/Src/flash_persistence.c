#include "flash_persistence.h"
#include "audio_user_dsp.h"

#define FLASH_LAYOUT_TAG 0x31335145
// Sectors saved before the orientation existed read 0xFFFFFFFF here, which loads as not flipped.
#define FLASH_FLIP_WORD (EQ_BAND_COUNT + 1)

static volatile uint32_t* const words = (volatile uint32_t*)FLASH_USER_START_ADDR;

static int8_t ReadSavedGain(uint32_t band)
{
  // An erased sector reads 0xFFFFFFFF and older firmware stored knob Y positions; neither carries the tag.
  int32_t gain = (int32_t)words[band + 1];
  if(words[0] == FLASH_LAYOUT_TAG && gain >= EQ_GAIN_MIN_DB && gain <= EQ_GAIN_MAX_DB)
    return gain;
  return 0;
}

static void WriteSector(const volatile int8_t* gains, bool isFlipped)
{
  // In single-bank mode (nDBANK=1) the sector number of FLASH_USER_SECTOR is reserved, so the erase would hit an unknown region.
  if(FLASH->OPTCR & FLASH_OPTCR_nDBANK)
    return;

  FLASH_EraseInitTypeDef eraseInitStruct;
  eraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORS;
  eraseInitStruct.Sector = FLASH_USER_SECTOR;
  eraseInitStruct.NbSectors = 1;
  eraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;
  uint32_t sectorError = 0;

  HAL_FLASH_Unlock();
  HAL_FLASHEx_Erase(&eraseInitStruct, &sectorError);
  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_USER_START_ADDR, FLASH_LAYOUT_TAG);
  for(uint32_t i = 0; i < EQ_BAND_COUNT; i++)
  {
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_USER_START_ADDR + (i + 1) * 4, (uint32_t)(int32_t)gains[i]);
  }
  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_USER_START_ADDR + FLASH_FLIP_WORD * 4, isFlipped);
  HAL_FLASH_Lock();
}

void FlashPersistence_Write(bool isFlipped)
{
  WriteSector(eqGains, isFlipped);
}

void FlashPersistence_WriteFlip(bool isFlipped)
{
  // Rewrites the last saved gains, so flipping never saves EQ edits the user has not saved.
  int8_t savedGains[EQ_BAND_COUNT];
  for(uint32_t i = 0; i < EQ_BAND_COUNT; i++)
    savedGains[i] = ReadSavedGain(i);
  WriteSector(savedGains, isFlipped);
}

void FlashPersistence_Restore()
{
  for(uint32_t i = 0; i < EQ_BAND_COUNT; i++)
    eqGains[i] = ReadSavedGain(i);
}

bool FlashPersistence_ReadFlip()
{
  return words[0] == FLASH_LAYOUT_TAG && words[FLASH_FLIP_WORD] == 1;
}
