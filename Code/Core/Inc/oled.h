/**
  * @file    oled.h
  * @brief   Text-only driver for a 128x64 SSD1315/SSD1306 OLED on STM32F042 (HAL, I2C).
  *
  * No frame buffer: characters are written straight into display RAM, so the
  * driver needs no static RAM (a few bytes of stack per call).
  *
  * Text grid: 21 columns x 8 rows (cell = 6 x 8 px). 2x text uses 2 x 2 cells.
  * Printable ASCII (32..126) is supported.
  *
  * Example:
  *   // BSP_I2C1_Init() must have been called first (shared bus with the TOF sensor)
  *   OLED_Init();
  *   OLED_Print(0, 0, "Hello");
  *   OLED_Printf(0, 2, "Temp: %3d C", temp);   // "%3d" pads, so old digits get overwritten
  *   OLED_Printf2x(0, 4, "%5lu", counter);
  */
#ifndef OLED_H
#define OLED_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ---- Configuration -------------------------------------------------------- */
#define OLED_I2C_ADDR        0x3C   /* 7-bit address: 0x3C (SA0 = GND) or 0x3D (SA0 = VCC) */
#define OLED_ROTATE_180      0      /* 1 = flip the picture upside down */

/* Bus layer: 0 = built-in HAL blocking implementation (uses OLED_I2C_HANDLE below),
 *            1 = you provide OLED_PortWrite() / OLED_PortDelayMs() (see bottom). */
#ifndef OLED_PORT_CUSTOM
#define OLED_PORT_CUSTOM     0
#endif
#define OLED_I2C_HANDLE      hi2c1  /* HAL handle from custom_bus.c; only used when OLED_PORT_CUSTOM == 0 */
#define OLED_I2C_TIMEOUT_MS  50U    /* per transaction (129 bytes @100 kHz is ~12 ms) */

/* ---- Grid ----------------------------------------------------------------- */
#define OLED_COLS            20
#define OLED_ROWS            8

/* ---- API ------------------------------------------------------------------ */
int32_t OLED_Init(void);                             /* 0 = OK, -1 = no ACK / bus error */
void    OLED_Clear(void);
void    OLED_ClearRow(uint8_t row);
void    OLED_SetContrast(uint8_t contrast);          /* 0..255 */
void    OLED_Power(uint8_t on);                      /* 1 = display on, 0 = off (RAM kept) */

/* col: 0..20, row: 0..7. Text is clipped at the right edge. */
void 	OLED_Command(uint8_t reg,uint8_t cmd);
void    OLED_Print(uint8_t col, uint8_t row, const char *str);
void    OLED_Printf(uint8_t col, uint8_t row, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

/* Double size (12 x 16 px): occupies 2 columns and 2 rows per character, row 0..6. */
void    OLED_Print2x(uint8_t col, uint8_t row, const char *str);
void    OLED_Printf2x(uint8_t col, uint8_t row, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

/* ---- Bus layer (implemented in oled.c unless OLED_PORT_CUSTOM == 1) -------- */
/* One I2C transaction: START, OLED_I2C_ADDR+W, ctrl, data[0..len-1], STOP.
 * ctrl = 0x00 -> data are commands, 0x40 -> data are display RAM. len <= 129. Returns 0 / -1. */
int32_t OLED_PortWrite(uint8_t ctrl, const uint8_t *data, uint16_t len);
void    OLED_PortDelayMs(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* OLED_H */
