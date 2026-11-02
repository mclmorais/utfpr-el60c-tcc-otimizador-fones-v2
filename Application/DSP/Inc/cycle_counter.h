#if !defined(__CYCLE_COUNTER_H__)
#define __CYCLE_COUNTER_H__

#include <stdint.h>

// The firmware implements these with the Cortex-M7 DWT in Application/User/Src/cycle_counter.c;
// tools/dsp_pc/cycle_counter.c stubs them so the DSP sources build on the PC without the ST HAL.
void CycleCounter_Init(void);
uint32_t CycleCounter_Read(void);

#endif // __CYCLE_COUNTER_H__
