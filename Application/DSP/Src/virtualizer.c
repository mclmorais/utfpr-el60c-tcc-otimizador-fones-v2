#include "virtualizer.h"
#include "audio_user_dsp.h"
#include "virtual_speakers_filters.h"
#include <math.h>
#include <string.h>

const CrossfeedPreset crossfeedPresets[CROSSFEED_PRESET_COUNT] = {
  {"Padrao",    700,  4.5f},
  {"Chu Moy",   700,  6.0f},
  {"Jan Meier", 650,  9.5f},
  {"Forte",     1000, 3.0f},
};

volatile bool virtEnabled = false;
volatile VirtualizerTechnique virtTechnique = VIRTUALIZER_CROSSFEED;
volatile uint8_t crossfeedPreset = 0;

VirtualizerTechnique Virtualizer_TechniqueFromSavedWord(uint32_t word)
{
  return word < VIRTUALIZER_TECHNIQUE_COUNT ? (VirtualizerTechnique)word : VIRTUALIZER_CROSSFEED;
}

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

// Follows the signal without producing the output, so a switch to the Crossfeed finds its state ready.
static void Crossfeed_Follow(const float* left, const float* right, uint32_t frameCount)
{
  if(crossfeedPreset != builtPreset)
    Crossfeed_Build(crossfeedPreset);

  for(uint32_t frame = 0; frame < frameCount; frame++)
    crossfeed = feedGain * (right[frame] - left[frame]) + pole * crossfeed;
}

// Direct-form FIR whose history is stored twice, back to back, so the taps of
// any sample are one run of memory and the loop never wraps.
typedef struct Fir {
  float history[2 * VIRTUAL_SPEAKERS_TAPS];
  uint32_t newest;
} Fir;

_Static_assert(VIRTUAL_SPEAKERS_TAPS % 4 == 0, "the FIR loop is unrolled 4 times");

// The shuffler (docs/adr/0002-alto-falantes-virtuais-mudam-o-timbre-mono.md):
// with a symmetric head, the two speakers need only a sum and a difference filter.
static Fir sumFir;
static Fir differenceFir;

static void Fir_Push(Fir* fir, float input)
{
  fir->newest = (fir->newest == 0 ? VIRTUAL_SPEAKERS_TAPS : fir->newest) - 1;
  fir->history[fir->newest] = input;
  fir->history[fir->newest + VIRTUAL_SPEAKERS_TAPS] = input;
}

static float Fir_Step(Fir* fir, const float* coefficients, float input)
{
  Fir_Push(fir, input);
  const float* past = &fir->history[fir->newest];
  // Four sums keep four multiply-adds in flight instead of waiting on one.
  float sum0 = 0;
  float sum1 = 0;
  float sum2 = 0;
  float sum3 = 0;
  for(uint32_t tap = 0; tap < VIRTUAL_SPEAKERS_TAPS; tap += 4)
  {
    sum0 += coefficients[tap] * past[tap];
    sum1 += coefficients[tap + 1] * past[tap + 1];
    sum2 += coefficients[tap + 2] * past[tap + 2];
    sum3 += coefficients[tap + 3] * past[tap + 3];
  }
  return (sum0 + sum1) + (sum2 + sum3);
}

// left = sum + difference and right = sum - difference, so swapping the inputs
// only negates the difference and the output is mirrored exactly.
static void VirtualSpeakers_Process(float* left, float* right, uint32_t frameCount)
{
  for(uint32_t frame = 0; frame < frameCount; frame++)
  {
    float sum = Fir_Step(&sumFir, virtualSpeakersSum, 0.5f * (left[frame] + right[frame]));
    float difference = Fir_Step(&differenceFir, virtualSpeakersDifference, 0.5f * (left[frame] - right[frame]));
    left[frame] = sum + difference;
    right[frame] = sum - difference;
  }
}

// Follows the signal without computing the filters, so a switch to the Virtual Speakers finds their history ready.
static void VirtualSpeakers_Follow(const float* left, const float* right, uint32_t frameCount)
{
  for(uint32_t frame = 0; frame < frameCount; frame++)
  {
    Fir_Push(&sumFir, 0.5f * (left[frame] + right[frame]));
    Fir_Push(&differenceFir, 0.5f * (left[frame] - right[frame]));
  }
}

static VirtualizerTechnique activeTechnique;
static float otherLeft[AUDIO_USER_DSP_MAX_PACKET_FRAMES];
static float otherRight[AUDIO_USER_DSP_MAX_PACKET_FRAMES];

static void Virtualizer_Init(void)
{
  activeTechnique = virtTechnique;
  Crossfeed_Build(crossfeedPreset);
  crossfeed = 0;
  memset(&sumFir, 0, sizeof(sumFir));
  memset(&differenceFir, 0, sizeof(differenceFir));
}

static void Technique_Process(VirtualizerTechnique technique, float* left, float* right, uint32_t frameCount)
{
  if(technique == VIRTUALIZER_CROSSFEED)
    Crossfeed_Process(left, right, frameCount);
  else
    VirtualSpeakers_Process(left, right, frameCount);
}

static void Technique_Follow(VirtualizerTechnique technique, const float* left, const float* right, uint32_t frameCount)
{
  if(technique == VIRTUALIZER_CROSSFEED)
    Crossfeed_Follow(left, right, frameCount);
  else
    VirtualSpeakers_Follow(left, right, frameCount);
}

// Every packet feeds both Techniques, but only the active one is computed. In the
// packet where the Technique changes both are, and the output slides linearly from
// the old to the new over the packet, like the Chain's switch.
static void Virtualizer_Process(float* left, float* right, uint32_t frameCount)
{
  VirtualizerTechnique technique = virtTechnique;
  VirtualizerTechnique other =
      technique == VIRTUALIZER_CROSSFEED ? VIRTUALIZER_VIRTUAL_SPEAKERS : VIRTUALIZER_CROSSFEED;

  if(technique == activeTechnique)
  {
    Technique_Follow(other, left, right, frameCount);
    Technique_Process(technique, left, right, frameCount);
    return;
  }

  // The old Technique runs on a copy of the input and the new one in place, so both histories take the packet.
  memcpy(otherLeft, left, frameCount * sizeof(float));
  memcpy(otherRight, right, frameCount * sizeof(float));
  Technique_Process(activeTechnique, otherLeft, otherRight, frameCount);
  Technique_Process(technique, left, right, frameCount);
  for(uint32_t frame = 0; frame < frameCount; frame++)
  {
    float weight = (float)(frame + 1) / (float)frameCount;
    left[frame] = otherLeft[frame] + (left[frame] - otherLeft[frame]) * weight;
    right[frame] = otherRight[frame] + (right[frame] - otherRight[frame]) * weight;
  }
  activeTechnique = technique;
}

const DspModule virtualizerModule = {Virtualizer_Init, Virtualizer_Process, &virtEnabled};
