/**
  ******************************************************************************
  * @file    audio_speaker_node.c
  * @author  MCD Application Team 
  * @brief   speaker node implementation.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2019  STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "usbd_audio.h"
#include "audio_speaker_node.h"
#include "usb_audio.h"

/* Private defines -----------------------------------------------------------*/
#define SPEAKER_CMD_STOP             1
#define SPEAKER_CMD_CHANGE_FREQUENCE 4

/* codec requests posted by interrupts and applied by AUDIO_SpeakerApplyCodecRequests */
#define SPEAKER_CODEC_INIT      0x01
#define SPEAKER_CODEC_DEINIT    0x02
#define SPEAKER_CODEC_FREQUENCY 0x04
#define SPEAKER_CODEC_MUTE      0x08
#define SPEAKER_CODEC_VOLUME    0x10
#define VOLUME_DB_256_TO_PERCENT(volume_db_256)                           \
  ((uint8_t)((((int)(volume_db_256) - VOLUME_SPEAKER_MIN_DB_256) * 100) / \
             (VOLUME_SPEAKER_MAX_DB_256 - VOLUME_SPEAKER_MIN_DB_256)))

#if USB_AUDIO_CONFIG_PLAY_RES_BIT == 24
#if USB_AUDIO_CONFIG_PLAY_USE_FREQ_44_1_K
#define AUDIO_SPEAKER_MAX_INJECTION_LENGTH(audio_desc)                                   \
  (((audio_desc)->frequency == USB_AUDIO_CONFIG_FREQ_44_1_K)                             \
       ? AUDIO_SPEAKER_INJECTION_LENGTH(audio_desc) + (4 * (audio_desc)->channels_count) \
       : AUDIO_SPEAKER_INJECTION_LENGTH(audio_desc))
#endif /* USB_AUDIO_CONFIG_PLAY_USE_FREQ_44_1_K */
#define AUDIO_SPEAKER_INJECTION_LENGTH(audio_desc) \
  AUDIO_MS_PACKET_SIZE((audio_desc)->frequency, (audio_desc)->channels_count, 4)
#else /* USB_AUDIO_CONFIG_PLAY_RES_BIT == 24  */
#define AUDIO_SPEAKER_MAX_INJECTION_LENGTH(audio_desc)                                                          \
  (((audio_desc)->frequency == USB_AUDIO_CONFIG_FREQ_44_1_K)                                                    \
       ? AUDIO_SPEAKER_INJECTION_LENGTH(audio_desc) + ((audio_desc)->resolution * (audio_desc)->channels_count) \
       : AUDIO_SPEAKER_INJECTION_LENGTH(audio_desc))
#define AUDIO_SPEAKER_INJECTION_LENGTH(audio_desc) \
  AUDIO_MS_PACKET_SIZE((audio_desc)->frequency, (audio_desc)->channels_count, (audio_desc)->resolution)
#endif /* USB_AUDIO_CONFIG_PLAY_RES_BIT == 24  */

/* alt buffer max size */
#if USB_AUDIO_CONFIG_PLAY_RES_BIT == 24
#define SPEAKER_ALT_BUFFER_SIZE ((USB_AUDIO_CONFIG_PLAY_FREQ_MAX + 999) / 1000) * 2 * 4 * 2
#else /* USB_AUDIO_CONFIG_PLAY_RES_BIT == 24 */
#define SPEAKER_ALT_BUFFER_SIZE ((USB_AUDIO_CONFIG_PLAY_FREQ_MAX + 999) / 1000) * 2 * 3 * 2
#endif /*  USB_AUDIO_CONFIG_PLAY_RES_BIT == 24*/

//#define DEBUG_SPEAKER_NODE 1 /* define when debug is required*/
#ifdef DEBUG_SPEAKER_NODE
#define SPEAKER_DEBUG_BUFFER_SIZE 1000
#endif /*DEBUG_SPEAKER_NODE*/

