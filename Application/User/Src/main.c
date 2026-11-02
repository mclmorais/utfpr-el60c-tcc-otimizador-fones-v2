// includes --------------------------------------------------------------------
#include "main.h"
#include "stlogo.h"
#include "usb_audio.h"
#include "audio_speaker_node.h"
#include "usart.h"
#include "flash_persistence.h"
#include "user_lcd.h"
#include "audio_user_dsp.h"

// private variables -----------------------------------------------------------
USBD_HandleTypeDef USBD_Device;
// static int32_t pending_buffer   = -1;
// static int32_t active_area      = INVALID_AREA;
// static uint32_t ImageIndex      = 0;
#define WATCHDOG_TRAIL_STEP_MS 15

static uint32_t watchdogTick = 0;
uint32_t serialTimer = 0;
uint32_t touchscreenTimer = 0;
uint32_t serialSendTimer = 0;
bool shouldPrintSamples = false;

uint8_t pColLeft[] = {0x00, 0x00, 0x01, 0x8F};  // 0 -> 399
uint8_t pColRight[] = {0x01, 0x90, 0x03, 0x1F}; // 400 -> 799
uint8_t pPage[] = {0x00, 0x00, 0x01, 0xDF};     // 0 -> 479
uint8_t pSyncLeft[] = {0x02, 0x15};             // Scan @ 533

// private function prototypes -------------------------------------------------
// static void OnError_Handler(uint32_t condition);
static void SystemClock_Config(void);
static void CPU_CACHE_Enable(void);
static void USB_Init(void);

// private functions -----------------------------------------------------------

/**
 * @brief  Main program
 * @param  None
 * @retval None
 */
int main(void)
{

  CPU_CACHE_Enable();

  /* STM32F7xx HAL library initialization:
	   - Configures the Flash ART accelerator on ITCM interface
	   - Configures the Systick to generate an interrupt each 1 msec
	   - Sets NVIC Group Priority to 4
	   - Low Level Initialization
	 */
  HAL_Init();

  // configures the system clock to have a frequency of 200 MHz
  SystemClock_Config();

  BSP_SDRAM_Init();
  FlashPersistence_Restore();
  FlashPersistence_RestoreModuleSwitches();
  AudioUserDsp_Init();
  USB_Init();

  HAL_Delay(1000);

  USART1_UART_Init();
  DisplaySettings display = FlashPersistence_ReadDisplay();
  LCD_Init(display.isDark);
  LCD_SetFlip(display.isFlipped);
  LCD_SetBrightness(display.brightness);
  Touchscreen_Init();

  uint8_t initString[] = "\r\n--- Horoscope Initialization Complete! ---\r\n";
  HAL_UART_Transmit(&UART1_Handle, initString, sizeof(initString), 10);

  // Infinite loop
  while(1)
  {
    // the codec shares I2C4 with the touch controller, so only this loop may talk to it
    AUDIO_SpeakerApplyCodecRequests();

    if(++touchscreenTimer > 40)
    {
      Touchscreen_ButtonHandler();
      touchscreenTimer = 0;
    }

    if(HAL_GetTick() - watchdogTick >= WATCHDOG_TRAIL_STEP_MS)
    {
      LCD_AdvanceWatchdogTrail();
      watchdogTick = HAL_GetTick();
    }
  }
}

/**
 * @brief  CPU L1-Cache enable.
 * @param  None
 * @retval None
 */
static void CPU_CACHE_Enable(void)
{
  SCB_EnableICache();
  SCB_EnableDCache();
}

/**
 * @brief  System Clock Configuration
 *         The system Clock is configured as follow :
 *            System Clock source            = PLL (HSE)
 *            SYSCLK(Hz)                     = 192000000
 *            HCLK(Hz)                       = 192000000
 *            AHB Prescaler                  = 1
 *            APB1 Prescaler                 = 4
 *            APB2 Prescaler                 = 2
 *            HSE Frequency(Hz)              = 25000000
 *            PLL_M                          = 25
 *            PLL_N                          = 384
 *            PLL_P                          = 2
 *            PLL_Q                          = 8
 *            PLL_R                          = 7
 *            VDD(V)                         = 3.3
 *            Main regulator output voltage  = Scale1 mode
 *            Flash Latency(WS)              = 6
 * @param  None
 * @retval None
 */
void SystemClock_Config(void)
{
  RCC_ClkInitTypeDef RCC_ClkInitStruct;
  RCC_OscInitTypeDef RCC_OscInitStruct;
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct;

  // Enable HSE Oscillator and activate PLL with HSE as source
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  //	RCC_OscInitStruct.HSIState = RCC_HSI_OFF;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 400;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 8;
  RCC_OscInitStruct.PLL.PLLR = 7;

  if(HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    Error_Handler();

  // Select PLLSAI output as USB clock source
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_CLK48;
  PeriphClkInitStruct.Clk48ClockSelection = RCC_CLK48SOURCE_PLL;
  if(HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    Error_Handler();

  // Select PLL as system clock source and configure the HCLK, PCLK1 and PCLK2 clocks dividers
  RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if(HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_6) != HAL_OK)
    Error_Handler();
}

/**
 * @brief  This function is executed in case of error occurrence.
 * @param  None
 * @retval None
 */
void Error_Handler(void)
{
  // User may add here some code to deal with this error
  while(1)
  {
  }
}
void USBD_error_handler(void)
{
  Error_Handler();
}

///**
// * @brief  On Error Handler on condition TRUE.
// * @param  condition : Can be TRUE or FALSE
// * @retval None
// */
//static void OnError_Handler(uint32_t condition)
//{
//	if(condition)
//	{
//		BSP_LED_On(LED1);
//		while(1); // blocking on error
//	}
//}

static void USB_Init(void)
{
  BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
  BSP_LCD_SetTextColor(LCD_COLOR_BLACK);

  // Init Device Library
  BSP_LCD_DisplayStringAt(0, 10, (uint8_t*)"USB Device Library ..... ", LEFT_MODE);
  USBD_Init(&USBD_Device, &AUDIO_Desc, 0);
  BSP_LCD_DisplayStringAt(420, 10, (uint8_t*)"OK", LEFT_MODE);

  // Add Supported Class
  BSP_LCD_DisplayStringAt(0, 30, (uint8_t*)"USB Register Class ..... ", LEFT_MODE);
  USBD_RegisterClass(&USBD_Device, USBD_AUDIO_CLASS);
  BSP_LCD_DisplayStringAt(420, 30, (uint8_t*)"OK", LEFT_MODE);

  // Add Interface callbacks for AUDIO Class
  BSP_LCD_DisplayStringAt(0, 50, (uint8_t*)"USB Register Interface . ", LEFT_MODE);
  USBD_AUDIO_RegisterInterface(&USBD_Device, &audio_class_interface);
  BSP_LCD_DisplayStringAt(420, 50, (uint8_t*)"OK", LEFT_MODE);

  // Start Device Process
  BSP_LCD_DisplayStringAt(0, 70, (uint8_t*)"USB Init ............... ", LEFT_MODE);
  USBD_Start(&USBD_Device);
  BSP_LCD_DisplayStringAt(420, 70, (uint8_t*)"OK", LEFT_MODE);
}

#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t* file, uint32_t line)
{
  /* User can add his own implementation to report the file name and line number,
	   ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

  // Infinite loop
  while(1)
  {
  }
}
#endif

//*********************** (C) COPYRIGHT STMicroelectronics *****END OF FILE***
