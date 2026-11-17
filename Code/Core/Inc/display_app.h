#ifndef DISPLAY_APP_H
#define DISPLAY_APP_H

#include <stdint.h>

/* Main-loop only: OLED and TOF share the same I2C bus. */
void DisplayApp_Init(uint8_t available);
void DisplayApp_SetDistance(uint32_t millimeters);
void DisplayApp_SetDistanceError(const char *text);
void DisplayApp_SetPower(uint16_t photodiode, uint16_t shunt);
void DisplayApp_Process(void);

#endif
