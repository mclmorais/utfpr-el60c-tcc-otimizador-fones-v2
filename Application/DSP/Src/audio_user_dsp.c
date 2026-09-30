#include "audio_user_dsp.h"
#include "usb_audio.h"
#include <math.h>

#define PI 3.14159265358979323846
#define BAND_BANDWIDTH_OCTAVES (1.0 / 3.0)

// Float round-off recirculates through the near-unit-circle poles of the
// lowest bands and lands above the 16-bit noise floor; double costs about
// 2.7x per band, so only those bands pay for it.
#define DOUBLE_PRECISION_BAND_COUNT 7
#define SINGLE_PRECISION_BAND_COUNT (EQ_BAND_COUNT - DOUBLE_PRECISION_BAND_COUNT)

typedef struct DoubleBiquad {
  double b0, b1, b2, a1, a2;
  double x1[2], x2[2], y1[2], y2[2];
} DoubleBiquad;

typedef struct FloatBiquad {
  float b0, b1, b2, a1, a2;
  float x1[2], x2[2], y1[2], y2[2];
} FloatBiquad;

const EqBand eqBands[EQ_BAND_COUNT] = {
  {20, "20"},
  {25, "25"},
  {31.5f, "31"},
  {40, "40"},
  {50, "50"},
  {63, "63"},
  {80, "80"},
  {100, "100"},
  {125, "125"},
  {160, "160"},
  {200, "200"},
  {250, "250"},
  {315, "315"},
  {400, "400"},
  {500, "500"},
  {630, "630"},
  {800, "800"},
  {1000, "1k"},
  {1250, "1k2"},
  {1600, "1k6"},
  {2000, "2k"},
  {2500, "2k5"},
  {3150, "3k1"},
  {4000, "4k"},
  {5000, "5k"},
  {6300, "6k3"},
  {8000, "8k"},
  {10000, "10k"},
  {12500, "12k"},
  {16000, "16k"},
  {20000, "20k"}
};

volatile int8_t eqGains[EQ_BAND_COUNT];

static DoubleBiquad lowFilters[DOUBLE_PRECISION_BAND_COUNT];
static FloatBiquad highFilters[SINGLE_PRECISION_BAND_COUNT];
static int8_t builtGains[EQ_BAND_COUNT];

static void AudioUserDsp_BuildBand(uint32_t band, int8_t gain)
{
  double A = pow(10.0, gain / 40.0);
  double omega = 2.0 * PI * eqBands[band].frequency / USB_AUDIO_CONFIG_PLAY_DEF_FREQ;
  double alpha = sin(omega) * sinh(log(2) / 2.0 * BAND_BANDWIDTH_OCTAVES * omega / sin(omega));

  double a0 = 1.0 + alpha / A;
  double b0 = (1.0 + alpha * A) / a0;
  double b1 = -2.0 * cos(omega) / a0;
  double b2 = (1.0 - alpha * A) / a0;
  double a1 = b1;
  double a2 = (1.0 - alpha / A) / a0;

  if(band < DOUBLE_PRECISION_BAND_COUNT)
  {
    DoubleBiquad* filter = &lowFilters[band];
    filter->b0 = b0;
    filter->b1 = b1;
    filter->b2 = b2;
    filter->a1 = a1;
    filter->a2 = a2;
  }
  else
  {
    FloatBiquad* filter = &highFilters[band - DOUBLE_PRECISION_BAND_COUNT];
    filter->b0 = (float)b0;
    filter->b1 = (float)b1;
    filter->b2 = (float)b2;
    filter->a1 = (float)a1;
    filter->a2 = (float)a2;
  }
  builtGains[band] = gain;
}

void AudioUserDsp_Init(void)
{
  for(uint32_t i = 0; i < DOUBLE_PRECISION_BAND_COUNT; i++)
    lowFilters[i] = (DoubleBiquad){0};
  for(uint32_t i = 0; i < SINGLE_PRECISION_BAND_COUNT; i++)
    highFilters[i] = (FloatBiquad){0};
  for(uint32_t i = 0; i < EQ_BAND_COUNT; i++)
    AudioUserDsp_BuildBand(i, eqGains[i]);
}

// A 0 dB band is an exact identity, so it is skipped; its history still
// tracks the signal so that raising its gain later does not click.
static double AudioUserDsp_RunLowBands(double sample, uint32_t channel)
{
  for(uint32_t i = 0; i < DOUBLE_PRECISION_BAND_COUNT; i++)
  {
    DoubleBiquad* filter = &lowFilters[i];
    double output = sample;
    if(builtGains[i] != 0)
      output =
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
  return sample;
}

static float AudioUserDsp_RunHighBands(float sample, uint32_t channel)
{
  for(uint32_t i = 0; i < SINGLE_PRECISION_BAND_COUNT; i++)
  {
    FloatBiquad* filter = &highFilters[i];
    float output = sample;
    if(builtGains[i + DOUBLE_PRECISION_BAND_COUNT] != 0)
      output =
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
  return sample;
}

void AudioUserDsp_Process(uint8_t* frames, uint32_t length)
{
  // A rebuild costs several libm calls; one band per packet keeps the ISR
  // inside its 1 ms budget, and the remaining bands converge on later packets.
  for(uint32_t i = 0; i < EQ_BAND_COUNT; i++)
  {
    int8_t gain = eqGains[i];
    if(builtGains[i] != gain)
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
      double input = (int16_t)(bytes[0] | (bytes[1] << 8));
      float sample = AudioUserDsp_RunHighBands((float)AudioUserDsp_RunLowBands(input, channel), channel);

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
