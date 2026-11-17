#include "oled_mock.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

char test_oled_rows[OLED_ROWS][OLED_COLS + 1];
unsigned int test_oled_writes;

void OLED_Print(uint8_t col, uint8_t row, const char *text)
{
  if (row >= OLED_ROWS || col >= OLED_COLS) return;
  ++test_oled_writes;
  size_t count = strlen(text);
  if (count > (size_t)(OLED_COLS - col)) count = (size_t)(OLED_COLS - col);
  memcpy(test_oled_rows[row] + col, text, count);
  test_oled_rows[row][OLED_COLS] = '\0';
}

void OLED_Printf(uint8_t col, uint8_t row, const char *format, ...)
{
  char text[OLED_COLS + 1];
  va_list args;
  va_start(args, format);
  (void)vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  OLED_Print(col, row, text);
}

void OLED_Printf2x(uint8_t col, uint8_t row, const char *format, ...)
{
  char text[OLED_COLS / 2 + 1];
  va_list args;
  va_start(args, format);
  (void)vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  OLED_Print(col, row, text);
}
