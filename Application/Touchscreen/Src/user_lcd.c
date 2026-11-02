#include "user_lcd.h"
#include "audio_user_dsp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FOREGROUND_LAYER_OFFSET  (800 * 480 * sizeof(uint16_t))  // Adjust this offset based on your needs

// pictures -----------------------------------------------------------
#include "utfprlogo.h"

// private defines -------------------------------------------------------------
#define VSYNC             1
#define VBP               1
#define VFP               1
#define VACT              480
#define HSYNC             1
#define HBP               1
#define HFP               1
#define HACT              400 // screen divided into two areas

#define INVALID_AREA      0
#define LEFT_AREA         1
#define RIGHT_AREA        2
#define WVGA_RES_X        800
#define WVGA_RES_Y        480

#define ARGB8888_BYTE_PER_PIXEL 4
#define RGB565_BYTE_PER_PIXEL   2
#define LCD_FRAME_BUFFER        0xC0000000
#define LAYER_SIZE_X            240
#define LAYER_SIZE_Y            130
#define LAYER_BYTE_PER_PIXEL    RGB565_BYTE_PER_PIXEL

#define BUTTON_BORDER_SIZE 2
#define Y_BAR_POSITION -60
#define EQ_PAGE_COUNT ((EQ_BAND_COUNT + NUMBER_OF_SLIDER_BUTTONS - 1) / NUMBER_OF_SLIDER_BUTTONS)
#define MADCTR_MIRROR_XY 0xC0 // MY | MX
#define STEP_BUTTON_HEIGHT 36
#define STEP_BUTTON_MARGIN 6
#define STEP_BUTTON_GAP 4
#define GAIN_TEXT_GAP 6
#define MOON_RADIUS 14
#define MOON_BITE_OFFSET_X 7
#define MOON_BITE_OFFSET_Y 5
#define LOGO_INK_BLUE 24
#define LOGO_YELLOW 0xFFCF00

// private function declarations --------------------------------------
static void     LCD_LayertInit(uint16_t LayerIndex, uint32_t Address);
static void     Display_StartupScreen(void);
static void     SelectTheme(bool isDark);
static void     DrawLogo(uint16_t x, uint16_t y);
static void     LCD_UpdateDarkModeButton(void);
static void     LCD_UpdateBrightnessButton(void);

// variables ----------------------------------------------------------
typedef struct LcdTheme
{
  uint32_t background;
  uint32_t foreground;
  uint32_t buttonIdle;
  uint32_t buttonActive;
} LcdTheme;

static const LcdTheme themes[] = {
  {LCD_COLOR_WHITE, LCD_COLOR_BLACK, LCD_COLOR_LIGHTGRAY, LCD_COLOR_LIGHTGREEN},
  {0xFF202020, 0xFFF0F0F0, 0xFF404040, 0xFF2E7D32}
};
static const LcdTheme* theme = &themes[0];

// !! THE NUMBER_OF_CIRCLE_BUTTONS DEFINE HAS TO BE UP TO DATE WITH THE NUMBER OF BUTTONS DEFINED HERE
CircleButtonTypeDef circleButtons[] = {
  //  x,   y,  r,   on,     off,  pressed, independent, active
  { 100, 440, 30, "EQ",  "", false, false, false, 0},
  { 150, 440, 30, "Vol", "", false, false, false, 0},
};

RectangleButton saveButton = {
  .x = 20,
  .y = 200,
  .width = 180,
  .height = 50,
  .text = "Salvar",
  .isPressed = false,
  .isActive = false
};

RectangleButton undoButton = {
  .x = 20,
  .y = 200 + 60,
  .width = 180,
  .height = 50,
  .text = "Desfazer",
  .isPressed = false,
  .isActive = false
};


RectangleButton resetButton = {
  .x = 20,
  .y = 200 + 60 + 60,
  .width = 180,
  .height = 50,
  .text = "Redefinir",
  .isPressed = false,
  .isActive = false
};


RectangleButton previousPageButton = {
  .x = 145,
  .y = 415,
  .width = 45,
  .height = 50,
  .text = "<",
  .isPressed = false,
  .isActive = false
};

RectangleButton nextPageButton = {
  .x = 198,
  .y = 415,
  .width = 45,
  .height = 50,
  .text = ">",
  .isPressed = false,
  .isActive = false
};

