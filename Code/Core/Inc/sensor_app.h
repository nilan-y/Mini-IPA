#ifndef SENSOR_APP_H
#define SENSOR_APP_H

#include <stdint.h>

/* Main-loop API: start one measurement and send its result as a text line. */
void SensorApp_Measure(void);
void SensorApp_Process(void);
uint8_t SensorApp_IsBusy(void);

#endif
