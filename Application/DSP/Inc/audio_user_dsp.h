#if !defined(__AUDIO_USER_DSP_H__)
#define __AUDIO_USER_DSP_H__

#include <stdint.h>

#define EQ_BAND_COUNT 8
#define EQ_GAIN_MIN_DB (-15)
#define EQ_GAIN_MAX_DB 15

extern volatile int8_t eqGains[EQ_BAND_COUNT];

void AudioUserDsp_Init(void);
void AudioUserDsp_Process(uint8_t* frames, uint32_t length);

#endif // __AUDIO_USER_DSP_H__
