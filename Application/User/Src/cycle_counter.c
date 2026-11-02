#include "cycle_counter.h"
#include "stm32f7xx.h"

void CycleCounter_Init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  // The Cortex-M7 DWT ignores writes until this key unlocks it.
  DWT->LAR = 0xC5ACCE55;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t CycleCounter_Read(void)
{
  return DWT->CYCCNT;
}
