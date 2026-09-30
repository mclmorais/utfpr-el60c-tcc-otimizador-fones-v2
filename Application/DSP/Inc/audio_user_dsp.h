#if !defined(__AUDIO_USER_DSP_H__)
#define __AUDIO_USER_DSP_H__

#include <stdint.h>
#include "stdbool.h"
#include "user_lcd.h"

#define NUMBER_OF_BANDS 8


typedef struct BiquadFilter {
  float b0, b1, b2, a1, a2;
  float x1[2], x2[2], y1[2], y2[2];
  int32_t gain, frequency, bandwidth;
} BiquadFilter;

void AudioUserDsp_Process(uint8_t* frames, uint32_t length);
void AudioUserDsp_BiquadFilterConfig(BiquadFilter* filter, int16_t gain, int16_t frequency, int16_t bandwidth);
int16_t AudioUserDsp_CalculateGain(uint16_t sliderY, SliderKnob* sliderKnob);

extern BiquadFilter biquadFilters[NUMBER_OF_BANDS];

#endif // __AUDIO_USER_DSP_H__
