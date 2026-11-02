/**
 ******************************************************************************
 * @file    BSP/Src/touchscreen.c
 * @author  MCD Application Team
 * @brief   This example code shows how to use the touchscreen driver.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2016 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "user_lcd.h"
#include "flash_persistence.h"
#include "equalizer.h"
#include "virtualizer.h"
/** @addtogroup STM32F7xx_HAL_Examples
 * @{
 */

/** @addtogroup BSP
 * @{
 */

/* Global variables ---------------------------------------------------------*/
uint32_t initStatus = TS_ERROR;
TS_StateTypeDef TS_State = {0};

/* Private functions ---------------------------------------------------------*/
extern bool shouldPrintSamples;
extern bool shouldApplyFilter;
bool areInitialCirclesDrawn = false;
static bool isTapLatched = false;

#define CIRCLE_BUTTON_DEBOUNCE_TIMER 100
#define STEP_REPEAT_DELAY_MS         500
#define STEP_REPEAT_INTERVAL_MS      100
static uint32_t nextStepRepeatTick;
// The controller can report a touch nobody made, so a touch counts only after it
// lasts TOUCH_PRESS_MS, and a lift only after no touch is seen for TOUCH_RELEASE_MS.
// The controller reports about every 9 ms and the quickest taps measured last ~28 ms,
// so TOUCH_PRESS_MS rejects one or two stray reports and still accepts those taps.
// The press is timed from its own first reading, so a stray report right after the
// loop stalls (codec init holds it ~300 ms) still has to last TOUCH_PRESS_MS.
#define TOUCH_PRESS_MS   20
#define TOUCH_RELEASE_MS 50
static uint32_t lastTouchedTick;
static uint32_t touchStartTick;
static bool isTouchSeen = false;

// An edit makes the last Save, Undo or Reset no longer describe the gains on screen.
static void MarkEqEdited(void)
{
  resetButton.isPressed = false;
  saveButton.isPressed = false;
  undoButton.isPressed = false;

  if(resetButton.isActive)
  {
    resetButton.isActive = false;
    LCD_UpdateRectangleButton(&resetButton);
  }
  if(saveButton.isActive)
  {
    saveButton.isActive = false;
    LCD_UpdateRectangleButton(&saveButton);
  }
  if(undoButton.isActive)
  {
    undoButton.isActive = false;
    LCD_UpdateRectangleButton(&undoButton);
  }
}

static DisplaySettings CurrentDisplay(void)
{
  return (DisplaySettings){flipButton.isActive, lcdBrightness, darkModeButton.isActive};
}

void Touchscreen_Init(void)
{
  initStatus = BSP_TS_Init(BSP_LCD_GetXSize(), BSP_LCD_GetYSize());
}

