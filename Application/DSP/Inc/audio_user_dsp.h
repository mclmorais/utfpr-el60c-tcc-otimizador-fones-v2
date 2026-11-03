#if !defined(__AUDIO_USER_DSP_H__)
#define __AUDIO_USER_DSP_H__

#include <stdint.h>

// The DSP sources build on the PC for the tests in tools/dsp_test.py, so they
// own their sample rate instead of including the USB configuration.
#define AUDIO_USER_DSP_SAMPLE_RATE 48000
// With the USB feedback endpoint the host may send one frame more than the nominal 48 per packet.
#define AUDIO_USER_DSP_MAX_PACKET_FRAMES (AUDIO_USER_DSP_SAMPLE_RATE / 1000 + 1)

// Worst DWT cycle counts of one packet since Init, read over ST-LINK by tools/usb_audio_probe.py:
// the whole AudioUserDsp_Process call, and each Module of the Chain in Chain order.
extern volatile uint32_t chainWorstCaseCycles;
extern volatile uint32_t moduleWorstCaseCycles[];

void AudioUserDsp_Init(void);
void AudioUserDsp_Process(uint8_t* frames, uint32_t length);

#endif // __AUDIO_USER_DSP_H__
