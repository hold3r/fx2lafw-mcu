#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "board.h"
#include "board_config.h"

/* The selected board supplies the CPU clock and interrupt priority width. */
#define configCPU_CLOCK_HZ                         BOARD_CPU_CLOCK_HZ
#define configTICK_RATE_HZ                         1000
#define configUSE_PREEMPTION                       1
#define configUSE_TIME_SLICING                     0
#define configMAX_PRIORITIES                       5
#define configMINIMAL_STACK_SIZE                   128
#define configMAX_TASK_NAME_LEN                    16
#define configTICK_TYPE_WIDTH_IN_BITS              TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                    1

/* Tasks and TinyUSB queues/mutexes use fixed storage, without an RTOS heap. */
#define configSUPPORT_STATIC_ALLOCATION            1
#define configSUPPORT_DYNAMIC_ALLOCATION           0
#define configKERNEL_PROVIDED_STATIC_MEMORY        1
#define configUSE_MUTEXES                          1
#define configUSE_RECURSIVE_MUTEXES                0
#define configUSE_COUNTING_SEMAPHORES              0
#define configUSE_TIMERS                           0
#define configUSE_EVENT_GROUPS                     0
#define configUSE_STREAM_BUFFERS                  0
#define configUSE_NEWLIB_REENTRANT                 0

#define configUSE_IDLE_HOOK                        0
#define configUSE_TICK_HOOK                        0
#define configCHECK_FOR_STACK_OVERFLOW             2
#define configCHECK_HANDLER_INSTALLATION           1
#define configUSE_TRACE_FACILITY                   1

/* TinyUSB OSAL uses these task APIs. */
#define INCLUDE_vTaskSuspend                       1
#define INCLUDE_vTaskDelay                         1
#define INCLUDE_xTaskGetCurrentTaskHandle           1

/* Install the port handlers directly in the existing startup vector table. */
#define vPortSVCHandler                            SVC_Handler
#define xPortPendSVHandler                         PendSV_Handler
#define xPortSysTickHandler                        SysTick_Handler

/* The board defines which interrupt priorities may call ...FromISR APIs. */
#define configPRIO_BITS                           BOARD_NVIC_PRIO_BITS
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY    BOARD_RTOS_LOWEST_IRQ_PRIORITY
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY BOARD_RTOS_MAX_SYSCALL_IRQ_PRIORITY
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define configASSERT(condition) \
    do { if (!(condition)) { Error_Handler(); } } while (0)

#endif /* FREERTOS_CONFIG_H */
