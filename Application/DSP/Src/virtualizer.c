#include "virtualizer.h"
#include "audio_user_dsp.h"
#include <math.h>

#define PI 3.14159265358979323846

const CrossfeedPreset crossfeedPresets[CROSSFEED_PRESET_COUNT] = {
  {"Padrao", 700, 4.5f},
};
const uint8_t crossfeedPresetCount = CROSSFEED_PRESET_COUNT;

volatile bool virtEnabled = false;
volatile VirtualizerTechnique virtTechnique = VIRTUALIZER_CROSSFEED;
volatile uint8_t crossfeedPreset = 0;

// bs2b feeds each ear the opposite channel through a first-order lowpass of gain G
// and its own channel through a first-order high shelf. Its shelf, given the
// lowpass's corner and gain, is exactly 1 - G * lowpass; bs2b's own choice of
// both leaves a mono signal 1.8 dB duller in the treble. With the exact
// complement, left = left + G * lowpass(right - left) and the mirror for
// right, so one lowpass of the difference serves both ears and mono passes
// unchanged (docs/adr/0001-cadeia-fixa-de-modulos.md).
static float feedGain;
static float pole;
static float crossfeed;
static uint8_t builtPreset;

static void Crossfeed_Build(uint8_t preset)
{
  // The crossed path sits levelDb below the direct one at low frequencies: G / (1 - G) = 10^(-level / 20).
  double ratio = pow(10.0, -crossfeedPresets[preset].levelDb / 20.0);
  double gain = ratio / (1.0 + ratio);
  double x = exp(-2.0 * PI * crossfeedPresets[preset].cutoffHz / AUDIO_USER_DSP_SAMPLE_RATE);
  pole = (float)x;
  feedGain = (float)(gain * (1.0 - x));
  builtPreset = preset;
}

static void Crossfeed_Process(float* left, float* right, uint32_t frameCount)
{
  if(crossfeedPreset != builtPreset)
    Crossfeed_Build(crossfeedPreset);

  for(uint32_t frame = 0; frame < frameCount; frame++)
  {
    crossfeed = feedGain * (right[frame] - left[frame]) + pole * crossfeed;
    left[frame] += crossfeed;
    right[frame] -= crossfeed;
  }
}

static void Virtualizer_Init(void)
{
  Crossfeed_Build(crossfeedPreset);
  crossfeed = 0;
}

static void Virtualizer_Process(float* left, float* right, uint32_t frameCount)
{
  switch(virtTechnique)
  {
  case VIRTUALIZER_CROSSFEED:
    Crossfeed_Process(left, right, frameCount);
    break;
  }
}

const DspModule virtualizerModule = {Virtualizer_Init, Virtualizer_Process, &virtEnabled};
