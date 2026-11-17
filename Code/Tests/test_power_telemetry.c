/* Exercise the actual main-loop service, with hardware and sensor stubs. */
#include "main.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Keep the host test independent of ARM interrupt instructions. */
#define __disable_irq() ((void)0)
#define main firmware_main
#include "../Core/Src/main.c"
#undef main

static uint32_t test_tick;
static unsigned int adc_starts, adc_stops, distance_starts, distance_stops;
static uint8_t usb_busy;
static uint8_t usb_configured = 1U;
static uint8_t *usb_buffer;
static uint16_t usb_length;
static unsigned int transmissions;
static unsigned int single_requests;
static GPIO_PinState button_state = GPIO_PIN_SET;

uint32_t HAL_GetTick(void) { return test_tick; }
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *adc, uint32_t *data, uint32_t n)
{
  assert(adc == &hadc && n == 2U && data == (uint32_t *)adc_values);
  ++adc_starts;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_Stop_DMA(ADC_HandleTypeDef *adc)
{ ++adc_stops; return HAL_OK; }
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state) {}
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
  assert(port == BTN_GPIO_Port && pin == BTN_Pin);
  return button_state;
}
HAL_TIM_ChannelStateTypeDef HAL_TIMEx_GetChannelNState(const TIM_HandleTypeDef *timer, uint32_t channel)
{ return HAL_TIM_CHANNEL_STATE_READY; }
HAL_StatusTypeDef HAL_TIMEx_PWMN_Start(TIM_HandleTypeDef *timer, uint32_t channel)
{ return HAL_OK; }
HAL_StatusTypeDef HAL_TIMEx_PWMN_Stop(TIM_HandleTypeDef *timer, uint32_t channel)
{ return HAL_OK; }
void SensorApp_StartContinuous(void) { ++distance_starts; }
void SensorApp_Stop(void) { ++distance_stops; }
void SensorApp_Measure(void) { ++single_requests; }
uint8_t CDC_IsTxBusy_FS(void) { return usb_busy; }
uint8_t CDC_Transmit_FS(uint8_t *buffer, uint16_t length)
{
  if (!usb_configured) return USBD_FAIL;
  if (usb_busy) return USBD_BUSY;
  usb_buffer = buffer;
  usb_length = length;
  usb_busy = 1U;
  ++transmissions;
  return USBD_OK;
}

static void complete_adc(void)
{
  adc_values[0] = 4095U;
  adc_values[1] = 4095U;
  hadc.Instance = ADC1;
  HAL_ADC_ConvCpltCallback(&hadc);
  ProcessPowerMeasurements();
}

static void expect_text(const char *text)
{
  assert(usb_length == strlen(text));
  assert(memcmp(usb_buffer, text, usb_length) == 0);
}

