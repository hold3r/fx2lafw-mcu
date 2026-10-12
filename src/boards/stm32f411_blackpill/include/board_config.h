#ifndef FX2LAFW_BOARD_CONFIG_H
#define FX2LAFW_BOARD_CONFIG_H

#include "stm32f4xx.h"

#define BOARD_CPU_CLOCK_HZ SystemCoreClock
#define BOARD_NVIC_PRIO_BITS __NVIC_PRIO_BITS
#define BOARD_RTOS_LOWEST_IRQ_PRIORITY 15
#define BOARD_RTOS_MAX_SYSCALL_IRQ_PRIORITY 5

#define BOARD_TUD_RHPORT 0
#define BOARD_TUD_MAX_SPEED OPT_MODE_FULL_SPEED

/* Two 64-byte packets in the DWC2 hardware FIFO for bulk IN endpoint 2. */
#define CFG_TUD_CONFIGURE_DWC2_DEFAULT \
    { .bm_double_buffered = (1u << 2), \
      .vbus_sensing = CFG_TUD_VBUS_DETECT_HW }

#endif /* FX2LAFW_BOARD_CONFIG_H */
