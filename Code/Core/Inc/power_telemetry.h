#ifndef POWER_TELEMETRY_H
#define POWER_TELEMETRY_H

#include <stdint.h>

/* Main-loop only. Values are already scaled by the application. */
void PowerTelemetry_Update(uint16_t photodiode, uint16_t shunt);
void PowerTelemetry_Process(void);
void PowerTelemetry_ClearPending(void);

#endif
