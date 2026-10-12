#include "board.h"
#include "FreeRTOS.h"
#include "queue.h"
#include <errno.h>

#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_pwr.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_system.h"
#include "stm32f4xx_ll_usart.h"
#include "stm32f4xx_ll_utils.h"
#include "tusb.h"

#define UART_TX_BUFFER_SIZE 512U
#define UART_TX_TIMEOUT_MS 10U
#define CLOCK_TIMEOUT_MS 100U
#define DEBUG_PINS (LL_GPIO_PIN_4 | LL_GPIO_PIN_5 | LL_GPIO_PIN_6 | LL_GPIO_PIN_7)

static uint8_t uart_tx_buffer[UART_TX_BUFFER_SIZE];
static StaticQueue_t uart_tx_queue_buffer;
static QueueHandle_t uart_tx_queue;

static void uart_init(void)
{
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART2);

    LL_GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = LL_GPIO_PIN_2;
    gpio_init.Mode = LL_GPIO_MODE_ALTERNATE;
    gpio_init.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    gpio_init.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio_init.Pull = LL_GPIO_PULL_UP;
    gpio_init.Alternate = LL_GPIO_AF_7;
    if (LL_GPIO_Init(GPIOA, &gpio_init) != SUCCESS) {
        Error_Handler();
    }

    LL_USART_InitTypeDef usart_init = {0};
    usart_init.BaudRate = 250000;
    usart_init.DataWidth = LL_USART_DATAWIDTH_8B;
    usart_init.StopBits = LL_USART_STOPBITS_1;
    usart_init.Parity = LL_USART_PARITY_NONE;
    usart_init.TransferDirection = LL_USART_DIRECTION_TX;
    usart_init.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
    usart_init.OverSampling = LL_USART_OVERSAMPLING_16;

    if (LL_USART_Init(USART2, &usart_init) != SUCCESS) {
        Error_Handler();
    }
    LL_USART_Enable(USART2);
    uart_tx_queue = xQueueCreateStatic(UART_TX_BUFFER_SIZE, sizeof(uint8_t),
                                      uart_tx_buffer, &uart_tx_queue_buffer);
    if (uart_tx_queue == NULL) {
        Error_Handler();
    }
}

int _write(int file, char *ptr, int len)
{
    (void)file;
    if (len <= 0) {
        return 0;
    }
    if (ptr == NULL) {
        errno = EINVAL;
        return -1;
    }
    /* Task-context, best-effort logs: never wait for UART or queue space. */
    if (uart_tx_queue == NULL) {
        return len;
    }
    for (int i = 0; i < len; i++) {
        if (xQueueSend(uart_tx_queue, &ptr[i], 0) != pdPASS) {
            break;
        }
    }
    /* Treat dropped logs as consumed so stdio does not retry or stop logging. */
    return len;
}

void board_uart_task(void *argument)
{
    (void)argument;

    while (1) {
        uint8_t byte;
        if (xQueueReceive(uart_tx_queue, &byte, portMAX_DELAY) != pdPASS) {
            continue;
        }

        /* Poll only in this low-priority task, with interrupts enabled. */
        uint32_t start = board_millis();
        while (!LL_USART_IsActiveFlag_TXE(USART2) &&
               (uint32_t)(board_millis() - start) < UART_TX_TIMEOUT_MS) {
        }
        if (LL_USART_IsActiveFlag_TXE(USART2)) {
            LL_USART_TransmitData8(USART2, byte);
        }
    }
}

static void wait_clock_status(uint32_t (*read_status)(void), uint32_t expected)
{
    uint32_t start = board_millis();
    while (read_status() != expected) {
        if ((uint32_t)(board_millis() - start) >= CLOCK_TIMEOUT_MS) {
            Error_Handler();
        }
    }
}

static void clock_init(void)
{
    LL_FLASH_SetLatency(LL_FLASH_LATENCY_3);
    wait_clock_status(LL_FLASH_GetLatency, LL_FLASH_LATENCY_3);
    LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);
    LL_RCC_HSE_Enable();
    wait_clock_status(LL_RCC_HSE_IsReady, 1U);

    /* 25 MHz HSE: 96 MHz CPU/timers, 48 MHz USB, 48 MHz APB1. */
    LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSE, LL_RCC_PLLM_DIV_25,
                              192, LL_RCC_PLLP_DIV_2);
    LL_RCC_PLL_ConfigDomain_48M(LL_RCC_PLLSOURCE_HSE, LL_RCC_PLLM_DIV_25,
                              192, LL_RCC_PLLQ_DIV_4);
    LL_RCC_PLL_Enable();
    wait_clock_status(LL_RCC_PLL_IsReady, 1U);
    wait_clock_status(LL_PWR_IsActiveFlag_VOS, 1U);

    LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
    LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_2);
    LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_1);
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
    wait_clock_status(LL_RCC_GetSysClkSource, LL_RCC_SYS_CLKSOURCE_STATUS_PLL);
    LL_SetSystemCoreClock(96000000);
    board_timebase_init();
}

static void usb_init(void)
{
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_OTGFS);

    LL_GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = LL_GPIO_PIN_11 | LL_GPIO_PIN_12;
    gpio_init.Mode = LL_GPIO_MODE_ALTERNATE;
    gpio_init.Pull = LL_GPIO_PULL_NO;
    gpio_init.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio_init.Alternate = LL_GPIO_AF_10;
    if (LL_GPIO_Init(GPIOA, &gpio_init) != SUCCESS) {
        Error_Handler();
    }

    NVIC_SetPriority(OTG_FS_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    /* TinyUSB owns USB controller setup, including VBUS sensing. */
}

static void gpio_init(void)
{
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);

    LL_GPIO_ResetOutputPin(GPIOA, DEBUG_PINS);
    LL_GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin        = DEBUG_PINS;
    gpio_init.Mode       = LL_GPIO_MODE_OUTPUT;
    gpio_init.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio_init.Speed      = LL_GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Pull       = LL_GPIO_PULL_NO;
    if (LL_GPIO_Init(GPIOA, &gpio_init) != SUCCESS) {
        Error_Handler();
    }
}

static inline void debug_pin(uint32_t pin, bool high)
{
    if (high) {
        LL_GPIO_SetOutputPin(GPIOA, pin);
    } else {
        LL_GPIO_ResetOutputPin(GPIOA, pin);
    }
}

void board_usb_debug_copy(bool active)
{
    debug_pin(LL_GPIO_PIN_5, active);
}

void board_usb_debug_pending(bool active)
{
    debug_pin(LL_GPIO_PIN_6, active);
}

void board_usb_debug_callback(bool active)
{
    debug_pin(LL_GPIO_PIN_7, active);
}

void board_capture_debug_wrap(bool high)
{
    debug_pin(LL_GPIO_PIN_4, high);
}

void board_init(void)
{
    NVIC_SetPriorityGrouping(3U); /* Four preemption bits, no subpriority. */
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);
    LL_FLASH_EnableInstCache();
    LL_FLASH_EnableDataCache();
    LL_FLASH_EnablePrefetch();
    LL_RCC_SetTIMPrescaler(LL_RCC_TIM_PRESCALER_TWICE);

    /* Start TIM11 on HSI so clock startup waits also have a time limit. */
    board_timebase_init();
    clock_init();
    usb_init();
    uart_init();
    gpio_init();
}

void OTG_FS_IRQHandler(void)
{
    tusb_int_handler(0, true);
}

void OTG_HS_IRQHandler(void)
{
    tusb_int_handler(1, true);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {
    }
}