int main(void)
{
  test_tick = 100U;
  ExecuteUsbCommand(1U);
  test_tick = 599U;
  ProcessPowerMeasurements();
  assert(adc_starts == 0U && transmissions == 0U);
  test_tick = 600U;
  ProcessPowerMeasurements();
  assert(adc_starts == 1U);
  complete_adc();
  assert(adc_stops == 1U && transmissions == 1U);
  assert(power_photodiode == 7U && power_shunt == 547U);
  expect_text("PL: 7\r\nPS: 547\r\n");

  /* New results must not overwrite a buffer USB is still transmitting. */
  PowerTelemetry_Update(1000U, 50000U);
  PowerTelemetry_Process();
  expect_text("PL: 7\r\nPS: 547\r\n");
  assert(transmissions == 1U);
  usb_busy = 0U;
  PowerTelemetry_Process();
  expect_text("PL: 1000\r\nPS: 50000\r\n");
  usb_busy = 0U;

  /* One START is sufficient for repeated 500-ms measurement cycles. */
  test_tick = 1099U;
  ProcessPowerMeasurements();
  assert(adc_starts == 1U);
  test_tick = 1100U;
  ProcessPowerMeasurements();
  assert(adc_starts == 2U);
  complete_adc();
  expect_text("PL: 7\r\nPS: 547\r\n");
  usb_busy = 0U;

  /* Simulated distance TX keeps its buffer while power waits for USB. */
  uint8_t distance[] = "D: 123mm\r\n";
  assert(CDC_Transmit_FS(distance, sizeof(distance) - 1U) == USBD_OK);
  PowerTelemetry_Update(65535U, 65535U);
  PowerTelemetry_Process();
  assert(usb_buffer == distance);
  usb_busy = 0U;
  PowerTelemetry_Process();
  expect_text("PL: 65535\r\nPS: 65535\r\n");
  usb_busy = 0U;

  /* Stop cancels pending reports; restarting with 1 works again. */
  PowerTelemetry_Update(42U, 43U);
  ExecuteUsbCommand(2U);
  unsigned int previous = transmissions;
  test_tick = 3000U;
  ProcessPowerMeasurements();
  assert(transmissions == previous && adc_starts == 2U);
  ExecuteUsbCommand(1U);
  test_tick += 500U;
  ProcessPowerMeasurements();
  assert(adc_starts == 3U);
  complete_adc();
  assert(transmissions == previous + 1U);
  usb_busy = 0U;

  /* Disconnected USB retains only the latest pending pair. */
  usb_configured = 0U;
  PowerTelemetry_Update(100U, 200U);
  PowerTelemetry_Process();
  PowerTelemetry_Update(300U, 400U);
  usb_configured = 1U;
  PowerTelemetry_Process();
  expect_text("PL: 300\r\nPS: 400\r\n");
  usb_busy = 0U;

  /* Unsigned scheduling remains correct across the HAL tick wraparound. */
  ExecuteUsbCommand(2U);
  test_tick = UINT32_MAX - 100U;
  ExecuteUsbCommand(1U);
  previous = adc_starts;
  test_tick += 499U;
  ProcessPowerMeasurements();
  assert(adc_starts == previous);
  ++test_tick;
  ProcessPowerMeasurements();
  assert(adc_starts == previous + 1U);
  ExecuteUsbCommand(2U);
  assert(adc_running == 0U && adc_ready == 0U);
  assert(distance_starts == 3U && distance_stops == 3U);

  /* Bounce is rejected; a stable press triggers once, even when held. */
  test_tick = 10000U;
  button_state = GPIO_PIN_RESET;
  ProcessButton();
  test_tick += 5U;
  button_state = GPIO_PIN_SET;
  ProcessButton();
  test_tick += 5U;
  button_state = GPIO_PIN_RESET;
  ProcessButton();
  test_tick += 29U;
  ProcessButton();
  assert(single_requests == 0U);
  ++test_tick;
  ProcessButton();
  assert(single_requests == 1U);
  test_tick += 1000U;
  ProcessButton();
  assert(single_requests == 1U);

  /* Standalone button single-shot also refreshes power once without command 1. */
  usb_busy = 0U;
  unsigned int before_adc = adc_starts;
  ProcessPowerMeasurements();
  assert(adc_starts == before_adc + 1U);
  complete_adc();
  expect_text("PL: 7\r\nPS: 547\r\n");
  usb_busy = 0U;
  test_tick += 500U;
  ProcessPowerMeasurements();
  assert(adc_starts == before_adc + 1U && !power_measurement_enabled);

  /* Release must also be stable before another press is accepted. */
  button_state = GPIO_PIN_SET;
  ProcessButton();
  test_tick += 30U;
  ProcessButton();
  button_state = GPIO_PIN_RESET;
  ProcessButton();
  test_tick += 30U;
  ProcessButton();
  assert(single_requests == 2U);
  ExecuteUsbCommand(2U);
  puts("Power telemetry and 2 Hz main-loop tests passed");
  return 0;
}
