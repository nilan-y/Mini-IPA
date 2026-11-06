#include "usb_commands.h"
#include "main.h"

/* Single producer (USB interrupt), single consumer (main loop).
 * Up to 15 commands can wait; when full, the newest command is discarded.
 */
#define COMMAND_QUEUE_SIZE 16U
static volatile uint8_t command_queue[COMMAND_QUEUE_SIZE];
static volatile uint8_t queue_head;
static volatile uint8_t queue_tail;

static uint8_t rx_value;
static uint8_t rx_digits;
static uint8_t rx_invalid;

void USB_Commands_ResetRx(void)
{
  rx_value = 0U;
  rx_digits = 0U;
  rx_invalid = 0U;
}

void USB_Commands_Receive(const uint8_t *data, uint32_t length)
{
  for (uint32_t i = 0U; i < length; ++i)
  {
    uint8_t byte = data[i];

    if ((byte == '\r') || (byte == '\n'))
    {
      if ((rx_invalid == 0U) && (rx_digits != 0U) &&
          (rx_value >= 1U) && (rx_value <= 10U))
      {
        uint8_t next = (uint8_t)((queue_head + 1U) % COMMAND_QUEUE_SIZE);
        if (next != queue_tail)
        {
          command_queue[queue_head] = rx_value;
          queue_head = next;
        }
      }
      USB_Commands_ResetRx();
    }
    else if ((rx_invalid == 0U) && (byte >= '0') && (byte <= '9') &&
             (rx_digits < 2U))
    {
      rx_value = (uint8_t)(rx_value * 10U + (byte - '0'));
      ++rx_digits;
    }
    else
    {
      /* Discard the entire invalid line, then recover at the next delimiter. */
      rx_invalid = 1U;
    }
  }
}

uint8_t USB_Commands_Read(uint8_t *command)
{
  uint8_t available = 0U;
  uint32_t primask = __get_PRIMASK();

  /* Keep the queue read atomic, but run the command with interrupts enabled. */
  __disable_irq();
  if (queue_tail != queue_head)
  {
    *command = command_queue[queue_tail];
    queue_tail = (uint8_t)((queue_tail + 1U) % COMMAND_QUEUE_SIZE);
    available = 1U;
  }
  __set_PRIMASK(primask);
  return available;
}
