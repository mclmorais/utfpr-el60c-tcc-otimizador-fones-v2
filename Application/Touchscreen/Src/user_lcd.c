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

// private function declarations --------------------------------------
static void     LCD_LayertInit(uint16_t LayerIndex, uint32_t Address);
static void     Display_StartupScreen(void);
static uint8_t  CopyImageToLcdFrameBuffer(void *pSrc, void *pDst, uint32_t xSize, uint32_t ySize, uint16_t x, uint16_t y);

// variables ----------------------------------------------------------
// !! THE NUMBER_OF_CIRCLE_BUTTONS DEFINE HAS TO BE UP TO DATE WITH THE NUMBER OF BUTTONS DEFINED HERE
CircleButtonTypeDef circleButtons[] = {
  //  x,   y,  r,               color,       text color,        on,     off,  pressed, independent, active
  { 100, 440, 30, LCD_COLOR_BURGUNDY,   LCD_COLOR_BLACK, "EQ",     "",       false,     false,      false, 0},
  { 150, 440, 30, LCD_COLOR_BURGUNDY,   LCD_COLOR_BLACK, "Vol",     "",       false,     false,      false, 0},
  // { 250, 420, 50, LCD_COLOR_MINT_GREEN, LCD_COLOR_BLACK, "Volume",  "",       false,    false,      false},
  // { 400, 420, 50, LCD_COLOR_MINT_GREEN, LCD_COLOR_BLACK, "Low Pass",     "",       false,    false,      false},
  // { 550, 420, 50, LCD_COLOR_MINT_GREEN, LCD_COLOR_BLACK, "Biquad",     "",       false,    false,      false},
  // { 700, 420, 50, LCD_COLOR_MINT_GREEN, LCD_COLOR_BLACK, "NYI",     "",       false,    false,      false},
  // { 650, 120, 50, LCD_COLOR_LIGHTBLUE,  LCD_COLOR_BLACK, "Sending", "Debug",  false,    true,       false}
};

RectangleButton saveButton = {
  .x = 20,
  .y = 200,
  .width = 180,
  .height = 50,
  .inactiveColor = LCD_COLOR_LIGHTGRAY,
  .activeColor = LCD_COLOR_LIGHTGREEN,
  .text = "Salvar",
  .isPressed = false,
  .isActive = false
};

RectangleButton undoButton = {
  .x = 20,
  .y = 200 + 60,
  .width = 180,
  .height = 50,
  .inactiveColor = LCD_COLOR_LIGHTGRAY,
  .activeColor = LCD_COLOR_LIGHTGREEN,
  .text = "Desfazer",
  .isPressed = false,
  .isActive = false
};


RectangleButton resetButton = {
  .x = 20,
  .y = 200 + 60 + 60,
  .width = 180,
  .height = 50,
  .inactiveColor = LCD_COLOR_LIGHTGRAY,
  .activeColor = LCD_COLOR_LIGHTGREEN,
  .text = "Redefinir",
  .isPressed = false,
  .isActive = false
};


RectangleButton previousPageButton = {
  .x = 145,
  .y = 415,
  .width = 45,
  .height = 50,
  .inactiveColor = LCD_COLOR_LIGHTGRAY,
  .activeColor = LCD_COLOR_LIGHTGREEN,
  .text = "<",
  .isPressed = false,
  .isActive = false
};

RectangleButton nextPageButton = {
  .x = 198,
  .y = 415,
  .width = 45,
  .height = 50,
  .inactiveColor = LCD_COLOR_LIGHTGRAY,
  .activeColor = LCD_COLOR_LIGHTGREEN,
  .text = ">",
  .isPressed = false,
  .isActive = false
};

RectangleButton flipButton = {
  .x = 738,
  .y = 415,
  .width = 56,
  .height = 50,
  .inactiveColor = LCD_COLOR_LIGHTGRAY,
  .activeColor = LCD_COLOR_LIGHTGREEN,
  .text = "180",
  .isPressed = false,
  .isActive = false
};

static uint8_t eqPage = 0;