RectangleButton flipButton = {
  .x = 738,
  .y = 415,
  .width = 56,
  .height = 50,
  .text = "180",
  .isPressed = false,
  .isActive = false
};

static const uint8_t brightnessLevels[] = {100, 75, 50, 25};
static char brightnessText[5];
uint8_t lcdBrightness = 100;

RectangleButton brightnessButton = {
  .x = 738,
  .y = 355,
  .width = 56,
  .height = 50,
  .text = brightnessText,
  .isPressed = false,
  .isActive = false
};

RectangleButton darkModeButton = {
  .x = 738,
  .y = 295,
  .width = 56,
  .height = 50,
  .text = "",
  .isPressed = false,
  .isActive = false
};

static uint8_t eqPage = 0;

SliderKnob sliderKnobs[] = {
  { 200 + 50, 70, 60, 320, 230, 20, 5, 7},
  { 200 + 110, 70, 60, 320, 230, 20, 5, 7},
  { 200 + 170, 70, 60, 320, 230, 20, 5, 7},
  { 200 + 230, 70, 60, 320, 230, 20, 5, 7},
  { 200 + 290, 70, 60, 320, 230, 20, 5, 7},
  { 200 + 350, 70, 60, 320, 230, 20, 5, 7},
  { 200 + 410, 70, 60, 320, 230, 20, 5, 7},
  { 200 + 470, 70, 60, 320, 230, 20, 5, 7}
};

// external variable declarations -------------------------------------
extern LTDC_HandleTypeDef hltdc_discovery;
/**
 * @brief  Initializes the DSI LCD.
 * The ititialization is done as below:
 *     - DSI PLL ititialization
 *     - DSI ititialization
 *     - LTDC ititialization
 *     - OTM8009A LCD Display IC Driver ititialization
 * @param  isDark: theme drawn from the first frame, so a saved dark mode never flashes white
 * @retval LCD state
 */
void LCD_Init(bool isDark)
{
  uint8_t lcd_status = LCD_OK;

  lcd_status = BSP_LCD_Init();
  while(lcd_status != LCD_OK);

  // Initialize LTDC layer 0 iused for Hint
  LCD_LayertInit(0, LAYER0_ADDRESS);
  BSP_LCD_SelectLayer(0);

  BSP_LCD_LayerDefaultInit(0, LCD_FB_START_ADDRESS);

  SelectTheme(isDark);

  // The OTM8009A driver turns on content adaptive brightness, which rescales the backlight from the image and would fight LCD_SetBrightness.
  uint8_t cabcOff[] = {OTM8009A_CMD_WRCABC, 0x00};
  DSI_IO_WriteCmd(0, cabcOff);

  Display_StartupScreen();
}

/**
 * @brief  Initializes the LCD layers.
 * @param  LayerIndex: Layer foreground or background
 * @param  FB_Address: Layer frame buffer
 * @retval None
 */
static void LCD_LayertInit(uint16_t LayerIndex, uint32_t Address)
{
  LCD_LayerCfgTypeDef Layercfg;

  // Layer Init
  Layercfg.WindowX0 = 0;
  Layercfg.WindowX1 = BSP_LCD_GetXSize() / 2;
  Layercfg.WindowY0 = 0;
  Layercfg.WindowY1 = BSP_LCD_GetYSize();
  Layercfg.PixelFormat = LTDC_PIXEL_FORMAT_ARGB8888;
  Layercfg.FBStartAdress = Address;
  Layercfg.Alpha = 255;
  Layercfg.Alpha0 = 0;
  Layercfg.Backcolor.Blue = 0;
  Layercfg.Backcolor.Green = 0;
  Layercfg.Backcolor.Red = 0;
  Layercfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
  Layercfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
  Layercfg.ImageWidth = BSP_LCD_GetXSize() / 2;
  Layercfg.ImageHeight = BSP_LCD_GetYSize();

  HAL_LTDC_ConfigLayer(&hltdc_discovery, &Layercfg, LayerIndex);
}

static void Display_StartupScreen(void)
{
  // sets lcd foreground layer
  BSP_LCD_SelectLayer(0);

  // clears the lcd
  BSP_LCD_SetBackColor(theme->background);
  BSP_LCD_Clear(theme->background);

  LCD_UpdateState();
}

