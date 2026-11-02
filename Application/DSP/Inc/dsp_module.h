#if !defined(__DSP_MODULE_H__)
#define __DSP_MODULE_H__

#include <stdbool.h>
#include <stdint.h>

// One stage of the Chain in audio_user_dsp.c. Process gets both channels of a
// packet together, so a Module may mix them, and works in place without
// saturating: the Chain saturates to 16 bits once, after the last Module.
// A Module that needs more precision keeps it inside; between Modules the
// blocks are single precision.
// The Chain owns the switch: it runs process whether enabled is on or off, so
// the filter history keeps following the signal, and only keeps or drops the
// output, fading between the two over one packet when enabled changes.
typedef struct DspModule {
  void (*init)(void);
  void (*process)(float* left, float* right, uint32_t frameCount);
  volatile bool* enabled;
} DspModule;

#endif // __DSP_MODULE_H__