SliderKnob sliderKnobs[] = {
  { 200 + 50, 70, 60, 320, LCD_COLOR_BLACK, 230, 20, 5, 7},
  { 200 + 110, 70, 60, 320, LCD_COLOR_BLACK, 230, 20, 5, 7},
  { 200 + 170, 70, 60, 320, LCD_COLOR_BLACK, 230, 20, 5, 7},
  { 200 + 230, 70, 60, 320, LCD_COLOR_BLACK, 230, 20, 5, 7},
  { 200 + 290, 70, 60, 320, LCD_COLOR_BLACK, 230, 20, 5, 7},
  { 200 + 350, 70, 60, 320, LCD_COLOR_BLACK, 230, 20, 5, 7},
  { 200 + 410, 70, 60, 320, LCD_COLOR_BLACK, 230, 20, 5, 7},
  { 200 + 470, 70, 60, 320, LCD_COLOR_BLACK, 230, 20, 5, 7}
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
 * @param  None
 * @retval LCD state
 */
void LCD_Init(void)
{
  uint8_t lcd_status = LCD_OK;

  lcd_status = BSP_LCD_Init();
  while(lcd_status != LCD_OK);

  // Initialize LTDC layer 0 iused for Hint
  LCD_LayertInit(0, LAYER0_ADDRESS);
  BSP_LCD_SelectLayer(0);

  BSP_LCD_LayerDefaultInit(0, LCD_FB_START_ADDRESS);

  BSP_LCD_Clear(LCD_COLOR_WHITE);

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
  BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
  BSP_LCD_Clear(LCD_COLOR_WHITE);

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
    BSP_LCD_Clear(LCD_COLOR_WHITE);

    // displays logo
    BSP_LCD_DrawPicture(utfprlogo, UTFPR_LOGO_WIDTH, UTFPR_LOGO_HEIGHT, 20, 20);

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
    BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
    BSP_LCD_FillCircle(circleButtons[i].x, circleButtons[i].y, circleButtons[i].radius);

    // draws button inner circle
    BSP_LCD_SetTextColor(circleButtons[i].isPressed ? LCD_COLOR_LIGHTGREEN : LCD_COLOR_LIGHTGRAY);
    BSP_LCD_FillCircle(circleButtons[i].x, circleButtons[i].y, circleButtons[i].radius - 3);

    BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
    BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
    BSP_LCD_DisplayStringAt(circleButtons[i].x - circleButtons[i].radius - 12, circleButtons[i].y - circleButtons[i].radius - 24, (uint8_t *)circleButtons[i].onText, LEFT_MODE);


  }

  LCD_UpdateRectangleButton(&flipButton);
}

void LCD_SetFlip(bool isFlipped)
{
  flipButton.isActive = isFlipped;

  // Mirroring both axes of the panel's scan turns the image 180 degrees without touching the framebuffer.
  uint8_t madctl[] = {OTM8009A_CMD_MADCTR, OTM8009A_MADCTR_MODE_LANDSCAPE ^ (isFlipped ? MADCTR_MIRROR_XY : 0)};
  DSI_IO_WriteCmd(0, madctl);

  LCD_UpdateRectangleButton(&flipButton);
}

/**
 * @brief  Copy and convert image (LAYER_SIZE_X, LAYER_SIZE_Y) of format RGB565
 * to LCD frame buffer area centered in WVGA resolution.
 * The area of copy is of size (LAYER_SIZE_X, LAYER_SIZE_Y) in ARGB8888.
 * @param  pSrc: Pointer to source buffer : source image buffer start here
 * @param  pDst: Pointer to destination buffer LCD frame buffer center area start here
 * @param  xSize: Buffer width (LAYER_SIZE_X here)
 * @param  ySize: Buffer height (LAYER_SIZE_Y here)
 * @retval LCD Status : LCD_OK or LCD_ERROR
 */
static uint8_t CopyImageToLcdFrameBuffer(void *pSrc, void *pDst, uint32_t xSize, uint32_t ySize, uint16_t x, uint16_t y)
{

  uint32_t destination = (uint32_t)pDst + (y * 800 + x) * 4;
  DMA2D_HandleTypeDef hdma2d_discovery;
  HAL_StatusTypeDef hal_status = HAL_OK;
  uint8_t lcd_status = LCD_ERROR;

  // Configure the DMA2D Mode, Color Mode and output offset
  hdma2d_discovery.Init.Mode         = DMA2D_M2M_PFC;
  hdma2d_discovery.Init.ColorMode    = DMA2D_OUTPUT_ARGB8888;// Output color out of PFC
  hdma2d_discovery.Init.AlphaInverted = DMA2D_REGULAR_ALPHA; // No Output Alpha Inversion
  hdma2d_discovery.Init.RedBlueSwap   = DMA2D_RB_REGULAR;// No Output Red & Blue swap

  // Output offset in pixels == nb of pixels to be added at end of line to come to the
  // first pixel of the next line : on the output side of the DMA2D computation
  // TODO: GENERALIZE
  hdma2d_discovery.Init.OutputOffset = (WVGA_RES_X - UTFPR_LOGO_WIDTH);

  // Foreground Configuration
  hdma2d_discovery.LayerCfg[1].AlphaMode = DMA2D_NO_MODIF_ALPHA;
  hdma2d_discovery.LayerCfg[1].InputAlpha = 0xFF; // fully opaque
  hdma2d_discovery.LayerCfg[1].InputColorMode = DMA2D_INPUT_RGB565;
  hdma2d_discovery.LayerCfg[1].InputOffset = 0;
  hdma2d_discovery.LayerCfg[1].RedBlueSwap = DMA2D_RB_REGULAR; // No ForeGround Red/Blue swap
  hdma2d_discovery.LayerCfg[1].AlphaInverted = DMA2D_REGULAR_ALPHA; // No ForeGround Alpha inversion

  hdma2d_discovery.Instance = DMA2D;

  // DMA2D Initialization
  if(HAL_DMA2D_Init(&hdma2d_discovery) == HAL_OK)
  {
    if(HAL_DMA2D_ConfigLayer(&hdma2d_discovery, 1) == HAL_OK)
    {
      if (HAL_DMA2D_Start(&hdma2d_discovery, (uint32_t)pSrc, destination, xSize, ySize) == HAL_OK)
      {
        // Polling For DMA transfer
        hal_status = HAL_DMA2D_PollForTransfer(&hdma2d_discovery, 10);
        if(hal_status == HAL_OK)
        {
          // return good status on exit
          lcd_status = LCD_OK;
        }
      }
    }
  }

  return(lcd_status);
}

