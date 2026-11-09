/* Real application + CDC callbacks, with hardware/driver boundaries simulated. */
#include "main.h"
#include "custom_bus.h"
#include "sensor_app.h"
#include "usb_commands.h"
#include "usbd_cdc_if.h"
#include "vl53l4cd_api.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

USBD_HandleTypeDef hUsbDeviceFS;
static USBD_CDC_HandleTypeDef cdc;
static uint32_t tick;
static GPIO_PinState usb_led;
static int32_t bus_status, init_status, start_status, read_status, stop_status;
static uint8_t ready, io_error;
static uint8_t offset_status, offset_applied;
static unsigned int offset_calls;
static uint32_t device_id = VL53L4CD_ID;
static uint32_t distance_mm = 123U, range_status;
static unsigned int bus_inits, bus_deinits, sensor_inits, starts, stops, clears;
static unsigned int transmissions;
static VL53L4CD_Object_t *active_sensor;

uint32_t HAL_GetTick(void) { return tick; }
void HAL_Delay(uint32_t delay) { tick += delay; }
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState value)
{
  if ((port == LED_USB_GPIO_Port) && (pin == LED_USB_Pin)) usb_led = value;
}
int32_t BSP_I2C1_Init(void) { ++bus_inits; return bus_status; }
int32_t BSP_I2C1_DeInit(void) { ++bus_deinits; return 0; }
int32_t BSP_GetTick(void) { return (int32_t)tick; }
int32_t BSP_I2C1_Send(uint16_t a, uint8_t *d, uint16_t n) { return 0; }
int32_t BSP_I2C1_Recv(uint16_t a, uint8_t *d, uint16_t n) { return 0; }

int32_t VL53L4CD_RegisterBusIO(VL53L4CD_Object_t *s, VL53L4CD_IO_t *io)
{
  assert(io->Address == VL53L4CD_DEVICE_ADDRESS);
  assert(io->WriteReg == BSP_I2C1_Send && io->ReadReg == BSP_I2C1_Recv);
  s->IO = *io;
  return io->Init();
}
int32_t VL53L4CD_ReadID(VL53L4CD_Object_t *s, uint32_t *id)
{ *id = device_id; return 0; }
int32_t VL53L4CD_Init(VL53L4CD_Object_t *s)
{
  ++sensor_inits;
  offset_applied = 0U;
  s->IsInitialized = (init_status == 0);
  return init_status;
}
VL53L4CD_Error VL53L4CD_SetOffset(Dev_t s, int16_t offset)
{
  assert(s->IsInitialized != 0U);
  assert(offset == 6);
  ++offset_calls;
  offset_applied = (offset_status == 0U);
  return offset_status;
}
int32_t VL53L4CD_Start(VL53L4CD_Object_t *s, uint32_t mode)
{
  assert((mode == VL53L4CD_MODE_ASYNC_ONESHOT) ||
         (mode == VL53L4CD_MODE_ASYNC_CONTINUOUS));
  /* Each sensor reinitialization must restore the correction before ranging. */
  assert(offset_applied != 0U);
  ++starts;
  active_sensor = s;
  if (start_status == 0)
  {
    s->IsRanging = 1U;
    s->IsContinuous = (mode == VL53L4CD_MODE_ASYNC_CONTINUOUS);
  }
  return start_status;
}
int32_t VL53L4CD_Stop(VL53L4CD_Object_t *s)
{
  ++stops;
  if (stop_status == 0) s->IsRanging = 0U;
  return stop_status;
}
VL53L4CD_Error VL53L4CD_ClearInterrupt(Dev_t s) { ++clears; return 0; }
VL53L4CD_Error VL53L4CD_CheckForDataReady(Dev_t s, uint8_t *value)
{ *value = ready; return io_error; }
int32_t VL53L4CD_GetDistance(VL53L4CD_Object_t *s, VL53L4CD_Result_t *r)
{
  r->ZoneResult[0].Distance[0] = distance_mm;
  r->ZoneResult[0].Status[0] = range_status;
  /* The real ST BSP acknowledges continuous results with ClearInterrupt. */
  if ((read_status == 0) && s->IsContinuous) ++clears;
  return read_status;
}

uint8_t USBD_CDC_SetTxBuffer(USBD_HandleTypeDef *d, uint8_t *p, uint16_t n)
{ cdc.TxBuffer = p; cdc.TxLength = n; return USBD_OK; }
uint8_t USBD_CDC_SetRxBuffer(USBD_HandleTypeDef *d, uint8_t *p)
{ cdc.RxBuffer = p; return USBD_OK; }
uint8_t USBD_CDC_ReceivePacket(USBD_HandleTypeDef *d) { return USBD_OK; }
uint8_t USBD_CDC_TransmitPacket(USBD_HandleTypeDef *d)
{ ++transmissions; cdc.TxState = 1U; return USBD_OK; }