/* Private function prototypes -----------------------------------------------*/
static int8_t AUDIO_SpeakerDeInit(uint32_t node_handle);
static int8_t AUDIO_SpeakerStart(AUDIO_CircularBuffer_t* buffer, uint32_t node_handle);
static int8_t AUDIO_SpeakerStop(uint32_t node_handle);
static int8_t AUDIO_SpeakerChangeFrequency(uint32_t node_handle);
static int8_t AUDIO_SpeakerMute(uint16_t channel_number, uint8_t mute, uint32_t node_handle);
static int8_t AUDIO_SpeakerSetVolume(uint16_t channel_number, int volume, uint32_t node_handle);
static void AUDIO_SpeakerInitInjectionsParams(AUDIO_SpeakerNode_t* speaker);
static void AUDIO_SpeakerPostCodecRequests(uint8_t set, uint8_t clear);
#if USB_AUDIO_CONFIG_PLAY_RES_BIT == 24
static void AUDIO_DoPadding_24_32(AUDIO_CircularBuffer_t* buff_src, uint8_t* data_dest, int size);
#endif /* USB_AUDIO_CONFIG_PLAY_RES_BIT == 24   */
static int8_t AUDIO_SpeakerStartReadCount(uint32_t node_handle);
static uint16_t AUDIO_SpeakerGetLastReadCount(uint32_t node_handle);

/* Private typedef -----------------------------------------------------------*/
#ifdef DEBUG_SPEAKER_NODE
typedef struct {
  uint32_t time;
  uint16_t injection_size;
  uint16_t read;
  uint16_t dump;
  uint8_t* data;
} AUDIO_SpeakerNodeBufferStats_t;
#endif /* DEBUG_SPEAKER_NODE*/

/* Private macros ------------------------------------------------------------*/
/* External variables --------------------------------------------------------*/
extern SAI_HandleTypeDef haudio_out_sai;
#ifdef DEBUG_SPEAKER_NODE
extern __IO uint32_t uwTick;
#endif /* DEBUG_SPEAKER_NODE*/

/* Private variables -----------------------------------------------------------*/
static AUDIO_SpeakerNode_t* AUDIO_SpeakerHandler = 0;
static uint8_t AUDIO_SpeakerAltBuffer[SPEAKER_ALT_BUFFER_SIZE];
/* Kept outside the node: AUDIO_SpeakerInit clears the node while a DEINIT may still be pending. */
static AUDIO_SpeakerNode_t* AUDIO_SpeakerCodecNode = 0;
static __IO uint8_t AUDIO_SpeakerCodecRequests = 0;
static __IO uint8_t AUDIO_SpeakerCodecMute = 0;
static __IO int AUDIO_SpeakerCodecVolume = VOLUME_SPEAKER_DEFAULT_DB_256;
static __IO uint8_t AUDIO_SpeakerCodecOn = 0;
#ifdef DEBUG_SPEAKER_NODE
static AUDIO_SpeakerNodeBufferStats_t AUDIO_SpeakerDebugStats[SPEAKER_DEBUG_BUFFER_SIZE];
static int AUDIO_SpeakerDebugStats_count = 0;
#endif /* DEBUG_SPEAKER_NODE*/

/* Exported functions ---------------------------------------------------------*/

/**
  * @brief  AUDIO_SpeakerInit
  *         Initializes the audio speaker node, set callbacks and start the codec. As no data are ready. The 
  *         SAI is feeded from the alternate buffer (filled by zeros)
  * @param  audio_description(IN): audio information
  * @param  session_handle(IN):   session handle
  * @param  node_handle(IN):      speaker node handle must be allocated
  * @retval 0 if no error
  */
int8_t AUDIO_SpeakerInit(AUDIO_Description_t* audio_description, AUDIO_Session_t* session_handle, uint32_t node_handle)
{
  AUDIO_SpeakerNode_t* speaker;

  speaker = (AUDIO_SpeakerNode_t*)node_handle;
  memset(speaker, 0, sizeof(AUDIO_SpeakerNode_t));
  speaker->node.type = AUDIO_OUTPUT;
  speaker->node.state = AUDIO_NODE_INITIALIZED;
  speaker->node.session_handle = session_handle;
  speaker->node.audio_description = audio_description;
  speaker->specific.alt_buffer = AUDIO_SpeakerAltBuffer;
  AUDIO_SpeakerInitInjectionsParams(speaker);

  /* set callbacks */
  speaker->SpeakerDeInit = AUDIO_SpeakerDeInit;
  speaker->SpeakerStart = AUDIO_SpeakerStart;
  speaker->SpeakerStop = AUDIO_SpeakerStop;
  speaker->SpeakerChangeFrequency = AUDIO_SpeakerChangeFrequency;
  speaker->SpeakerMute = AUDIO_SpeakerMute;
  speaker->SpeakerSetVolume = AUDIO_SpeakerSetVolume;
  speaker->SpeakerStartReadCount = AUDIO_SpeakerStartReadCount;
  speaker->SpeakerGetReadCount = AUDIO_SpeakerGetLastReadCount;

  AUDIO_SpeakerCodecNode = speaker;
  AUDIO_SpeakerHandler = speaker;
  AUDIO_SpeakerPostCodecRequests(SPEAKER_CODEC_INIT, 0);
  return 0;
}

