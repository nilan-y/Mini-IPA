/* Host test: see README_USB.md for the compiler command. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "usb_commands.h"

uint32_t test_primask;

static void receive(const char *text)
{
  USB_Commands_Receive((const uint8_t *)text, (uint32_t)strlen(text));
}

static void expect(uint8_t expected)
{
  uint8_t actual = 0U;
  assert(USB_Commands_Read(&actual) == 1U);
  assert(actual == expected);
}

static void expect_empty(void)
{
  uint8_t command;
  assert(USB_Commands_Read(&command) == 0U);
}

int main(void)
{
  expect_empty();
  receive("1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n");
  for (uint8_t i = 1U; i <= 10U; ++i)
  {
    expect(i);
  }
  expect_empty();

  /* USB packet boundaries must not split command 10 into commands 1 and 0. */
  receive("1");
  expect_empty();
  receive("0");
  expect_empty();
  receive("\r");
  expect(10U);
  receive("\n2\r\n3\r4\n\n");
  expect(2U);
  expect(3U);
  expect(4U);
  expect_empty();

  /* Invalid input is ignored as a whole; the next valid line still works. */
  receive("0\n11\n99\n100\n-1\nabc\n1x\n 2\n99999999999999999\n");
  const uint8_t binary[] = {0U, '1', '\n', 0xffU, '\n'};
  USB_Commands_Receive(binary, sizeof(binary));
  expect_empty();
  receive("5\n");
  expect(5U);

  /* Reset a partial line when USB is reinitialized. */
  receive("1");
  USB_Commands_ResetRx();
  receive("0\n6\n");
  expect(6U);
  expect_empty();

  /* Wrap the queue repeatedly and verify FIFO order. */
  for (unsigned int round = 0U; round < 40U; ++round)
  {
    receive("7\n8\n9\n10\n");
    for (uint8_t i = 7U; i <= 10U; ++i)
    {
      expect(i);
    }
  }

  /* Full queue drops newest commands without corrupting pending entries. */
  for (unsigned int i = 0U; i < 15U; ++i)
  {
    receive("1\n");
  }
  receive("10\n");
  for (unsigned int i = 0U; i < 15U; ++i)
  {
    expect(1U);
  }
  expect_empty();
  receive("10\n");
  expect(10U);

  /* Read must preserve the caller's interrupt state, including on empty queue. */
  assert(test_primask == 0U);
  test_primask = 1U;
  receive("2\n");
  expect(2U);
  assert(test_primask == 1U);
  expect_empty();
  assert(test_primask == 1U);

  puts("USB command tests passed");
  return 0;
}
