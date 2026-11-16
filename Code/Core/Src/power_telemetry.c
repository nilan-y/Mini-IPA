#include "power_telemetry.h"
#include "usbd_cdc_if.h"
#include <stdio.h>

static uint16_t latest_photodiode;
static uint16_t latest_shunt;
static uint8_t pending;
static uint8_t sending;
/* CDC keeps the supplied pointer until transmission completes. */
static char reply[40];

void PowerTelemetry_Update(uint16_t photodiode, uint16_t shunt)
{
  latest_photodiode = photodiode;
  latest_shunt = shunt;
  pending = 1U;
}

void PowerTelemetry_Process(void)
{
  if (sending != 0U)
  {
    if (CDC_IsTxBusy_FS() != 0U)
    {
      return;
    }
    sending = 0U;
  }
  if ((pending != 0U) && (CDC_IsTxBusy_FS() == 0U))
  {
    int length = snprintf(reply, sizeof(reply), "PL: %u\r\nPS: %u\r\n",
                          (unsigned)latest_photodiode, (unsigned)latest_shunt);
    if ((length > 0) && ((unsigned)length < sizeof(reply)) &&
        (CDC_Transmit_FS((uint8_t *)reply, (uint16_t)length) == USBD_OK))
    {
      pending = 0U;
      sending = 1U;
    }
  }
}

void PowerTelemetry_ClearPending(void)
{
  /* Keep an accepted transmission's buffer intact until USB releases it. */
  pending = 0U;
}