/**
  * @brief  AUDIO_SpeakerApplyCodecRequests
  *         Applies the codec requests posted by the USB and DMA interrupts. Must be called from the main
  *         loop only: the codec shares I2C4 with the touch controller, which the main loop reads.
  * @param  None
  * @retval None
  */
void AUDIO_SpeakerApplyCodecRequests(void)
{
  AUDIO_SpeakerNode_t* speaker = AUDIO_SpeakerCodecNode;
  uint32_t primask = __get_PRIMASK();
  uint8_t requests;

  __disable_irq();
  requests = AUDIO_SpeakerCodecRequests;
  AUDIO_SpeakerCodecRequests = 0;
  __set_PRIMASK(primask);

  if((requests & SPEAKER_CODEC_DEINIT) && AUDIO_SpeakerCodecOn)
  {
    BSP_AUDIO_OUT_SetMute(AUDIO_MUTE_ON);
    BSP_AUDIO_OUT_Stop(CODEC_PDWN_SW);
    BSP_AUDIO_OUT_DeInit();
    AUDIO_SpeakerCodecOn = 0;
  }
  if(requests & SPEAKER_CODEC_INIT)
  {
    BSP_AUDIO_OUT_Init_Ext(OUTPUT_DEVICE_AUTO,
                           VOLUME_DB_256_TO_PERCENT(VOLUME_SPEAKER_DEFAULT_DB_256),
                           speaker->node.audio_description->frequency,
                           speaker->node.audio_description->resolution << 3);
    BSP_AUDIO_OUT_Play((uint16_t*)speaker->specific.data, speaker->specific.data_size);
    AUDIO_SpeakerCodecOn = 1;
  }
  if(!AUDIO_SpeakerCodecOn)
  {
    return;
  }
  if(requests & SPEAKER_CODEC_FREQUENCY)
  {
    BSP_AUDIO_OUT_SetMute(AUDIO_MUTE_ON);
    BSP_AUDIO_OUT_SetFrequency(speaker->node.audio_description->frequency);
    BSP_AUDIO_OUT_ChangeBuffer((uint16_t*)speaker->specific.data, (uint16_t)speaker->specific.data_size);
    requests |= SPEAKER_CODEC_MUTE;
  }
  if(requests & SPEAKER_CODEC_MUTE)
  {
    BSP_AUDIO_OUT_SetMute(AUDIO_SpeakerCodecMute);
  }
  if(requests & SPEAKER_CODEC_VOLUME)
  {
    BSP_AUDIO_OUT_SetVolume(VOLUME_DB_256_TO_PERCENT(AUDIO_SpeakerCodecVolume));
  }
}

/**
  * @brief  AUDIO_SpeakerPostCodecRequests
  *         Posts codec requests for the main loop. Safe to call from any interrupt priority.
  * @param  set(IN):   requests to add
  * @param  clear(IN): pending requests to drop
  * @retval None
  */
static void AUDIO_SpeakerPostCodecRequests(uint8_t set, uint8_t clear)
{
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  AUDIO_SpeakerCodecRequests = (AUDIO_SpeakerCodecRequests & ~clear) | set;
  __set_PRIMASK(primask);
}

/**
  * @brief  BSP_AUDIO_OUT_Error_CallBack
  *         Manages the DMA error event.
  * @param  None
  * @retval None
  */
void BSP_AUDIO_OUT_Error_CallBack(void)
{
  Error_Handler();
}

/**
  * @brief  BSP_AUDIO_OUT_TransferComplete_CallBack
  *         Manages the DMA full Transfer complete event.
  * @param  None
  * @retval None
  */
