#ifndef USB_COMMANDS_H
#define USB_COMMANDS_H

#include <stdint.h>

/* USB interrupt context: receive ASCII commands 1..10 terminated by CR or LF. */
void USB_Commands_Receive(const uint8_t *data, uint32_t length);
void USB_Commands_ResetRx(void);

/* Main loop: returns 1 and the next command, or 0 when the queue is empty. */
uint8_t USB_Commands_Read(uint8_t *command);

#endif
