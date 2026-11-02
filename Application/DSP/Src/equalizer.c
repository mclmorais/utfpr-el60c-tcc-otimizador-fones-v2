#include "equalizer.h"
#include "audio_user_dsp.h"
#include <math.h>

#define PI                     3.14159265358979323846
#define BAND_BANDWIDTH_OCTAVES (1.0 / 3.0)

// Float round-off recirculates through the near-unit-circle poles of the
// lowest bands and lands above the 16-bit noise floor; double costs about
// 2.7x per band, so only those bands pay for it.
#define DOUBLE_PRECISION_BAND_COUNT 7
#define SINGLE_PRECISION_BAND_COUNT (EQ_BAND_COUNT - DOUBLE_PRECISION_BAND_COUNT)

typedef struct DoubleHistory {
  double x1, x2, y1, y2;
} DoubleHistory;

typedef struct DoubleBiquad {
  double b0, b1, b2, a1, a2;
  DoubleHistory history[2];
} DoubleBiquad;

typedef struct FloatHistory {
  float x1, x2, y1, y2;
} FloatHistory;

typedef struct FloatBiquad {
  float b0, b1, b2, a1, a2;
  FloatHistory history[2];
} FloatBiquad;

const EqBand eqBands[EQ_BAND_COUNT] = {
  {20,    "20" },
  {25,    "25" },
  {31.5f, "31" },
  {40,    "40" },
  {50,    "50" },
  {63,    "63" },
  {80,    "80" },
  {100,   "100"},
  {125,   "125"},
  {160,   "160"},
  {200,   "200"},
  {250,   "250"},
  {315,   "315"},
  {400,   "400"},
  {500,   "500"},
  {630,   "630"},
  {800,   "800"},
  {1000,  "1k" },
  {1250,  "1k2"},
  {1600,  "1k6"},
  {2000,  "2k" },
  {2500,  "2k5"},
  {3150,  "3k1"},
  {4000,  "4k" },
  {5000,  "5k" },
  {6300,  "6k3"},
  {8000,  "8k" },
  {10000, "10k"},
  {12500, "12k"},
  {16000, "16k"},
  {20000, "20k"},
};

volatile int16_t eqGains[EQ_BAND_COUNT];

static DoubleBiquad lowFilters[DOUBLE_PRECISION_BAND_COUNT];
static FloatBiquad highFilters[SINGLE_PRECISION_BAND_COUNT];
static int16_t builtGains[EQ_BAND_COUNT];
static double wideSamples[AUDIO_USER_DSP_MAX_PACKET_FRAMES];

static void Equalizer_BuildBand(uint32_t band, int16_t gain)
{
  double A = pow(10.0, gain / (40.0 * EQ_GAIN_STEPS_PER_DB));
  double omega = 2.0 * PI * eqBands[band].frequency / AUDIO_USER_DSP_SAMPLE_RATE;
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

static void Equalizer_Init(void)
{
  for(uint32_t i = 0; i < DOUBLE_PRECISION_BAND_COUNT; i++)
    lowFilters[i] = (DoubleBiquad){0};
  for(uint32_t i = 0; i < SINGLE_PRECISION_BAND_COUNT; i++)
    highFilters[i] = (FloatBiquad){0};
  for(uint32_t i = 0; i < EQ_BAND_COUNT; i++)
    Equalizer_BuildBand(i, eqGains[i]);
}

static void Equalizer_RunDoubleBand(const DoubleBiquad* filter, DoubleHistory* history, double* samples,
                                    uint32_t frameCount)
{
  double x1 = history->x1;
  double x2 = history->x2;
  double y1 = history->y1;
  double y2 = history->y2;
  for(uint32_t i = 0; i < frameCount; i++)
  {
    double x = samples[i];
    double y = filter->b0 * x + filter->b1 * x1 + filter->b2 * x2 - filter->a1 * y1 - filter->a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    samples[i] = y;
  }
  *history = (DoubleHistory){x1, x2, y1, y2};
}

static void Equalizer_RunFloatBand(const FloatBiquad* filter, FloatHistory* history, float* samples,
                                   uint32_t frameCount)
{
  float x1 = history->x1;
  float x2 = history->x2;
  float y1 = history->y1;
  float y2 = history->y2;
  for(uint32_t i = 0; i < frameCount; i++)
  {
    float x = samples[i];
    float y = filter->b0 * x + filter->b1 * x1 + filter->b2 * x2 - filter->a1 * y1 - filter->a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    samples[i] = y;
  }
  *history = (FloatHistory){x1, x2, y1, y2};
}

// A 0 dB band is an exact identity, so it is skipped; its history still
// tracks the signal so that raising its gain later does not click.
static void Equalizer_TrackDoubleBand(DoubleHistory* history, const double* samples, uint32_t frameCount)
{
  double last = samples[frameCount - 1];
  if(frameCount == 1)
    *history = (DoubleHistory){last, history->x1, last, history->y1};
  else
    *history = (DoubleHistory){last, samples[frameCount - 2], last, samples[frameCount - 2]};
}

static void Equalizer_TrackFloatBand(FloatHistory* history, const float* samples, uint32_t frameCount)
{
  float last = samples[frameCount - 1];
  if(frameCount == 1)
    *history = (FloatHistory){last, history->x1, last, history->y1};
  else
    *history = (FloatHistory){last, samples[frameCount - 2], last, samples[frameCount - 2]};
}

static void Equalizer_ProcessChannel(float* samples, uint32_t frameCount, uint32_t channel)
{
  for(uint32_t i = 0; i < frameCount; i++)
    wideSamples[i] = samples[i];
  for(uint32_t band = 0; band < DOUBLE_PRECISION_BAND_COUNT; band++)
  {
    DoubleBiquad* filter = &lowFilters[band];
    if(builtGains[band] != 0)
      Equalizer_RunDoubleBand(filter, &filter->history[channel], wideSamples, frameCount);
    else
      Equalizer_TrackDoubleBand(&filter->history[channel], wideSamples, frameCount);
  }
  for(uint32_t i = 0; i < frameCount; i++)
    samples[i] = (float)wideSamples[i];

  for(uint32_t band = 0; band < SINGLE_PRECISION_BAND_COUNT; band++)
  {
    FloatBiquad* filter = &highFilters[band];
    if(builtGains[band + DOUBLE_PRECISION_BAND_COUNT] != 0)
      Equalizer_RunFloatBand(filter, &filter->history[channel], samples, frameCount);
    else
      Equalizer_TrackFloatBand(&filter->history[channel], samples, frameCount);
  }
}

static void Equalizer_Process(float* left, float* right, uint32_t frameCount)
{
  // A rebuild costs several libm calls; one band per packet keeps the ISR
  // inside its 1 ms budget, and the remaining bands converge on later packets.
  for(uint32_t i = 0; i < EQ_BAND_COUNT; i++)
  {
    int16_t gain = eqGains[i];
    if(builtGains[i] != gain)
    {
      Equalizer_BuildBand(i, gain);
      break;
    }
  }

  if(frameCount == 0)
    return;
  Equalizer_ProcessChannel(left, frameCount, 0);
  Equalizer_ProcessChannel(right, frameCount, 1);
}

const DspModule equalizerModule = {Equalizer_Init, Equalizer_Process};