void LCD_UpdateState()
{

  if(circleButtons[0].isActive)
  {
    LCD_DisplayEqPage();

    // displays option buttons
    LCD_UpdateRectangleButton(&saveButton);
    LCD_UpdateRectangleButton(&resetButton);
    LCD_UpdateRectangleButton(&undoButton);
    LCD_UpdateRectangleButton(&previousPageButton);
    LCD_UpdateRectangleButton(&nextPageButton);
  }
  else
  {
    // default state: displays background

    // clears display
    BSP_LCD_Clear(theme->background);

    // displays logo
    DrawLogo(20, 20);

    // displays background yellow line
    BSP_LCD_SetFont(&Font24);
    BSP_LCD_SetTextColor(LCD_COLOR_UTFPRYELLOW);
    BSP_LCD_FillRect(0, BSP_LCD_GetYSize() / 2 + Y_BAR_POSITION, BSP_LCD_GetXSize(), 90);

    // displays background text
    BSP_LCD_SetBackColor(LCD_COLOR_UTFPRYELLOW);
    BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
    BSP_LCD_DisplayStringAt(0, BSP_LCD_GetYSize() / 2 + Y_BAR_POSITION + 15, (uint8_t *)"V2.0a", CENTER_MODE);
    BSP_LCD_DisplayStringAt(0, BSP_LCD_GetYSize() / 2 + Y_BAR_POSITION + 45, (uint8_t *)"Branch Biquad", CENTER_MODE);
  }

  // circle buttons are always updated
  for(uint8_t i = 0; i < NUMBER_OF_CIRCLE_BUTTONS; i++)
  {
    // draws button border
    BSP_LCD_SetTextColor(theme->foreground);
    BSP_LCD_FillCircle(circleButtons[i].x, circleButtons[i].y, circleButtons[i].radius);

    // draws button inner circle
    BSP_LCD_SetTextColor(circleButtons[i].isPressed ? theme->buttonActive : theme->buttonIdle);
    BSP_LCD_FillCircle(circleButtons[i].x, circleButtons[i].y, circleButtons[i].radius - 3);

    BSP_LCD_SetTextColor(theme->foreground);
    BSP_LCD_SetBackColor(theme->background);
    BSP_LCD_DisplayStringAt(circleButtons[i].x - circleButtons[i].radius - 12, circleButtons[i].y - circleButtons[i].radius - 24, (uint8_t *)circleButtons[i].onText, LEFT_MODE);


  }

  LCD_UpdateRectangleButton(&flipButton);
  LCD_UpdateBrightnessButton();
  LCD_UpdateDarkModeButton();
}

static void LCD_UpdateBrightnessButton(void)
{
  sprintf(brightnessText, "%u%%", lcdBrightness);

  // Font24 fits only 3 characters in the button, and "100%" needs 4.
  sFONT* font = BSP_LCD_GetFont();
  BSP_LCD_SetFont(&Font16);
  LCD_UpdateRectangleButton(&brightnessButton);
  BSP_LCD_SetFont(font);
}

void LCD_SetBrightness(uint8_t brightness)
{
  lcdBrightness = brightness;
  BSP_LCD_SetBrightness(brightness);
  LCD_UpdateBrightnessButton();
}

void LCD_CycleBrightness(void)
{
  // Steps to the next dimmer level and wraps to the brightest; a saved level between entries steps to the one below it.
  uint8_t next = brightnessLevels[0];
  for(uint8_t i = 0; i < sizeof(brightnessLevels); i++)
  {
    if(brightnessLevels[i] < lcdBrightness)
    {
      next = brightnessLevels[i];
      break;
    }
  }
  LCD_SetBrightness(next);
}

void LCD_SetFlip(bool isFlipped)
{
  flipButton.isActive = isFlipped;

  // Mirroring both axes of the panel's scan turns the image 180 degrees without touching the framebuffer.
  uint8_t madctl[] = {OTM8009A_CMD_MADCTR, OTM8009A_MADCTR_MODE_LANDSCAPE ^ (isFlipped ? MADCTR_MIRROR_XY : 0)};
  DSI_IO_WriteCmd(0, madctl);

  LCD_UpdateRectangleButton(&flipButton);
}

static void SelectTheme(bool isDark)
{
  theme = &themes[isDark];
  darkModeButton.isActive = isDark;
}

void LCD_SetDarkMode(bool isDark)
{
  SelectTheme(isDark);
  Display_StartupScreen();
}