static void expect_reply(const char *expected)
{
  SensorApp_Process();
  assert(cdc.TxState == 1U);
  assert(cdc.TxLength == strlen(expected));
  assert(memcmp(cdc.TxBuffer, expected, cdc.TxLength) == 0);
  assert(SensorApp_IsBusy());
  /* No overwrite or spontaneous second measurement while USB owns the buffer. */
  unsigned int before = starts;
  SensorApp_Process();
  assert(starts == before);
  assert(memcmp(cdc.TxBuffer, expected, cdc.TxLength) == 0);
  cdc.TxState = 0U; /* Simulate transfer completion. */
  SensorApp_Process();
  assert(!SensorApp_IsBusy());
}

int main(void)
{
  uint8_t data[] = "1\n";
  uint32_t length = 2U;
  uint8_t command;
  tick = 1000U;
  assert(CDC_Transmit_FS(data, 2U) == USBD_FAIL); /* No null dereference. */
  hUsbDeviceFS.pClassData = &cdc;
  hUsbDeviceFS.dev_state = USBD_STATE_CONFIGURED;
  assert(USBD_Interface_fops_FS.Init() == USBD_OK);
  assert(USBD_Interface_fops_FS.Receive(data, &length) == USBD_OK);
  assert(USB_Commands_Read(&command) && command == 1U);
  assert(usb_led == GPIO_PIN_SET);
  CDC_ProcessActivity_FS();
  tick += 99U;
  CDC_ProcessActivity_FS();
  assert(usb_led == GPIO_PIN_SET);
  ++tick;
  CDC_ProcessActivity_FS();
  assert(usb_led == GPIO_PIN_RESET);

  bus_status = -1;
  SensorApp_Measure();
  expect_reply("ERROR TOF I2C_INIT\r\n");
  assert(bus_inits == 1U && bus_deinits == 1U);
  bus_status = 0;
  device_id = 0;
  SensorApp_Measure();
  expect_reply("ERROR TOF NOT_FOUND\r\n");
  assert(sensor_inits == 0U);
  device_id = VL53L4CD_ID;
  init_status = -1;
  SensorApp_Measure();
  expect_reply("ERROR TOF INIT\r\n");
  assert(offset_calls == 0U);
  init_status = 0;
  offset_status = 1U;
  SensorApp_Measure();
  expect_reply("ERROR TOF OFFSET\r\n");
  assert(starts == 0U && offset_calls == 1U);
  offset_status = 0U;
  SensorApp_Measure();
  assert(offset_calls == 2U);
  assert(starts == 1U && clears == 1U);
  ready = 0;
  SensorApp_Process();
  assert(cdc.TxState == 0U && SensorApp_IsBusy());
  ready = 1;
  expect_reply("D: 123mm\r\n");
  assert(stops == 1U && usb_led == GPIO_PIN_SET);

  /* A second click gets a new sample and clears the previous ready flag. */
  unsigned int previous_inits = sensor_inits;
  unsigned int previous_offsets = offset_calls;
  distance_mm = 456U;
  SensorApp_Measure();
  expect_reply("D: 456mm\r\n");
  assert(clears == 2U && sensor_inits == previous_inits && bus_inits == 2U);
  assert(offset_calls == previous_offsets);
  range_status = 4;
  SensorApp_Measure();
  expect_reply("Out of Range=4\r\n");
  range_status = 0;

  /* Polling timeout and LED expiry work across HAL tick wraparound. */
  tick = UINT32_MAX - 200U;
  ready = 0;
  SensorApp_Measure();
  tick += 500U;
  expect_reply("ERROR TOF TIMEOUT\r\n");
  CDC_ProcessActivity_FS();
  tick += 100U;
  CDC_ProcessActivity_FS();
  assert(usb_led == GPIO_PIN_RESET);

  SensorApp_Measure();
  io_error = 1;
  assert(offset_calls == previous_offsets + 1U);
  expect_reply("ERROR TOF I2C_READ\r\n");
  io_error = 0;
  start_status = -1;
  SensorApp_Measure();
  expect_reply("ERROR TOF START\r\n");
  start_status = 0;
  ready = 1;
  read_status = -1;
  SensorApp_Measure();
  expect_reply("ERROR TOF READ_OR_STOP\r\n");
  read_status = 0;
  stop_status = -1;
  SensorApp_Measure();
  expect_reply("ERROR TOF READ_OR_STOP\r\n");
  stop_status = 0;

  /* Busy CDC retries without replacing the in-flight buffer. */
  cdc.TxState = 1U;
  uint8_t *previous_buffer = cdc.TxBuffer;
  SensorApp_Measure();
  SensorApp_Process();
  assert(SensorApp_IsBusy() && cdc.TxBuffer == previous_buffer);
  cdc.TxState = 0;
  expect_reply("D: 456mm\r\n");

  tick = UINT32_MAX - 50U;
  assert(CDC_Transmit_FS(data, 2U) == USBD_OK);
  CDC_ProcessActivity_FS();
  tick += 99U;
  CDC_ProcessActivity_FS();
  assert(usb_led == GPIO_PIN_SET);
  ++tick;
  CDC_ProcessActivity_FS();
  assert(usb_led == GPIO_PIN_RESET);
  cdc.TxState = 0U;

  /* Disconnected interface drops pending replies after a bounded wait. */
  SensorApp_Measure();
  hUsbDeviceFS.pClassData = NULL;
  SensorApp_Process();
  assert(SensorApp_IsBusy());
  tick += 1000U;
  SensorApp_Process();
  assert(!SensorApp_IsBusy());
  hUsbDeviceFS.pClassData = &cdc;
  ready = 0U;

  /* Stream starts once, sends successive fresh samples, and keeps ranging. */
  unsigned int before_starts = starts, before_stops = stops;
  SensorApp_StartContinuous();
  assert(active_sensor->IsContinuous && active_sensor->IsRanging);
  SensorApp_StartContinuous();
  assert(starts == before_starts + 1U);
  for (unsigned int i = 0U; i < 3U; ++i)
  {
    distance_mm = 100U + i;
    char expected[32];
    (void)snprintf(expected, sizeof(expected), "D: %umm\r\n", 100U + i);
    ready = 1U;
    SensorApp_Process();
    assert(cdc.TxState == 1U);
    assert(memcmp(cdc.TxBuffer, expected, cdc.TxLength) == 0);
    SensorApp_StartContinuous(); /* Repeated 1 must not restart the sensor. */
    assert(starts == before_starts + 1U && stops == before_stops);
    ready = 0U;
    cdc.TxState = 0U;
    SensorApp_Process();
    assert(SensorApp_IsBusy() && cdc.TxState == 0U);
  }
  SensorApp_Stop(); /* Command 2 while waiting for a measurement. */
  assert(!active_sensor->IsRanging && !SensorApp_IsBusy());
  before_starts = transmissions;
  ready = 1U;
  SensorApp_Process();
  assert(transmissions == before_starts);
  SensorApp_Stop(); /* Repeated 2 is harmless. */

  /* Stop during accepted USB TX retains the buffer, but stops hardware now. */
  SensorApp_StartContinuous();
  SensorApp_Process();
  assert(cdc.TxState == 1U);
  uint8_t saved_reply[80];
  uint32_t saved_length = cdc.TxLength;
  memcpy(saved_reply, cdc.TxBuffer, saved_length);
  SensorApp_Stop();
  assert(!active_sensor->IsRanging);
  before_starts = transmissions;
  SensorApp_Process();
  assert(transmissions == before_starts);
  assert(memcmp(saved_reply, cdc.TxBuffer, saved_length) == 0);
  cdc.TxState = 0U;
  SensorApp_Process();
  assert(!SensorApp_IsBusy() && transmissions == before_starts);

  /* Stop also cancels a result waiting for the USB endpoint to become free. */
  cdc.TxState = 1U;
  SensorApp_StartContinuous();
  SensorApp_Process();
  SensorApp_Stop();
  cdc.TxState = 0U;
  SensorApp_Process();
  assert(!SensorApp_IsBusy() && transmissions == before_starts);

  /* Command 3 switches an active stream to exactly one fresh sample. */
  SensorApp_StartContinuous();
  SensorApp_Process();
  assert(cdc.TxState == 1U);
  memcpy(saved_reply, cdc.TxBuffer, cdc.TxLength);
  saved_length = cdc.TxLength;
  SensorApp_Measure();
  assert(!active_sensor->IsRanging);
  SensorApp_Process();
  assert(memcmp(saved_reply, cdc.TxBuffer, saved_length) == 0);
  ready = 0U;
  cdc.TxState = 0U;
  SensorApp_Process();
  assert(active_sensor->IsRanging && !active_sensor->IsContinuous);
  ready = 1U;
  expect_reply("D: 102mm\r\n");

  /* Stop errors disable streaming and are reported once USB is available. */
  SensorApp_StartContinuous();
  SensorApp_Process();
  stop_status = -1;
  SensorApp_Stop();
  SensorApp_Process();
  cdc.TxState = 0U;
  expect_reply("ERROR TOF STOP\r\n");
  stop_status = 0;

  /* A sensor timeout terminates the stream; a later 1 can start it again. */
  ready = 0U;
  SensorApp_StartContinuous();
  tick += 500U;
  expect_reply("ERROR TOF TIMEOUT\r\n");
  SensorApp_StartContinuous();
  assert(active_sensor->IsRanging && active_sensor->IsContinuous);
  SensorApp_Stop();
  assert(!SensorApp_IsBusy());

  /* Repeated single triggers wait for USB completion and each get one sample. */
  ready = 0U;
  SensorApp_Measure();
  SensorApp_Measure();
  ready = 1U;
  SensorApp_Process();
  assert(cdc.TxState == 1U);
  before_starts = starts;
  ready = 0U;
  cdc.TxState = 0U;
  SensorApp_Process();
  assert(starts == before_starts + 1U && !active_sensor->IsContinuous);
  ready = 1U;
  expect_reply("D: 102mm\r\n");

  /* Stop cancels queued single triggers as well as the active measurement. */
  ready = 0U;
  SensorApp_Measure();
  SensorApp_Measure();
  SensorApp_Stop();
  before_starts = starts;
  SensorApp_Process();
  assert(starts == before_starts && !SensorApp_IsBusy());
  puts("Sensor/USB integration tests passed");
  return 0;
}
