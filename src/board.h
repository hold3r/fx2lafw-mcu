#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

void board_init(void);
void board_uart_task(void *argument);
void Error_Handler(void);
/* The board supplies milliseconds independently of the FreeRTOS tick. */
void board_timebase_init(void);
uint32_t board_millis(void);

/* Boards assign physical pins or provide no-op diagnostic implementations. */
void board_usb_debug_copy(bool active);
void board_usb_debug_pending(bool active);
void board_usb_debug_callback(bool active);
void board_capture_debug_wrap(bool high);

#endif