static void LCD_UpdateDarkModeButton(void)
{
  LCD_UpdateRectangleButton(&darkModeButton);

  // A crescent: the second disc is cut out in the button's own fill.
  uint16_t centerX = darkModeButton.x + darkModeButton.width / 2;
  uint16_t centerY = darkModeButton.y + darkModeButton.height / 2;
  BSP_LCD_SetTextColor(theme->foreground);
  BSP_LCD_FillCircle(centerX, centerY, MOON_RADIUS);
  BSP_LCD_SetTextColor(darkModeButton.isActive ? theme->buttonActive : theme->buttonIdle);
  BSP_LCD_FillCircle(centerX + MOON_BITE_OFFSET_X, centerY - MOON_BITE_OFFSET_Y, MOON_RADIUS);
}

static int32_t Clamp255(int32_t value)
{
  return value < 0 ? 0 : value > 255 ? 255 : value;
}

// utfprlogo blends paper white, ink and yellow. Recovering the three weights per pixel lets paper and ink follow the theme.
static void DrawLogo(uint16_t x, uint16_t y)
{
  for(uint16_t row = 0; row < UTFPR_LOGO_HEIGHT; row++)
  {
    for(uint16_t column = 0; column < UTFPR_LOGO_WIDTH; column++)
    {
      uint32_t i = row * UTFPR_LOGO_WIDTH + column;
      uint16_t pixel = utfprlogo[2 * i] | (utfprlogo[2 * i + 1] << 8);
      int32_t red = (((pixel >> 11) & 0x1F) * 255 + 15) / 31;
      int32_t blue = ((pixel & 0x1F) * 255 + 15) / 31;

      int32_t yellow = Clamp255(red - blue);
      int32_t paper = Clamp255((blue - LOGO_INK_BLUE + LOGO_INK_BLUE * yellow / 255) * 255 / (255 - LOGO_INK_BLUE));
      int32_t ink = Clamp255(255 - paper - yellow);

      uint32_t color = 0xFF000000;
      for(uint8_t shift = 0; shift <= 16; shift += 8)
      {
        uint32_t channel = (paper * ((theme->background >> shift) & 0xFF) + ink * ((theme->foreground >> shift) & 0xFF) + yellow * ((LOGO_YELLOW >> shift) & 0xFF)) / 255;
        color |= channel << shift;
      }
      BSP_LCD_DrawPixel(x + column, y + row, color);
    }
  }
}

void LCD_UpdateRectangleButton(RectangleButton* button)
{
  BSP_LCD_SetTextColor(theme->foreground);
  BSP_LCD_FillRect(button->x - BUTTON_BORDER_SIZE, button->y - BUTTON_BORDER_SIZE, button->width + (2*BUTTON_BORDER_SIZE), button->height + (2*BUTTON_BORDER_SIZE));
  BSP_LCD_SetTextColor(button->isActive ? theme->buttonActive : theme->buttonIdle);
  BSP_LCD_FillRect(button->x, button->y, button->width, button->height);

  BSP_LCD_SetBackColor(button->isActive ? theme->buttonActive : theme->buttonIdle);
  BSP_LCD_SetTextColor(theme->foreground);
  BSP_LCD_DisplayStringAt(button->x + 5, button->y + button->height / 2 - 6, (uint8_t *)button->text, LEFT_MODE);
}

int8_t LCD_ColumnToBand(uint8_t column)
{
  uint32_t band = eqPage * NUMBER_OF_SLIDER_BUTTONS + column;
  return band < EQ_BAND_COUNT ? band : -1;
}

static uint16_t StepButtonY(SliderKnob* knob, int8_t step)
{
  // The 2 px are the frame drawn around the sliders.
  if(step > 0)
    return knob->sliderY - 2 - STEP_BUTTON_GAP - STEP_BUTTON_HEIGHT;
  return knob->sliderY + knob->sliderHeight + 2 + STEP_BUTTON_GAP;
}

static uint16_t GainTextY(SliderKnob* knob)
{
  return StepButtonY(knob, -1) + STEP_BUTTON_HEIGHT + BUTTON_BORDER_SIZE + GAIN_TEXT_GAP;
}

