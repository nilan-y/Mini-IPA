#include "display_app.h"
#include "main.h"
#include "oled.h"
#include <stdio.h>

static uint8_t available;
static uint8_t distance_dirty;
static uint8_t power_dirty;
static uint32_t last_update;
static char distance_text[11] = "-- mm";
static uint16_t latest_photodiode;
static uint16_t latest_shunt;
static uint8_t power_valid;

void DisplayApp_Init(uint8_t display_available)
{
  available = display_available;
  distance_dirty = 1U;
  power_dirty = 1U;
  last_update = HAL_GetTick() - 500U;
  if (available != 0U)
  {
    OLED_Print(0U, 0U, "Distanz");
  }
}

void DisplayApp_SetDistanceError(const char *text)
{
  /* Bounded copy and change detection without another formatting buffer. */
  for (uint8_t i = 0U; i < sizeof(distance_text) - 1U; ++i)
  {
    char next = text[i];
    if (distance_text[i] != next)
    {
      distance_dirty = 1U;
    }
    distance_text[i] = next;
    if (next == '\0')
    {
      return;
    }
  }
  distance_text[sizeof(distance_text) - 1U] = '\0';
}

void DisplayApp_SetDistance(uint32_t millimeters)
{
  char next[sizeof(distance_text)];
  /* The VL53L4CD result is a uint16_t; cap input to keep the 2x text in bounds. */
  if (millimeters > UINT16_MAX)
  {
    DisplayApp_SetDistanceError("Ungueltig");
    return;
  }
  (void)snprintf(next, sizeof(next), "%lu mm", (unsigned long)millimeters);
  DisplayApp_SetDistanceError(next);
}

void DisplayApp_SetPower(uint16_t photodiode, uint16_t shunt)
{
  if ((power_valid == 0U) || (photodiode != latest_photodiode) ||
      (shunt != latest_shunt))
  {
    latest_photodiode = photodiode;
    latest_shunt = shunt;
    power_valid = 1U;
    power_dirty = 1U;
  }
}

void DisplayApp_Process(void)
{
  uint32_t now = HAL_GetTick();
  if ((available == 0U) || ((uint32_t)(now - last_update) < 500U) ||
      ((distance_dirty == 0U) && (power_dirty == 0U)))
  {
    return;
  }
  last_update = now;
  if (distance_dirty != 0U)
  {
    /* Ten 2x characters fill twenty columns, overwriting all old digits. */
    OLED_Printf2x(0U, 1U, "%-10s", distance_text);
    distance_dirty = 0U;
  }
  if (power_dirty != 0U)
  {
    if (power_valid != 0U)
    {
      OLED_Printf(0U, 4U, "PL: %5u mW        ", (unsigned)latest_photodiode);
      OLED_Printf(0U, 5U, "PS: %5u mW        ", (unsigned)latest_shunt);
    }
    else
    {
      OLED_Print(0U, 4U, "PL:    -- mW        ");
      OLED_Print(0U, 5U, "PS:    -- mW        ");
    }
    power_dirty = 0U;
  }
}
