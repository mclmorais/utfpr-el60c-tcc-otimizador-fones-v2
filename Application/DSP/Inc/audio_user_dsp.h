#if !defined(__AUDIO_USER_DSP_H__)
#define __AUDIO_USER_DSP_H__

#include <stdint.h>

#define EQ_BAND_COUNT 31
#define EQ_GAIN_MIN_DB (-15)
#define EQ_GAIN_MAX_DB 15
// Gains are stored in tenths of a dB so the step buttons can fine tune a band.
#define EQ_GAIN_STEPS_PER_DB 10
#define EQ_GAIN_MIN (EQ_GAIN_MIN_DB * EQ_GAIN_STEPS_PER_DB)
#define EQ_GAIN_MAX (EQ_GAIN_MAX_DB * EQ_GAIN_STEPS_PER_DB)

typedef struct EqBand {
  float frequency;
  const char* label;
} EqBand;

extern const EqBand eqBands[EQ_BAND_COUNT];
extern volatile int16_t eqGains[EQ_BAND_COUNT];

void AudioUserDsp_Init(void);
void AudioUserDsp_Process(uint8_t* frames, uint32_t length);

#endif // __AUDIO_USER_DSP_H__