void BSP_AUDIO_OUT_TransferComplete_CallBack(void)
{
  uint16_t wr_distance, read_length;

  if((AUDIO_SpeakerHandler) && (AUDIO_SpeakerHandler->node.state != AUDIO_NODE_OFF))
  {
    if(AUDIO_SpeakerHandler->specific.cmd & SPEAKER_CMD_CHANGE_FREQUENCE)
    {
      AUDIO_SpeakerHandler->node.state = AUDIO_NODE_STOPPED;
      AUDIO_SpeakerInitInjectionsParams(AUDIO_SpeakerHandler);
      AUDIO_SpeakerHandler->specific.cmd = 0;
      /* the DMA stays stopped until the main loop changes the codec frequency and re-arms it */
      AUDIO_SpeakerPostCodecRequests(SPEAKER_CODEC_FREQUENCY, 0);
      return;
    }

    if(AUDIO_SpeakerHandler->specific.cmd & SPEAKER_CMD_STOP)
    {
      AUDIO_SpeakerHandler->specific.data = AUDIO_SpeakerHandler->specific.alt_buffer;
      AUDIO_SpeakerHandler->specific.data_size = AUDIO_SpeakerHandler->specific.injection_size;
      AUDIO_SpeakerHandler->specific.offset = 0;
      memset(AUDIO_SpeakerHandler->specific.data, 0, AUDIO_SpeakerHandler->specific.data_size);
      AUDIO_SpeakerHandler->node.state = AUDIO_NODE_STOPPED;
      AUDIO_SpeakerHandler->specific.cmd ^= SPEAKER_CMD_STOP;
    }
    /* inject current data */
    BSP_AUDIO_OUT_ChangeBuffer((uint16_t*)AUDIO_SpeakerHandler->specific.data,
                               (uint16_t)AUDIO_SpeakerHandler->specific.data_size);
    /* if speaker was started prepare next data */
    if(AUDIO_SpeakerHandler->node.state == AUDIO_NODE_STARTED)
    {
#ifdef DEBUG_SPEAKER_NODE
      AUDIO_SpeakerDebugStats[AUDIO_SpeakerDebugStats_count].time = uwTick;
#endif /* DEBUG_SPEAKER_NODE */

      /* inform session that a packet is played */
      AUDIO_SpeakerHandler->node.session_handle->SessionCallback(
          AUDIO_PACKET_PLAYED, (AUDIO_Node_t*)AUDIO_SpeakerHandler, AUDIO_SpeakerHandler->node.session_handle);
      /* prepare next size to inject */
#if (USB_AUDIO_CONFIG_PLAY_RES_BIT == 24)
      AUDIO_SpeakerHandler->specific.data =
          (AUDIO_SpeakerHandler->specific.offset)
              ? AUDIO_SpeakerHandler->specific.alt_buffer
              : AUDIO_SpeakerHandler->specific.alt_buffer + AUDIO_SpeakerHandler->specific.data_size;
      AUDIO_SpeakerHandler->specific.offset ^= 1;
#endif /* (USB_AUDIO_CONFIG_PLAY_RES_BIT == 24) */
      AUDIO_SpeakerHandler->specific.data_size = AUDIO_SpeakerHandler->specific.injection_size;
      read_length = AUDIO_SpeakerHandler->packet_length;
#if USB_AUDIO_CONFIG_PLAY_USE_FREQ_44_1_K
      if(AUDIO_SpeakerHandler->node.audio_description->frequency == USB_AUDIO_CONFIG_FREQ_44_1_K)
      {
        if(AUDIO_SpeakerHandler->injection_44_count < 9)
        {
          AUDIO_SpeakerHandler->injection_44_count++;
        }
        else
        {
          AUDIO_SpeakerHandler->injection_44_count = 0;
          AUDIO_SpeakerHandler->specific.data_size = AUDIO_SpeakerHandler->specific.alt_buf_half_size;
          read_length = AUDIO_SpeakerHandler->packet_length_max_44_1;
        }
      }
#endif /* USB_AUDIO_CONFIG_PLAY_USE_FREQ_44_1_K*/
      wr_distance = AUDIO_BUFFER_FILLED_SIZE(AUDIO_SpeakerHandler->buf);
      if(wr_distance < AUDIO_SpeakerHandler->specific.injection_size)
      {
        /** inform session that an underrun is happened */
        AUDIO_SpeakerHandler->node.session_handle->SessionCallback(
            AUDIO_UNDERRUN, (AUDIO_Node_t*)AUDIO_SpeakerHandler, AUDIO_SpeakerHandler->node.session_handle);
      }
      else
      {

#if (USB_AUDIO_CONFIG_PLAY_RES_BIT == 24)
        /* buffer already prepared in half transfer */
        AUDIO_DoPadding_24_32(AUDIO_SpeakerHandler->buf, AUDIO_SpeakerHandler->specific.data, read_length);
#else /*  (USB_AUDIO_CONFIG_PLAY_RES_BIT == 24)  */
        AUDIO_SpeakerHandler->specific.data = AUDIO_SpeakerHandler->buf->data + AUDIO_SpeakerHandler->buf->rd_ptr;
#if USB_AUDIO_CONFIG_PLAY_USE_FREQ_44_1_K
        if(AUDIO_SpeakerHandler->node.audio_description->frequency == USB_AUDIO_CONFIG_FREQ_44_1_K)
        {
          uint16_t d = AUDIO_SpeakerHandler->buf->size - AUDIO_SpeakerHandler->buf->rd_ptr;
          if(d < AUDIO_SpeakerHandler->specific.data_size)
          {
            memcpy(AUDIO_SpeakerHandler->specific.alt_buffer,
                   AUDIO_SpeakerHandler->buf->data + AUDIO_SpeakerHandler->buf->rd_ptr,
                   d);
            memcpy(AUDIO_SpeakerHandler->specific.alt_buffer + d,
                   AUDIO_SpeakerHandler->buf->data,
                   AUDIO_SpeakerHandler->specific.data_size - d);
            AUDIO_SpeakerHandler->specific.data = AUDIO_SpeakerHandler->specific.alt_buffer;
          }
        }
#endif /* USB_AUDIO_CONFIG_PLAY_USE_FREQ_44_1_K*/
#endif /*  USB_AUDIO_CONFIG_PLAY_RES_BIT */
#ifdef DEBUG_SPEAKER_NODE
        AUDIO_SpeakerDebugStats[AUDIO_SpeakerDebugStats_count].data = AUDIO_SpeakerHandler->specific.data;
        AUDIO_SpeakerDebugStats[AUDIO_SpeakerDebugStats_count].injection_size =
            AUDIO_SpeakerHandler->specific.data_size;
#endif /* DEBUG_SPEAKER_NODE*/
        /* update read pointer */
        AUDIO_SpeakerHandler->buf->rd_ptr += read_length;
        if(AUDIO_SpeakerHandler->buf->rd_ptr >= AUDIO_SpeakerHandler->buf->size)
        {
          AUDIO_SpeakerHandler->buf->rd_ptr = AUDIO_SpeakerHandler->buf->rd_ptr - AUDIO_SpeakerHandler->buf->size;
        }
#ifdef DEBUG_SPEAKER_NODE
        AUDIO_SpeakerDebugStats[AUDIO_SpeakerDebugStats_count].read = AUDIO_SpeakerHandler->buf->rd_ptr;
#endif /* DEBUG_SPEAKER_NODE*/
      }
#ifdef DEBUG_SPEAKER_NODE
      if(++AUDIO_SpeakerDebugStats_count == SPEAKER_DEBUG_BUFFER_SIZE)
      {
        AUDIO_SpeakerDebugStats_count = 0;
      }
#endif /* DEBUG_SPEAKER_NODE*/
    } /* AUDIO_SpeakerHandler->node.state == AUDIO_NODE_STARTED */
  }
}

