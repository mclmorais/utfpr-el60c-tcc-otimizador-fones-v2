#include "audio_user_dsp.h"
#include "usb_audio.h"
#include <math.h>

#define PI 3.14159265358979323846

typedef struct BiquadFilter {
  float b0, b1, b2, a1, a2;
  float x1[2], x2[2], y1[2], y2[2];
  int8_t gain;
} BiquadFilter;

static const float frequencies[EQ_BAND_COUNT] = {30, 60, 150, 400, 1000, 3000, 8000, 16000};
static const float bandwidths[EQ_BAND_COUNT] =  {1,   1,   2,   2,    2,    3,    3,     3};

volatile int8_t eqGains[EQ_BAND_COUNT];

static BiquadFilter filters[EQ_BAND_COUNT];

static void AudioUserDsp_BuildBand(uint32_t band, int8_t gain)
{
  BiquadFilter* filter = &filters[band];
  double A = pow(10.0, gain / 40.0);
  double omega = 2.0 * PI * frequencies[band] / USB_AUDIO_CONFIG_PLAY_DEF_FREQ;
  double alpha = sin(omega) * sinh(log(2) / 2.0 * bandwidths[band] * omega / sin(omega));

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
}

void AudioUserDsp_Init(void)
{
  for(uint32_t i = 0; i < EQ_BAND_COUNT; i++)
  {
    filters[i] = (BiquadFilter){0};
    AudioUserDsp_BuildBand(i, eqGains[i]);
  }
}

void AudioUserDsp_Process(uint8_t* frames, uint32_t length)
{
  // A rebuild costs several libm calls; one band per packet keeps the ISR
  // inside its 1 ms budget, and the remaining bands converge on later packets.
  for(uint32_t i = 0; i < EQ_BAND_COUNT; i++)
  {
    int8_t gain = eqGains[i];
    if(filters[i].gain != gain)
    {
      AudioUserDsp_BuildBand(i, gain);
      break;
    }
  }

  for(uint32_t offset = 0; offset + 4 <= length; offset += 4)
  {
    for(uint32_t channel = 0; channel < 2; channel++)
    {
      uint8_t* bytes = frames + offset + 2 * channel;
      float sample = (int16_t)(bytes[0] | (bytes[1] << 8));

      for(uint32_t i = 0; i < EQ_BAND_COUNT; i++)
      {
        BiquadFilter* filter = &filters[i];
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