static void LCD_DisplayStepButton(SliderKnob* knob, int8_t step)
{
  uint16_t x = knob->sliderX + STEP_BUTTON_MARGIN;
  uint16_t y = StepButtonY(knob, step);
  uint16_t width = knob->sliderWidth - 2 * STEP_BUTTON_MARGIN;
  sFONT* font = BSP_LCD_GetFont();

  BSP_LCD_SetTextColor(theme->foreground);
  BSP_LCD_FillRect(x - BUTTON_BORDER_SIZE, y - BUTTON_BORDER_SIZE, width + (2*BUTTON_BORDER_SIZE), STEP_BUTTON_HEIGHT + (2*BUTTON_BORDER_SIZE));
  BSP_LCD_SetTextColor(theme->buttonIdle);
  BSP_LCD_FillRect(x, y, width, STEP_BUTTON_HEIGHT);

  BSP_LCD_SetBackColor(theme->buttonIdle);
  BSP_LCD_SetTextColor(theme->foreground);
  BSP_LCD_DisplayStringAt(x + (width - font->Width) / 2, y + (STEP_BUTTON_HEIGHT - font->Height) / 2, (uint8_t *)(step > 0 ? "+" : "-"), LEFT_MODE);
}

int8_t LCD_StepButtonAt(uint8_t knobIndex, uint16_t x, uint16_t y)
{
  SliderKnob* knob = &sliderKnobs[knobIndex];
  if(x <= knob->sliderX || x >= knob->sliderX + knob->sliderWidth)
    return 0;

  for(int8_t step = -1; step <= 1; step += 2)
  {
    uint16_t top = StepButtonY(knob, step);
    if(y > top && y < top + STEP_BUTTON_HEIGHT)
      return step;
  }
  return 0;
}

void LCD_DisplayEqPage(void)
{
  SliderKnob* first = &sliderKnobs[0];
  uint16_t frameX = first->sliderX - 2;
  uint16_t frameWidth = NUMBER_OF_SLIDER_BUTTONS * first->sliderWidth + 4;
  uint16_t frameY = first->sliderY - 2;
  uint16_t frameHeight = first->sliderHeight + 4;
  uint16_t belowFrameY = frameY + frameHeight;

  BSP_LCD_SetTextColor(theme->background);
  BSP_LCD_FillRect(frameX, 0, frameWidth, frameY);
  BSP_LCD_FillRect(frameX, belowFrameY, frameWidth, GainTextY(first) + Font16.Height - belowFrameY);
  BSP_LCD_SetTextColor(theme->foreground);
  BSP_LCD_FillRect(frameX, frameY, frameWidth, frameHeight);

  for(uint8_t column = 0; column < NUMBER_OF_SLIDER_BUTTONS; column++)
  {
    SliderKnob* knob = &sliderKnobs[column];
    int8_t band = LCD_ColumnToBand(column);

    if(band < 0)
    {
      BSP_LCD_SetTextColor(theme->background);
      BSP_LCD_FillRect(knob->sliderX, knob->sliderY, knob->sliderWidth, knob->sliderHeight);
      continue;
    }

    const char* label = eqBands[band].label;
    uint16_t labelWidth = strlen(label) * BSP_LCD_GetFont()->Width;
    BSP_LCD_SetBackColor(theme->background);
    BSP_LCD_SetTextColor(theme->foreground);
    BSP_LCD_DisplayStringAt(knob->sliderX + (knob->sliderWidth - labelWidth) / 2, 0, (uint8_t *)label, LEFT_MODE);

    LCD_DisplayStepButton(knob, 1);
    LCD_DisplayStepButton(knob, -1);
    LCD_InitSlider(column);
    LCD_DisplayKnob(column, eqGains[band]);
  }
}

void LCD_ChangeEqPage(int8_t step)
{
  eqPage = (eqPage + EQ_PAGE_COUNT + step) % EQ_PAGE_COUNT;
  LCD_DisplayEqPage();
}

void LCD_InitSlider(uint8_t knobIndex)
{
  SliderKnob* knob = &sliderKnobs[knobIndex];

  BSP_LCD_SetTextColor(theme->background);
  BSP_LCD_FillRect(knob->sliderX, knob->sliderY, knob->sliderWidth, knob->sliderHeight);

  BSP_LCD_SetTextColor(theme->foreground);
  BSP_LCD_FillRect((knob->sliderX + (knob->sliderWidth / 2)-5), knob->sliderY + 10, 10, knob->sliderHeight - 20);
}

