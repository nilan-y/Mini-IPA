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
static uint32_t device_id = VL53L4CD_ID;
static uint32_t distance_mm = 123U, range_status;
static unsigned int bus_inits, bus_deinits, sensor_inits, starts, stops, clears;

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
{ ++sensor_inits; s->IsInitialized = (init_status == 0); return init_status; }
int32_t VL53L4CD_Start(VL53L4CD_Object_t *s, uint32_t mode)
{
  assert(mode == VL53L4CD_MODE_ASYNC_ONESHOT);
  ++starts;
  return start_status;
}
int32_t VL53L4CD_Stop(VL53L4CD_Object_t *s) { ++stops; return stop_status; }
VL53L4CD_Error VL53L4CD_ClearInterrupt(Dev_t s) { ++clears; return 0; }
VL53L4CD_Error VL53L4CD_CheckForDataReady(Dev_t s, uint8_t *value)
{ *value = ready; return io_error; }
int32_t VL53L4CD_GetDistance(VL53L4CD_Object_t *s, VL53L4CD_Result_t *r)
{
  r->ZoneResult[0].Distance[0] = distance_mm;
  r->ZoneResult[0].Status[0] = range_status;
  return read_status;
}

uint8_t USBD_CDC_SetTxBuffer(USBD_HandleTypeDef *d, uint8_t *p, uint16_t n)
{ cdc.TxBuffer = p; cdc.TxLength = n; return USBD_OK; }
uint8_t USBD_CDC_SetRxBuffer(USBD_HandleTypeDef *d, uint8_t *p)
{ cdc.RxBuffer = p; return USBD_OK; }
uint8_t USBD_CDC_ReceivePacket(USBD_HandleTypeDef *d) { return USBD_OK; }
uint8_t USBD_CDC_TransmitPacket(USBD_HandleTypeDef *d)
{ cdc.TxState = 1U; return USBD_OK; }

static void expect_reply(const char *expected)
{
  SensorApp_Process();
  assert(cdc.TxState == 1U);
  assert(cdc.TxLength == strlen(expected));
  assert(memcmp(cdc.TxBuffer, expected, cdc.TxLength) == 0);
  assert(SensorApp_IsBusy());
  /* No overwrite or second measurement while USB owns the reply buffer. */
  unsigned int before = starts;
  SensorApp_Measure();
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
  init_status = 0;
  SensorApp_Measure();
  assert(starts == 1U && clears == 1U);
  ready = 0;
  SensorApp_Process();
  assert(cdc.TxState == 0U && SensorApp_IsBusy());
  ready = 1;
  expect_reply("DISTANCE_MM=123\r\n");
  assert(stops == 1U && usb_led == GPIO_PIN_SET);

  /* A second click gets a new sample and clears the previous ready flag. */
  unsigned int previous_inits = sensor_inits;
  distance_mm = 456U;
  SensorApp_Measure();
  expect_reply("DISTANCE_MM=456\r\n");
  assert(clears == 2U && sensor_inits == previous_inits && bus_inits == 2U);
  range_status = 4;
  SensorApp_Measure();
  expect_reply("ERROR TOF RANGE_STATUS=4\r\n");
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
  expect_reply("DISTANCE_MM=456\r\n");

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
  puts("Sensor/USB integration tests passed");
  return 0;
}
