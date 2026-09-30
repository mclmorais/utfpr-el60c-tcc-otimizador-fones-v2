#include "audio_user_dsp.h"
#include "usb_audio.h"
#include <math.h>

#define PI 3.14159265358979323846f

BiquadFilter biquadFilters[NUMBER_OF_BANDS];

void AudioUserDsp_Process(uint8_t* frames, uint32_t length)
{
  for(uint32_t offset = 0; offset + 4 <= length; offset += 4)
  {
    for(uint32_t channel = 0; channel < 2; channel++)
    {
      uint8_t* bytes = frames + offset + 2 * channel;
      float sample = (int16_t)(bytes[0] | (bytes[1] << 8));

      for(uint32_t i = 0; i < NUMBER_OF_BANDS; i++)
      {
        BiquadFilter* filter = &biquadFilters[i];
        float output =
            filter->b0 * sample
          + filter->b1 * filter->x1[channel]
          + filter->b2 * filter->x2[channel]
          - filter->a1 * filter->y1[channel]
          - filter->a2 * filter->y2[channel];

        filter->x2[channel] = filter->x1[channel];
        filter->x1[channel] = sample;
        filter->y2[channel] = filter->y1[channel];
        filter->y1[channel] = output;
        sample = output;
      }

      if(sample > 32767.0f)
        sample = 32767.0f;
      else if(sample < -32768.0f)
        sample = -32768.0f;

      uint16_t result = (uint16_t)(int16_t)sample;
      bytes[0] = result & 0xFF;
      bytes[1] = result >> 8;
    }
  }
}

int16_t AudioUserDsp_CalculateGain(uint16_t sliderY, SliderKnob* sliderKnob)
{
  double inputMin = sliderKnob->sliderY;
  double inputMax = sliderKnob->sliderY + sliderKnob->sliderHeight;
  double outputMax = 15;
  double outputMin = -15;
  int16_t newGain = outputMax + (sliderKnob->knobY - inputMin) * (outputMin - outputMax) / (inputMax - inputMin);
  return newGain;
}


void AudioUserDsp_BiquadFilterConfig(BiquadFilter* filter, int16_t gain, int16_t frequency, int16_t bandwidth)
{
  double A = pow(10.0, gain / 40.0);
  double omega = 2.0 * PI * frequency / USB_AUDIO_CONFIG_PLAY_DEF_FREQ;
  double alpha = sin(omega) * sinh(log(2) / 2.0 * bandwidth * omega / sin(omega));

  double b0 = 1.0 + alpha * A;
  double b1 = -2.0 * cos(omega);
  double b2 = 1.0 - alpha * A;
  double a0 = 1.0 + alpha / A;
  double a1 = -2.0 * cos(omega);
  double a2 = 1.0 - alpha / A;

  filter->b0 = (float)(b0 / a0);
  filter->b1 = (float)(b1 / a0);
  filter->b2 = (float)(b2 / a0);
  filter->a1 = (float)(a1 / a0);
  filter->a2 = (float)(a2 / a0);

  filter->gain = gain;
  filter->frequency = frequency;
  filter->bandwidth = bandwidth;
}