/**
  * @brief  BSP_AUDIO_OUT_HalfTransfer_CallBack
  *         This function is called when half of the requested buffer has been transferred.
  * @param  None
  * @retval None
  */
void BSP_AUDIO_OUT_HalfTransfer_CallBack(void)
{}
/* Private functions ---------------------------------------------------------*/
/**
  * @brief  AUDIO_SpeakerDeInit
  *         De-Initializes the audio speaker node 
  * @param  node_handle(IN): speaker node handle must be initialized
  * @retval  : 0 if no error
  */
static int8_t AUDIO_SpeakerDeInit(uint32_t node_handle)
{
  AUDIO_SpeakerNode_t* speaker;

  speaker = (AUDIO_SpeakerNode_t*)node_handle;
  /* without a handler the transfer complete callback stops re-arming the DMA */
  AUDIO_SpeakerHandler = 0;
  if(speaker->node.state != AUDIO_NODE_OFF)
  {
    speaker->node.state = AUDIO_NODE_OFF;
    AUDIO_SpeakerPostCodecRequests(
        SPEAKER_CODEC_DEINIT, SPEAKER_CODEC_INIT | SPEAKER_CODEC_FREQUENCY | SPEAKER_CODEC_MUTE | SPEAKER_CODEC_VOLUME);
  }
  return 0;
}

