#ifndef SENSOR_APP_H
#define SENSOR_APP_H

#include <stdint.h>

#define SENSOR_DISTANCE_INVALID UINT16_MAX

/* Main-loop API: stream, stop, or send one fresh sample as a text line. */
void SensorApp_StartContinuous(void);
void SensorApp_Stop(void);
void SensorApp_Measure(void);
void SensorApp_Process(void);
uint8_t SensorApp_IsBusy(void);
/* Last valid distance in mm, or SENSOR_DISTANCE_INVALID before/after an error. */
uint16_t SensorApp_GetLastDistance(void);

#endif
