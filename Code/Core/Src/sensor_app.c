#include "sensor_app.h"
#include "main.h"
#include "custom_bus.h"
#include "vl53l4cd_api.h"
#include "usbd_cdc_if.h"
#include <stdio.h>
#include <string.h>

#define MEASUREMENT_TIMEOUT_MS 500U
#define USB_REPLY_TIMEOUT_MS 1000U
/* Known calibration correction, applied by the sensor (not again in USB data). */
#define SENSOR_OFFSET_MM ((int16_t)6)

typedef enum { IDLE, MEASURING, REPLY_PENDING, REPLY_SENDING } SensorAppState;
static SensorAppState state;
static VL53L4CD_Object_t sensor;
static uint8_t bus_registered;
static uint32_t started_at;
/* USB CDC transmits asynchronously: keep this buffer until TX completes. */
static char reply[80];

static void ReplyReady(void)
{
  started_at = HAL_GetTick();
  state = REPLY_PENDING;
}

static void ReplyError(const char *reason)
{
  (void)snprintf(reply, sizeof(reply), "ERROR TOF %s\r\n", reason);
  ReplyReady();
}

static uint8_t InitSensor(void)
{
  uint32_t id = 0U;
  VL53L4CD_IO_t io = {
    .Init = BSP_I2C1_Init,
    .DeInit = BSP_I2C1_DeInit,
    .Address = VL53L4CD_DEVICE_ADDRESS,
    .WriteReg = BSP_I2C1_Send,
    .ReadReg = BSP_I2C1_Recv,
    .GetTick = BSP_GetTick
  };

  /* XSHUT is active low. Use the already configured GPIO and I2C BSP. */
  HAL_GPIO_WritePin(SHUT_TOF_GPIO_Port, SHUT_TOF_Pin, GPIO_PIN_RESET);
  HAL_Delay(2U);
  HAL_GPIO_WritePin(SHUT_TOF_GPIO_Port, SHUT_TOF_Pin, GPIO_PIN_SET);
  HAL_Delay(10U);

  if (bus_registered == 0U)
  {
    if (VL53L4CD_RegisterBusIO(&sensor, &io) != VL53L4CD_OK)
    {
      (void)BSP_I2C1_DeInit();
      ReplyError("I2C_INIT");
      return 0U;
    }
    bus_registered = 1U;
  }
  /* Probe before loading configuration; report missing/wrong hardware. */
  if ((VL53L4CD_ReadID(&sensor, &id) != VL53L4CD_OK) || (id != VL53L4CD_ID))
  {
    ReplyError("NOT_FOUND");
    return 0U;
  }
  if (VL53L4CD_Init(&sensor) != VL53L4CD_OK)
  {
    ReplyError("INIT");
    return 0U;
  }
  /* SensorInit/XSHUT resets calibration registers: restore after every init. */
  if (VL53L4CD_SetOffset(&sensor, SENSOR_OFFSET_MM) != VL53L4CD_ERROR_NONE)
  {
    sensor.IsInitialized = 0U;
    ReplyError("OFFSET");
    return 0U;
  }
  return 1U;
}

void SensorApp_Measure(void)
{
  if (state != IDLE)
  {
    return;
  }
  if ((sensor.IsInitialized == 0U) && (InitSensor() == 0U))
  {
    return;
  }
  /* Discard the previous ready flag so each click gets a fresh sample. */
  if ((VL53L4CD_ClearInterrupt(&sensor) != VL53L4CD_ERROR_NONE) ||
      (VL53L4CD_Start(&sensor, VL53L4CD_MODE_ASYNC_ONESHOT) != VL53L4CD_OK))
  {
    sensor.IsInitialized = 0U;
    ReplyError("START");
    return;
  }
  started_at = HAL_GetTick();
  state = MEASURING;
}

void SensorApp_Process(void)
{
  if (state == MEASURING)
  {
    uint8_t ready = 0U;
    VL53L4CD_Result_t result = {0};

    /* Use ST's data-ready API so polling does not block the main loop. */
    if (VL53L4CD_CheckForDataReady(&sensor, &ready) != VL53L4CD_ERROR_NONE)
    {
      (void)VL53L4CD_Stop(&sensor);
      sensor.IsInitialized = 0U;
      ReplyError("I2C_READ");
    }
    else if (ready != 0U)
    {
      int32_t read_status = VL53L4CD_GetDistance(&sensor, &result);
      int32_t stop_status = VL53L4CD_Stop(&sensor);
      if ((read_status != VL53L4CD_OK) || (stop_status != VL53L4CD_OK))
      {
        sensor.IsInitialized = 0U;
        ReplyError("READ_OR_STOP");
      }
      else if (result.ZoneResult[0].Status[0] != 0U)
      {
        (void)snprintf(reply, sizeof(reply), "Out of Range=%lu\r\n",
                       (unsigned long)result.ZoneResult[0].Status[0]);
        ReplyReady();
      }
      else
      {
        (void)snprintf(reply, sizeof(reply), "D: %lumm\r\n",
                       (unsigned long)result.ZoneResult[0].Distance[0]);
        ReplyReady();
      }
    }
    else if ((uint32_t)(HAL_GetTick() - started_at) >= MEASUREMENT_TIMEOUT_MS)
    {
      (void)VL53L4CD_Stop(&sensor);
      sensor.IsInitialized = 0U;
      ReplyError("TIMEOUT");
    }
  }

  if (state == REPLY_PENDING)
  {
    if (CDC_Transmit_FS((uint8_t *)reply, (uint16_t)strlen(reply)) == USBD_OK)
    {
      state = REPLY_SENDING;
    }
    else if ((uint32_t)(HAL_GetTick() - started_at) >= USB_REPLY_TIMEOUT_MS)
    {
      /* Disconnected/not accepting replies: do not block commands forever. */
      state = IDLE;
    }
  }
  else if ((state == REPLY_SENDING) && (CDC_IsTxBusy_FS() == 0U))
  {
    state = IDLE;
  }
}

uint8_t SensorApp_IsBusy(void)
{
  return (state != IDLE) ? 1U : 0U;
}