/**
  * @brief  AUDIO_SpeakerStart
  *         Start the audio speaker node 
  * @param  buffer(IN):     buffer to use while node is being started
  * @param  node_handle(IN): speaker node handle must be initialized
  * @retval 0 if no error
  */
static int8_t AUDIO_SpeakerStart(AUDIO_CircularBuffer_t* buffer, uint32_t node_handle)
{
  AUDIO_SpeakerNode_t* speaker;

  speaker = (AUDIO_SpeakerNode_t*)node_handle;
  speaker->buf = buffer;
  speaker->specific.cmd = 0;
  AUDIO_SpeakerMute(0, speaker->node.audio_description->audio_mute, node_handle);
  AUDIO_SpeakerSetVolume(0, speaker->node.audio_description->audio_volume_db_256, node_handle);
  speaker->node.state = AUDIO_NODE_STARTED;
  return 0;
}

/**
  * @brief  AUDIO_SpeakerStop
  *         Stop speaker node. the speaker will be stopped after finalizing current packet transfer.
  * @param  node_handle(IN): speaker node handle must be Started
  * @retval 0 if no error
  */
static int8_t AUDIO_SpeakerStop(uint32_t node_handle)
{
  AUDIO_SpeakerNode_t* speaker;

  speaker = (AUDIO_SpeakerNode_t*)node_handle;
  speaker->specific.cmd |= SPEAKER_CMD_STOP;

  return 0;
}

/**
  * @brief  AUDIO_SpeakerChangeFrequency
  *         change frequency then stop speaker node
  * @param  node_handle: speaker node handle must be Started
  * @retval 0 if no error
  */
static int8_t AUDIO_SpeakerChangeFrequency(uint32_t node_handle)
{
  AUDIO_SpeakerNode_t* speaker;

  speaker = (AUDIO_SpeakerNode_t*)node_handle;
  speaker->specific.cmd |= SPEAKER_CMD_CHANGE_FREQUENCE;
  return 0;
}

/**
  * @brief  AUDIO_SpeakerInitInjectionsParams
  *         Stop speaker node
  * @param  speaker(IN): speaker node handle must be Started
  * @retval 0 if no error
  */
static void AUDIO_SpeakerInitInjectionsParams(AUDIO_SpeakerNode_t* speaker)
{
  speaker->packet_length = AUDIO_MS_PACKET_SIZE_FROM_AUD_DESC(speaker->node.audio_description);
  speaker->specific.injection_size = AUDIO_SPEAKER_INJECTION_LENGTH(speaker->node.audio_description);
  speaker->specific.double_buff = 0;
  speaker->specific.offset = 0;
#if USB_AUDIO_CONFIG_PLAY_RES_BIT == 24
  speaker->specific.double_buff = 1;
  speaker->specific.alt_buf_half_size = speaker->specific.injection_size;
#endif /* USB_AUDIO_CONFIG_PLAY_RES_BIT == 24*/
#if USB_AUDIO_CONFIG_PLAY_USE_FREQ_44_1_K
  if(speaker->node.audio_description->frequency == USB_AUDIO_CONFIG_FREQ_44_1_K)
  {
    speaker->specific.double_buff = 1;
    speaker->packet_length_max_44_1 = speaker->packet_length + AUDIO_SAMPLE_LENGTH(speaker->node.audio_description);
    speaker->specific.alt_buf_half_size = AUDIO_SPEAKER_MAX_INJECTION_LENGTH(speaker->node.audio_description);
  }
#endif /* USB_AUDIO_CONFIG_PLAY_USE_FREQ_44_1_K*/
  /* update alternative buffer */
  memset(speaker->specific.alt_buffer, 0, speaker->specific.injection_size);
  speaker->specific.data = speaker->specific.alt_buffer; /* start injection of dumped data */
  speaker->specific.data_size = speaker->specific.injection_size;
}
/**
  * @brief  AUDIO_SpeakerMute
  *         set Mute value to speaker
  * @param  channel_number(IN): channel number
* @param  mute(IN): mute value (0 : mute , 1 unmute)
  * @param  node_handle(IN): speaker node handle must be Started
  * @retval  : 0 if no error
  */
