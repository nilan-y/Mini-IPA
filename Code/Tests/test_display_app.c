#include "main.h"
#include "display_app.h"
#include "oled_mock.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t tick;
uint32_t HAL_GetTick(void) { return tick; }

int main(void)
{
  DisplayApp_Init(0U);
  DisplayApp_SetDistance(123U);
  DisplayApp_SetPower(1000U, 2000U);
  DisplayApp_Process();
  assert(test_oled_writes == 0U);

  DisplayApp_Init(1U);
  DisplayApp_Process();
  assert(strncmp(test_oled_rows[0], "Distanz", 7U) == 0);
  assert(strncmp(test_oled_rows[1], "123 mm    ", 10U) == 0);
  assert(strcmp(test_oled_rows[4], "PL:  1000 mW        ") == 0);
  assert(strcmp(test_oled_rows[5], "PS:  2000 mW        ") == 0);
  unsigned int writes = test_oled_writes;

  /* Keep high-frequency results; only render the latest every 500 ms. */
  DisplayApp_SetDistance(65535U);
  tick = 499U;
  DisplayApp_Process();
  assert(test_oled_writes == writes);
  DisplayApp_SetDistance(7U);
  DisplayApp_SetPower(1U, 2U);
  tick = 500U;
  DisplayApp_Process();
  assert(strncmp(test_oled_rows[1], "7 mm      ", 10U) == 0);
  assert(strcmp(test_oled_rows[4], "PL:     1 mW        ") == 0);
  DisplayApp_Process();
  writes = test_oled_writes;
  DisplayApp_SetDistance(7U);
  DisplayApp_SetPower(1U, 2U);
  tick += 500U;
  DisplayApp_Process();
  assert(test_oled_writes == writes); /* No redraw for unchanged values. */

  DisplayApp_SetDistanceError("Ungueltig");
  DisplayApp_Process();
  assert(strncmp(test_oled_rows[1], "Ungueltig ", 10U) == 0);
  tick += 500U;
  DisplayApp_SetDistance(12345U);
  DisplayApp_Process();
  assert(strncmp(test_oled_rows[1], "12345 mm  ", 10U) == 0);
  tick += 500U;
  DisplayApp_SetDistanceError("TOF Fehler");
  DisplayApp_Process();
  assert(strncmp(test_oled_rows[1], "TOF Fehler", 10U) == 0);

  /* Bounded status copying preserves truncation and detects real changes. */
  tick += 500U;
  DisplayApp_SetDistanceError("ABCDEFGHIJKL");
  DisplayApp_Process();
  assert(strncmp(test_oled_rows[1], "ABCDEFGHIJ", 10U) == 0);
  writes = test_oled_writes;
  tick += 500U;
  DisplayApp_SetDistanceError("ABCDEFGHIJdifferent suffix");
  DisplayApp_Process();
  assert(test_oled_writes == writes);
  DisplayApp_SetDistanceError("X");
  DisplayApp_Process();
  assert(strncmp(test_oled_rows[1], "X         ", 10U) == 0);

  tick = UINT32_MAX - 100U;
  DisplayApp_Init(1U);
  DisplayApp_Process();
  writes = test_oled_writes;
  DisplayApp_SetDistance(9U);
  tick += 499U;
  DisplayApp_Process();
  assert(test_oled_writes == writes);
  ++tick;
  DisplayApp_Process();
  assert(strncmp(test_oled_rows[1], "9 mm      ", 10U) == 0);
  puts("OLED display state and refresh tests passed");
  return 0;
}
