#include "audio_user_dsp.h"
#include "cycle_counter.h"
#include "dsp_module.h"
#include "equalizer.h"

// The Chain is fixed at compile time, in this order (docs/adr/0001-cadeia-fixa-de-modulos.md).
// tools/usb_audio_probe.py names moduleWorstCaseCycles in the same order.
static const DspModule* const chain[] = {&equalizerModule};
#define CHAIN_LENGTH (sizeof(chain) / sizeof(chain[0]))

volatile uint32_t chainWorstCaseCycles;
volatile uint32_t moduleWorstCaseCycles[CHAIN_LENGTH];

static float left[AUDIO_USER_DSP_MAX_PACKET_FRAMES];
static float right[AUDIO_USER_DSP_MAX_PACKET_FRAMES];

void AudioUserDsp_Init(void)
{
  CycleCounter_Init();
  chainWorstCaseCycles = 0;
  for(uint32_t i = 0; i < CHAIN_LENGTH; i++)
  {
    moduleWorstCaseCycles[i] = 0;
    chain[i]->init();
  }
}

static int16_t AudioUserDsp_Read(const uint8_t* bytes)
{
  return (int16_t)(bytes[0] | (bytes[1] << 8));
}

static void AudioUserDsp_Write(uint8_t* bytes, float sample)
{
  if(sample > 32767.0f)
    sample = 32767.0f;
  else if(sample < -32768.0f)
    sample = -32768.0f;

  uint16_t result = (uint16_t)(int16_t)sample;
  bytes[0] = result & 0xFF;
  bytes[1] = result >> 8;
}

// frames holds interleaved stereo 16-bit samples; audio_usb_nodes.c asserts that
// a USB packet never exceeds AUDIO_USER_DSP_MAX_PACKET_FRAMES.
void AudioUserDsp_Process(uint8_t* frames, uint32_t length)
{
  uint32_t chainStart = CycleCounter_Read();
  uint32_t frameCount = length / 4;

  for(uint32_t frame = 0; frame < frameCount; frame++)
  {
    left[frame] = AudioUserDsp_Read(frames + 4 * frame);
    right[frame] = AudioUserDsp_Read(frames + 4 * frame + 2);
  }

  for(uint32_t i = 0; i < CHAIN_LENGTH; i++)
  {
    uint32_t moduleStart = CycleCounter_Read();
    chain[i]->process(left, right, frameCount);
    uint32_t moduleCycles = CycleCounter_Read() - moduleStart;
    if(moduleCycles > moduleWorstCaseCycles[i])
      moduleWorstCaseCycles[i] = moduleCycles;
  }

  for(uint32_t frame = 0; frame < frameCount; frame++)
  {
    AudioUserDsp_Write(frames + 4 * frame, left[frame]);
    AudioUserDsp_Write(frames + 4 * frame + 2, right[frame]);
  }

  uint32_t chainCycles = CycleCounter_Read() - chainStart;
  if(chainCycles > chainWorstCaseCycles)
    chainWorstCaseCycles = chainCycles;
}