static int8_t AUDIO_SpeakerMute(uint16_t channel_number, uint8_t mute, uint32_t node_handle)
{
  AUDIO_SpeakerCodecMute = mute;
  AUDIO_SpeakerPostCodecRequests(SPEAKER_CODEC_MUTE, 0);
  return 0;
}

/**
  * @brief  AUDIO_SpeakerSetVolume
  *         set Volume value to speaker
  * @param  channel_number(IN): channel number
  * @param  volume_db_256(IN):  volume value in db
  * @param  node_handle(IN):    speaker node handle must be Started
  * @retval 0 if no error
  */
static int8_t AUDIO_SpeakerSetVolume(uint16_t channel_number, int volume_db_256, uint32_t node_handle)
{
  AUDIO_SpeakerCodecVolume = volume_db_256;
  AUDIO_SpeakerPostCodecRequests(SPEAKER_CODEC_VOLUME, 0);
  return 0;
}

#if USB_AUDIO_CONFIG_PLAY_RES_BIT == 24
/**
  * @brief  AUDIO_DoPadding_24_32
  *         padding 24bit  sample to 32 sample by adding zeros .
  * @param  buff_src(IN):          
  * @param  data_dest(OUT):          
  * @param  size(IN):               
  * @retval None
  */
static void AUDIO_DoPadding_24_32(AUDIO_CircularBuffer_t* buff_src, uint8_t* data_dest, int size)
{
  int k = 0, j = buff_src->rd_ptr;
  for(int i = 0; i < size; i += 3)
  {
    data_dest[k++] = 0;
    for(int p = 0; p < 3; p++)
    {
      if(j == buff_src->size)
      {
        j = 0;
      }
      data_dest[k++] = buff_src->data[j++];
    }
  }
}
#endif /* USB_AUDIO_CONFIG_PLAY_RES_BIT == 24   */

/**
  * @brief  AUDIO_SpeakerStartReadCount
  *         Start a counter of how much of byte has been read from the buffer(transmitted to SAI)
  * @param  node_handle: mic node handle must be started
  * @retval  : 0 if no error
  */
static int8_t AUDIO_SpeakerStartReadCount(uint32_t node_handle)
{
  AUDIO_SpeakerNode_t* speaker;

  speaker = (AUDIO_SpeakerNode_t*)node_handle;
  /* the host can stream before the main loop has set up the SAI and its DMA */
  if(!AUDIO_SpeakerCodecOn)
  {
    return 0;
  }
  speaker->specific.dma_remaining = __HAL_DMA_GET_COUNTER(haudio_out_sai.hdmatx);
  return 0;
}

/**
  * @brief  AUDIO_SpeakerGetLastReadCount
  *         return the number of bytes have been read and reset the counter
  * @param  node_handle: speaker node handle must be started
  * @retval  :  number of read bytes , 0 if  an error
  */

static uint16_t AUDIO_SpeakerGetLastReadCount(uint32_t node_handle)
{
  AUDIO_SpeakerNode_t* speaker;
  int cur_waiting_bytes, read_bytes, last_packet_size;

  speaker = (AUDIO_SpeakerNode_t*)node_handle;
  if(!AUDIO_SpeakerCodecOn)
  {
    return 0;
  }
  /* read remind value in dma buffer */
  cur_waiting_bytes = __HAL_DMA_GET_COUNTER(haudio_out_sai.hdmatx);
  last_packet_size = haudio_out_sai.XferSize;
  read_bytes = (speaker->specific.dma_remaining >= cur_waiting_bytes)
                   ? speaker->specific.dma_remaining - cur_waiting_bytes
                   : (last_packet_size - cur_waiting_bytes) + speaker->specific.dma_remaining;
  if(read_bytes < (last_packet_size >> 1))
  {
    read_bytes += last_packet_size;
  }
  speaker->specific.dma_remaining = cur_waiting_bytes;

  return read_bytes;
}
/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