void LCD_DisplayKnob(uint8_t knobIndex, int16_t gain)
{
  SliderKnob* knob = &sliderKnobs[knobIndex];
  uint16_t newKnobY = LCD_TranslateGainToKnobPosition(knobIndex, gain);

  // clears the previous spot if it moved
  if(newKnobY != knob->knobY)
  {
    BSP_LCD_SetTextColor(theme->background);
    BSP_LCD_FillRect(knob->sliderX + 3, knob->knobY - 5, knob->sliderWidth - 3, 12);

    BSP_LCD_SetTextColor(theme->foreground);
    BSP_LCD_FillRect((knob->sliderX + (knob->sliderWidth / 2)-5), knob->knobY - 5, 10, 12);
  }

  BSP_LCD_SetTextColor(theme->foreground);
  BSP_LCD_FillRect(knob->sliderX + 3, newKnobY - 5, knob->sliderWidth - 3, 12);

  BSP_LCD_SetTextColor(theme->background);
  BSP_LCD_FillRect(knob->sliderX + 5, newKnobY - 3, knob->sliderWidth - 7, 8);

  knob->knobY = newKnobY;

  char text[12];
  int16_t magnitude = abs(gain);
  sprintf(text, "%s%d.%d", gain < 0 ? "-" : gain > 0 ? "+" : "", magnitude / EQ_GAIN_STEPS_PER_DB, magnitude % EQ_GAIN_STEPS_PER_DB);

  // Font24 fits only 3 characters in a column, and "-12.5" needs 5.
  sFONT* font = BSP_LCD_GetFont();
  BSP_LCD_SetFont(&Font16);
  uint16_t textY = GainTextY(knob);
  uint16_t textWidth = strlen(text) * Font16.Width;
  BSP_LCD_SetTextColor(theme->background);
  BSP_LCD_FillRect(knob->sliderX, textY, knob->sliderWidth, Font16.Height);
  BSP_LCD_SetBackColor(theme->background);
  BSP_LCD_SetTextColor(theme->foreground);
  BSP_LCD_DisplayStringAt(knob->sliderX + (knob->sliderWidth - textWidth) / 2, textY, (uint8_t *)text, LEFT_MODE);
  BSP_LCD_SetFont(font);
}

uint16_t LCD_TranslateGainToKnobPosition(uint8_t knobIndex, int16_t gain)
{
  SliderKnob* knob = &sliderKnobs[knobIndex];
  int32_t bottom = knob->sliderY + knob->sliderHeight;
  return bottom - (gain - EQ_GAIN_MIN) * knob->sliderHeight / (EQ_GAIN_MAX - EQ_GAIN_MIN);
}

// Dragging snaps to whole dB; the step buttons reach the tenths in between.
int16_t LCD_TranslateKnobPositionToGain(uint8_t knobIndex, uint16_t knobY)
{
  SliderKnob* knob = &sliderKnobs[knobIndex];
  int32_t bottom = knob->sliderY + knob->sliderHeight;
  int32_t gain = EQ_GAIN_MIN_DB + ((bottom - knobY) * (EQ_GAIN_MAX_DB - EQ_GAIN_MIN_DB) + knob->sliderHeight / 2) / knob->sliderHeight;
  if(gain < EQ_GAIN_MIN_DB)
    gain = EQ_GAIN_MIN_DB;
  if(gain > EQ_GAIN_MAX_DB)
    gain = EQ_GAIN_MAX_DB;
  return gain * EQ_GAIN_STEPS_PER_DB;
}

void LCD_UpdateButton(uint8_t buttonIndex, bool isPressed, bool shouldToggleOtherButtons)
{
  CircleButtonTypeDef* button = &circleButtons[buttonIndex];

  button->isPressed = isPressed;
  button->isActive = isPressed;
  LCD_UpdateState();
}

void LCD_UpdateWatchdog (uint32_t* watchdogCounter)
{
  char text[5];
  sprintf(text, "%04u", ((unsigned int)*watchdogCounter));
  BSP_LCD_SetBackColor(theme->background);
  BSP_LCD_SetTextColor(theme->foreground);
  BSP_LCD_DisplayStringAt(0, 0, (uint8_t *)text, RIGHT_MODE);
  *watchdogCounter = *watchdogCounter + 1;
  if(*watchdogCounter > 9999)
    *watchdogCounter = 0;
}
