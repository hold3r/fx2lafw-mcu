#include "board.h"
#include "fx2lafw_protocol.h"
#include "FreeRTOS.h"
#include "task.h"
#include "tusb.h"

/* 512 words for protocol/USB, including builds with USB logging enabled. */
#define PROTOCOL_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 4U)
#define UART_TASK_STACK_SIZE configMINIMAL_STACK_SIZE

static StackType_t protocol_stack[PROTOCOL_TASK_STACK_SIZE];
static StaticTask_t protocol_task_buffer;
static StackType_t uart_stack[UART_TASK_STACK_SIZE];
static StaticTask_t uart_task_buffer;

static void protocol_task(void *argument)
{
    (void)argument;

    /* USB interrupts use RTOS queues, so initialize TinyUSB after scheduling starts. */
    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    if (!tusb_init(BOARD_TUD_RHPORT, &dev_init)) {
        Error_Handler();
    }

    fx2lafw_protocol_init();

    while (1) {
        fx2lafw_protocol_task();
    }
}

int main(void)
{
    board_init();

    TaskHandle_t uart_task = xTaskCreateStatic(board_uart_task, "uart",
                                              UART_TASK_STACK_SIZE, NULL,
                                              tskIDLE_PRIORITY + 1,
                                              uart_stack, &uart_task_buffer);
    if (uart_task == NULL) {
        Error_Handler();
    }

    TaskHandle_t task = xTaskCreateStatic(protocol_task, "fx2lafw",
                                         PROTOCOL_TASK_STACK_SIZE, NULL,
                                         configMAX_PRIORITIES - 1,
                                         protocol_stack, &protocol_task_buffer);
    if (task == NULL) {
        Error_Handler();
    }

    vTaskStartScheduler();
    Error_Handler();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    Error_Handler();
}
