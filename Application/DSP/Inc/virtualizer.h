#if !defined(__VIRTUALIZER_H__)
#define __VIRTUALIZER_H__

#include "dsp_module.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum VirtualizerTechnique {
  VIRTUALIZER_CROSSFEED,
  VIRTUALIZER_VIRTUAL_SPEAKERS,
} VirtualizerTechnique;

// A Crossfeed setting (the first three are published, the last is this project's own): the opposite channel is lowpassed at cutoffHz
// and reaches the ear levelDb below the direct one at low frequencies.
typedef struct CrossfeedPreset {
  const char* name; // ASCII, the LCD fonts have no accents
  float cutoffHz;
  float levelDb;
} CrossfeedPreset;

#define CROSSFEED_PRESET_COUNT 4

extern const CrossfeedPreset crossfeedPresets[CROSSFEED_PRESET_COUNT];

extern volatile bool virtEnabled;
extern volatile VirtualizerTechnique virtTechnique;
// Index into crossfeedPresets; a change takes effect at the next packet, without a fade.
extern volatile uint8_t crossfeedPreset;

extern const DspModule virtualizerModule;

#endif // __VIRTUALIZER_H__
