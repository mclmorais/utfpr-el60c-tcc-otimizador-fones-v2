#include "flash_persistence.h"
#include "equalizer.h"
#include "virtualizer.h"

#define FLASH_LAYOUT_TAG          0x32335145
#define FLASH_WHOLE_DB_LAYOUT_TAG 0x31335145
// Sectors saved before the orientation existed read 0xFFFFFFFF here, which loads as not flipped.
#define FLASH_FLIP_WORD (EQ_BAND_COUNT + 1)
// Sectors saved before the brightness existed read 0xFFFFFFFF here, which loads as full brightness.
#define FLASH_BRIGHTNESS_WORD (EQ_BAND_COUNT + 2)
#define DEFAULT_BRIGHTNESS    100
// Sectors saved before the dark mode existed read 0xFFFFFFFF here, which loads as the light theme.
#define FLASH_DARK_WORD (EQ_BAND_COUNT + 3)
// Sectors saved before the Equalizer switch existed read 0xFFFFFFFF here, which loads as on.
#define FLASH_EQ_ENABLED_WORD (EQ_BAND_COUNT + 4)
// Sectors saved before the Virtualization existed read 0xFFFFFFFF here, which loads as off.
#define FLASH_VIRT_ENABLED_WORD (EQ_BAND_COUNT + 5)
// Sectors saved before the Presets existed read 0xFFFFFFFF here, which is out of range and loads as the first Preset, Padrao.
#define FLASH_CROSSFEED_PRESET_WORD (EQ_BAND_COUNT + 6)

static volatile uint32_t* const words = (volatile uint32_t*)FLASH_USER_START_ADDR;

// Returns 0 when the sector holds no gains: an erased sector reads 0xFFFFFFFF and older firmware stored knob Y positions; neither carries a tag.
static int32_t SavedGainScale(void)
{
  if(words[0] == FLASH_LAYOUT_TAG)
    return 1;
  // Sectors saved before gains had tenths of a dB hold whole dB.
  if(words[0] == FLASH_WHOLE_DB_LAYOUT_TAG)
    return EQ_GAIN_STEPS_PER_DB;
  return 0;
}

static int16_t ReadSavedGain(uint32_t band)
{
  int32_t gain = (int32_t)words[band + 1] * SavedGainScale();
  if(gain >= EQ_GAIN_MIN && gain <= EQ_GAIN_MAX)
    return gain;
  return 0;
}

static void WriteSector(const volatile int16_t* gains, DisplaySettings display)
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
  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_USER_START_ADDR + FLASH_FLIP_WORD * 4, display.isFlipped);
  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_USER_START_ADDR + FLASH_BRIGHTNESS_WORD * 4, display.brightness);
  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_USER_START_ADDR + FLASH_DARK_WORD * 4, display.isDark);
  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_USER_START_ADDR + FLASH_EQ_ENABLED_WORD * 4, eqEnabled);
  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_USER_START_ADDR + FLASH_VIRT_ENABLED_WORD * 4, virtEnabled);
  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_USER_START_ADDR + FLASH_CROSSFEED_PRESET_WORD * 4, crossfeedPreset);
  HAL_FLASH_Lock();
}

void FlashPersistence_Write(DisplaySettings display)
{
  WriteSector(eqGains, display);
}

void FlashPersistence_WriteSettings(DisplaySettings display)
{
  // Rewrites the last saved gains, so a setting saved on touch never saves EQ edits the user has not saved.
  int16_t savedGains[EQ_BAND_COUNT];
  for(uint32_t i = 0; i < EQ_BAND_COUNT; i++)
    savedGains[i] = ReadSavedGain(i);
  WriteSector(savedGains, display);
}

void FlashPersistence_RestoreGains()
{
  for(uint32_t i = 0; i < EQ_BAND_COUNT; i++)
    eqGains[i] = ReadSavedGain(i);
}

void FlashPersistence_Restore()
{
  FlashPersistence_RestoreGains();

  bool hasSavedSettings = SavedGainScale() != 0;
  eqEnabled = !hasSavedSettings || words[FLASH_EQ_ENABLED_WORD] != 0;
  virtEnabled = hasSavedSettings && words[FLASH_VIRT_ENABLED_WORD] == 1;
  crossfeedPreset = hasSavedSettings && words[FLASH_CROSSFEED_PRESET_WORD] < CROSSFEED_PRESET_COUNT
                        ? (uint8_t)words[FLASH_CROSSFEED_PRESET_WORD]
                        : 0;
}

DisplaySettings FlashPersistence_ReadDisplay()
{
  DisplaySettings display = {false, DEFAULT_BRIGHTNESS, false};
  if(SavedGainScale() == 0)
    return display;

  display.isFlipped = words[FLASH_FLIP_WORD] == 1;
  if(words[FLASH_BRIGHTNESS_WORD] >= 1 && words[FLASH_BRIGHTNESS_WORD] <= 100)
    display.brightness = words[FLASH_BRIGHTNESS_WORD];
  display.isDark = words[FLASH_DARK_WORD] == 1;
  return display;
}