void LCD_UpdateRectangleButton(RectangleButton* button)
{
  BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
  BSP_LCD_FillRect(button->x - BUTTON_BORDER_SIZE, button->y - BUTTON_BORDER_SIZE, button->width + (2*BUTTON_BORDER_SIZE), button->height + (2*BUTTON_BORDER_SIZE));
  BSP_LCD_SetTextColor(button->isActive ? button->activeColor : button->inactiveColor);
  BSP_LCD_FillRect(button->x, button->y, button->width, button->height);

  BSP_LCD_SetBackColor(button->isActive ? button->activeColor : button->inactiveColor);
  BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
  BSP_LCD_DisplayStringAt(button->x + 5, button->y + button->height / 2 - 6, (uint8_t *)button->text, LEFT_MODE);
}

int8_t LCD_ColumnToBand(uint8_t column)
{
  uint32_t band = eqPage * NUMBER_OF_SLIDER_BUTTONS + column;
  return band < EQ_BAND_COUNT ? band : -1;
}

static uint16_t StepButtonY(SliderKnob* knob, int8_t step)
{
  // The 2 px are the black frame drawn around the sliders.
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

  BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
  BSP_LCD_FillRect(x - BUTTON_BORDER_SIZE, y - BUTTON_BORDER_SIZE, width + (2*BUTTON_BORDER_SIZE), STEP_BUTTON_HEIGHT + (2*BUTTON_BORDER_SIZE));
  BSP_LCD_SetTextColor(LCD_COLOR_LIGHTGRAY);
  BSP_LCD_FillRect(x, y, width, STEP_BUTTON_HEIGHT);

  BSP_LCD_SetBackColor(LCD_COLOR_LIGHTGRAY);
  BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
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

  BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
  BSP_LCD_FillRect(frameX, 0, frameWidth, frameY);
  BSP_LCD_FillRect(frameX, belowFrameY, frameWidth, GainTextY(first) + Font16.Height - belowFrameY);
  BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
  BSP_LCD_FillRect(frameX, frameY, frameWidth, frameHeight);

  for(uint8_t column = 0; column < NUMBER_OF_SLIDER_BUTTONS; column++)
  {
    SliderKnob* knob = &sliderKnobs[column];
    int8_t band = LCD_ColumnToBand(column);

    if(band < 0)
    {
      BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
      BSP_LCD_FillRect(knob->sliderX, knob->sliderY, knob->sliderWidth, knob->sliderHeight);
      continue;
    }

    const char* label = eqBands[band].label;
    uint16_t labelWidth = strlen(label) * BSP_LCD_GetFont()->Width;
    BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
    BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
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

  BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
  BSP_LCD_FillRect(knob->sliderX, knob->sliderY, knob->sliderWidth, knob->sliderHeight);

  BSP_LCD_SetTextColor(knob->sliderColor);
  BSP_LCD_FillRect((knob->sliderX + (knob->sliderWidth / 2)-5), knob->sliderY + 10, 10, knob->sliderHeight - 20);
}

void LCD_DisplayKnob(uint8_t knobIndex, int16_t gain)
{
  SliderKnob* knob = &sliderKnobs[knobIndex];
  uint16_t newKnobY = LCD_TranslateGainToKnobPosition(knobIndex, gain);

  // clears the previous spot if it moved
  if(newKnobY != knob->knobY)
  {
    BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
    BSP_LCD_FillRect(knob->sliderX + 3, knob->knobY - 5, knob->sliderWidth - 3, 12);

    BSP_LCD_SetTextColor(knob->sliderColor);
    BSP_LCD_FillRect((knob->sliderX + (knob->sliderWidth / 2)-5), knob->knobY - 5, 10, 12);
  }

  BSP_LCD_SetTextColor(knob->sliderColor);
  BSP_LCD_FillRect(knob->sliderX + 3, newKnobY - 5, knob->sliderWidth - 3, 12);

  BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
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
  BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
  BSP_LCD_FillRect(knob->sliderX, textY, knob->sliderWidth, Font16.Height);
  BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
  BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
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
  BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
  BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
  BSP_LCD_DisplayStringAt(0, 0, (uint8_t *)text, RIGHT_MODE);
  *watchdogCounter = *watchdogCounter + 1;
  if(*watchdogCounter > 9999)
    *watchdogCounter = 0;
}


void BSP_LCD_DrawPicture(const uint8_t* image, uint32_t width, uint32_t height, uint32_t xPosition, uint32_t yPosition )
{
  CopyImageToLcdFrameBuffer((void*)image, (void*)(LCD_FRAME_BUFFER), width, height, xPosition, yPosition);
}
