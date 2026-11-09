#ifndef SENSOR_APP_H
#define SENSOR_APP_H

#include <stdint.h>

/* Main-loop API: stream, stop, or send one fresh sample as a text line. */
void SensorApp_StartContinuous(void);
void SensorApp_Stop(void);
void SensorApp_Measure(void);
void SensorApp_Process(void);
uint8_t SensorApp_IsBusy(void);

#endif