void Touchscreen_ButtonHandler(void)
{
  uint16_t touchXPosition, touchYPosition;
  //	uint8_t state = 0;
  //	uint8_t exitTsUseCase = 0;

  if(initStatus != TS_OK)
    return;

  initStatus = BSP_TS_GetState(&TS_State);

  uint32_t now = HAL_GetTick();

  if(!TS_State.touchDetected)
  {
    isTouchSeen = false;
    if(now - lastTouchedTick < TOUCH_RELEASE_MS)
      return;

    isTapLatched = false;
    for(uint32_t i = 0; i < NUMBER_OF_CIRCLE_BUTTONS; i++)
    {
      if(++circleButtons[i].debounceTimer > CIRCLE_BUTTON_DEBOUNCE_TIMER)
        circleButtons[i].debounceTimer = CIRCLE_BUTTON_DEBOUNCE_TIMER;
    }
    return;
  }

  lastTouchedTick = now;
  if(!isTouchSeen)
  {
    isTouchSeen = true;
    touchStartTick = now;
  }
  if(now - touchStartTick < TOUCH_PRESS_MS)
    return;

  touchXPosition = TS_State.touchX[0];
  touchYPosition = TS_State.touchY[0];

  if(flipButton.isActive)
  {
    touchXPosition = BSP_LCD_GetXSize() - 1 - touchXPosition;
    touchYPosition = BSP_LCD_GetYSize() - 1 - touchYPosition;
  }

  for(uint32_t i = 0; i < NUMBER_OF_CIRCLE_BUTTONS; i++)
  {
    if((touchYPosition > circleButtons[i].y - circleButtons[i].radius) &&
       (touchYPosition < circleButtons[i].y + circleButtons[i].radius))
    {
      if((touchXPosition > circleButtons[i].x - circleButtons[i].radius) &&
         (touchXPosition < circleButtons[i].x + circleButtons[i].radius))
      {

        if(circleButtons[i].debounceTimer == CIRCLE_BUTTON_DEBOUNCE_TIMER)
          LCD_UpdateButton(i, !circleButtons[i].isPressed, false);

        circleButtons[i].debounceTimer = 0;

        return;
      }
    }
  }

  if(touchXPosition > flipButton.x && touchXPosition < flipButton.x + flipButton.width &&
     touchYPosition > flipButton.y && touchYPosition < flipButton.y + flipButton.height)
  {
    if(!isTapLatched)
    {
      isTapLatched = true;
      LCD_SetFlip(!flipButton.isActive);
      FlashPersistence_WriteSettings(CurrentDisplay());
    }

    return;
  }

  if(touchXPosition > brightnessButton.x && touchXPosition < brightnessButton.x + brightnessButton.width &&
     touchYPosition > brightnessButton.y && touchYPosition < brightnessButton.y + brightnessButton.height)
  {
    if(!isTapLatched)
    {
      isTapLatched = true;
      LCD_CycleBrightness();
      FlashPersistence_WriteSettings(CurrentDisplay());
    }

    return;
  }

  if(touchXPosition > darkModeButton.x && touchXPosition < darkModeButton.x + darkModeButton.width &&
     touchYPosition > darkModeButton.y && touchYPosition < darkModeButton.y + darkModeButton.height)
  {
    if(!isTapLatched)
    {
      isTapLatched = true;
      LCD_SetDarkMode(!darkModeButton.isActive);
      FlashPersistence_WriteSettings(CurrentDisplay());
    }

    return;
  }

  if(circleButtons[VIRT_CIRCLE_BUTTON].isActive)
  {
    if(touchXPosition > virtSwitchButton.x && touchXPosition < virtSwitchButton.x + virtSwitchButton.width &&
       touchYPosition > virtSwitchButton.y && touchYPosition < virtSwitchButton.y + virtSwitchButton.height)
    {
      if(!isTapLatched)
      {
        isTapLatched = true;
        virtEnabled = !virtEnabled;
        LCD_UpdateSwitchButton(&virtSwitchButton, virtEnabled);
        FlashPersistence_WriteSettings(CurrentDisplay());
      }

      return;
    }

    for(uint32_t i = 0; i < CROSSFEED_PRESET_COUNT; i++)
    {
      if(touchXPosition > presetButtons[i].x && touchXPosition < presetButtons[i].x + presetButtons[i].width &&
         touchYPosition > presetButtons[i].y && touchYPosition < presetButtons[i].y + presetButtons[i].height)
      {
        if(!isTapLatched)
        {
          isTapLatched = true;
          crossfeedPreset = (uint8_t)i;
          LCD_UpdateState();
          FlashPersistence_WriteSettings(CurrentDisplay());
        }

        break;
      }
    }

    return;
  }

  if(circleButtons[EQ_CIRCLE_BUTTON].isActive)
  {
    if(touchXPosition > eqSwitchButton.x && touchXPosition < eqSwitchButton.x + eqSwitchButton.width &&
       touchYPosition > eqSwitchButton.y && touchYPosition < eqSwitchButton.y + eqSwitchButton.height)
    {
      if(!isTapLatched)
      {
        isTapLatched = true;
        eqEnabled = !eqEnabled;
        LCD_UpdateSwitchButton(&eqSwitchButton, eqEnabled);
        FlashPersistence_WriteSettings(CurrentDisplay());
      }

      return;
    }

    if(touchYPosition > previousPageButton.y && touchYPosition < previousPageButton.y + previousPageButton.height)
    {
      int8_t step = 0;
      if(touchXPosition > previousPageButton.x && touchXPosition < previousPageButton.x + previousPageButton.width)
        step = -1;
      else if(touchXPosition > nextPageButton.x && touchXPosition < nextPageButton.x + nextPageButton.width)
        step = 1;

      if(step != 0)
      {
        if(!isTapLatched)
        {
          isTapLatched = true;
          LCD_ChangeEqPage(step);
        }

        return;
      }
    }

    if(touchYPosition > saveButton.y && touchYPosition < saveButton.y + saveButton.height)
    {
      if(touchXPosition > saveButton.x && touchXPosition < saveButton.x + saveButton.width)
      {
        if(!saveButton.isPressed)
        {
          saveButton.isPressed = true;
          saveButton.isActive = true;
          resetButton.isPressed = false;
          resetButton.isActive = false;
          undoButton.isPressed = false;
          undoButton.isActive = false;
          LCD_UpdateRectangleButton(&saveButton);
          LCD_UpdateRectangleButton(&undoButton);
          LCD_UpdateRectangleButton(&resetButton);
          FlashPersistence_Write(CurrentDisplay());
        }

        return;
      }
    }
    else if(touchYPosition > undoButton.y && touchYPosition < undoButton.y + undoButton.height)
    {
      if(touchXPosition > undoButton.x && touchXPosition < saveButton.x + undoButton.width)
      {
        if(!undoButton.isPressed)
        {
          saveButton.isPressed = false;
          saveButton.isActive = false;
          undoButton.isPressed = true;
          undoButton.isActive = true;
          resetButton.isPressed = false;
          resetButton.isActive = false;
          LCD_UpdateRectangleButton(&saveButton);
          LCD_UpdateRectangleButton(&undoButton);
          LCD_UpdateRectangleButton(&resetButton);
          FlashPersistence_Restore();
          LCD_DisplayEqPage();
        }

        return;
      }
    }
    else if(touchYPosition > resetButton.y && touchYPosition < resetButton.y + resetButton.height)
    {
      if(touchXPosition > resetButton.x && touchXPosition < resetButton.x + resetButton.width)
      {
        if(!resetButton.isPressed)
        {
          saveButton.isPressed = false;
          saveButton.isActive = false;
          undoButton.isPressed = false;
          undoButton.isActive = false;
          resetButton.isPressed = true;
          resetButton.isActive = true;
          LCD_UpdateRectangleButton(&saveButton);
          LCD_UpdateRectangleButton(&undoButton);
          LCD_UpdateRectangleButton(&resetButton);
          for(uint8_t i = 0; i < EQ_BAND_COUNT; i++)
            eqGains[i] = 0;
          LCD_DisplayEqPage();
        }

        return;
      }
    }

    for(uint8_t i = 0; i < NUMBER_OF_SLIDER_BUTTONS; i++)
    {
      int8_t band = LCD_ColumnToBand(i);
      if(band < 0)
        continue;

      int8_t step = LCD_StepButtonAt(i, touchXPosition, touchYPosition);
      if(step != 0)
      {
        bool isFirstTap = !isTapLatched;
        if(isFirstTap || (int32_t)(now - nextStepRepeatTick) >= 0)
        {
          isTapLatched = true;
          nextStepRepeatTick = now + (isFirstTap ? STEP_REPEAT_DELAY_MS : STEP_REPEAT_INTERVAL_MS);
          int16_t gain = eqGains[band] + step;
          if(gain >= EQ_GAIN_MIN && gain <= EQ_GAIN_MAX)
          {
            eqGains[band] = gain;
            LCD_DisplayKnob(i, gain);
            MarkEqEdited();
          }
        }

        return;
      }

      if((touchYPosition > sliderKnobs[i].sliderY + 10) &&
         (touchYPosition < sliderKnobs[i].sliderY + sliderKnobs[i].sliderHeight - 10))
      {
        if((touchXPosition > sliderKnobs[i].sliderX) &&
           (touchXPosition < sliderKnobs[i].sliderX + sliderKnobs[i].sliderWidth))
        {
          if(++sliderKnobs[i].debounceCount >= sliderKnobs[i].debouceLimit)
          {
            int16_t gain = LCD_TranslateKnobPositionToGain(i, touchYPosition);
            eqGains[band] = gain;
            LCD_DisplayKnob(i, gain);
            sliderKnobs[i].debounceCount = 0;
            MarkEqEdited();
            return;
          }
        }
      }
    }
  }
}

/**
 * @}
 */

/**
 * @}
 */
