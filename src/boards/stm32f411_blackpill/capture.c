#include "fx2lafw_capture.h"
#include "main.h"
#include "board.h"

#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_tim.h"
#include "stm32f4xx_ll_dma.h"

#define CAPTURE_TIM             TIM1
#define CAPTURE_DMA             DMA2
#define CAPTURE_DMA_STREAM      LL_DMA_STREAM_5
#define CAPTURE_DMA_CHANNEL     LL_DMA_CHANNEL_6
#define CAPTURE_GPIO_PORT       GPIOA
#define CAPTURE_TIM_CLOCK_HZ    96000000UL

#define CAPTURE_BUFFER_SIZE     (32U * 1024U)
#define DMA_STOP_TIMEOUT_MS     2U

_Static_assert(CAPTURE_BUFFER_SIZE <= UINT16_MAX, "DMA NDTR is 16-bit");
_Static_assert(CAPTURE_BUFFER_SIZE % FX2LAFW_CAPTURE_CHUNK_SIZE == 0U,
               "Capture chunks must not cross the end of the DMA buffer");

static uint8_t s_buffer[CAPTURE_BUFFER_SIZE] __attribute__((aligned(4)));
/* Total consumed bytes, not a ring offset. Unsigned rollover is intentional. */
static uint32_t s_consumed_bytes;
static volatile uint32_t s_dma_wraps;

void fx2lafw_capture_init(void)
{
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA2);
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_TIM1);
    __DSB();

    LL_TIM_InitTypeDef timer = {0};
    timer.CounterMode = LL_TIM_COUNTERMODE_UP;
    timer.Autoreload = UINT16_MAX;
    if (LL_TIM_Init(CAPTURE_TIM, &timer) != SUCCESS) {
        Error_Handler();
    }
    LL_TIM_EnableDMAReq_UPDATE(CAPTURE_TIM);

    /* TIM1 update samples GPIOA[7:0] into the circular byte buffer. */
    LL_DMA_InitTypeDef dma = {0};
    dma.Channel = CAPTURE_DMA_CHANNEL;
    dma.Direction = LL_DMA_DIRECTION_PERIPH_TO_MEMORY;
    dma.Mode = LL_DMA_MODE_CIRCULAR;
    dma.PeriphOrM2MSrcAddress = (uint32_t)&CAPTURE_GPIO_PORT->IDR;
    dma.MemoryOrM2MDstAddress = (uint32_t)s_buffer;
    dma.PeriphOrM2MSrcIncMode = LL_DMA_PERIPH_NOINCREMENT;
    dma.MemoryOrM2MDstIncMode = LL_DMA_MEMORY_INCREMENT;
    dma.PeriphOrM2MSrcDataSize = LL_DMA_PDATAALIGN_BYTE;
    dma.MemoryOrM2MDstDataSize = LL_DMA_MDATAALIGN_BYTE;
    dma.FIFOMode = LL_DMA_FIFOMODE_DISABLE;
    dma.NbData = CAPTURE_BUFFER_SIZE;
    dma.Priority = LL_DMA_PRIORITY_VERYHIGH;
    if (LL_DMA_Init(CAPTURE_DMA, CAPTURE_DMA_STREAM, &dma) != SUCCESS) {
        Error_Handler();
    }

    NVIC_SetPriority(DMA2_Stream5_IRQn, 1U);
    LL_DMA_DisableIT_HT(CAPTURE_DMA, CAPTURE_DMA_STREAM);
    LL_DMA_EnableIT_TC(CAPTURE_DMA, CAPTURE_DMA_STREAM);
    LL_DMA_EnableIT_TE(CAPTURE_DMA, CAPTURE_DMA_STREAM);
    NVIC_EnableIRQ(DMA2_Stream5_IRQn);
}

void fx2lafw_capture_start(uint32_t sample_rate_hz, bool sample_wide)
{
    (void)sample_wide;

    fx2lafw_capture_stop();

    if (sample_rate_hz == 0U) {
        sample_rate_hz = 1U;
    }
    uint32_t period = CAPTURE_TIM_CLOCK_HZ / sample_rate_hz;
    if (period < 1U) {
        period = 1U;
    }
    LL_TIM_SetAutoReload(CAPTURE_TIM, period - 1U);
    LL_TIM_SetCounter(CAPTURE_TIM, 0U);

    LL_TIM_GenerateEvent_UPDATE(CAPTURE_TIM);
    LL_TIM_ClearFlag_UPDATE(CAPTURE_TIM);

    /* Clear flags from the previous capture before arming DMA. */
    LL_DMA_ClearFlag_TC5(CAPTURE_DMA);
    LL_DMA_ClearFlag_HT5(CAPTURE_DMA);
    LL_DMA_ClearFlag_TE5(CAPTURE_DMA);
    LL_DMA_ClearFlag_DME5(CAPTURE_DMA);
    LL_DMA_ClearFlag_FE5(CAPTURE_DMA);

    LL_DMA_SetMemoryAddress(CAPTURE_DMA, CAPTURE_DMA_STREAM, (uint32_t)s_buffer);
    LL_DMA_SetDataLength(CAPTURE_DMA, CAPTURE_DMA_STREAM, CAPTURE_BUFFER_SIZE);

    board_capture_debug_wrap(false);
    LL_DMA_EnableStream(CAPTURE_DMA, CAPTURE_DMA_STREAM);
    LL_TIM_EnableCounter(CAPTURE_TIM);
}

void fx2lafw_capture_stop(void)
{
    LL_TIM_DisableCounter(CAPTURE_TIM);
    LL_DMA_DisableStream(CAPTURE_DMA, CAPTURE_DMA_STREAM);

    /* EN must be clear before resetting the counters or rearming DMA.
     * Called from the USB task with interrupts enabled: TIM11 keeps ticking. */
    uint32_t stop_tick = board_millis();
    while (LL_DMA_IsEnabledStream(CAPTURE_DMA, CAPTURE_DMA_STREAM)) {
        if ((uint32_t)(board_millis() - stop_tick) >= DMA_STOP_TIMEOUT_MS) {
            Error_Handler();
        }
    }

    LL_DMA_ClearFlag_TC5(CAPTURE_DMA);
    LL_DMA_ClearFlag_HT5(CAPTURE_DMA);
    s_dma_wraps = 0U;
    s_consumed_bytes = 0U;
}

static uint32_t capture_available(void)
{
    /* Keep the ISR's wrap count consistent with NDTR. DMA continues running;
     * account for a wrap whose TC interrupt has not been handled yet. */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t wraps = s_dma_wraps;
    uint32_t rem = LL_DMA_GetDataLength(CAPTURE_DMA, CAPTURE_DMA_STREAM);
    if (LL_DMA_IsActiveFlag_TC5(CAPTURE_DMA)) {
        rem = LL_DMA_GetDataLength(CAPTURE_DMA, CAPTURE_DMA_STREAM);
        ++wraps;
        if (rem == 0U) {
            rem = CAPTURE_BUFFER_SIZE;
        }
    }
    uint32_t available = wraps * CAPTURE_BUFFER_SIZE +
                         CAPTURE_BUFFER_SIZE - rem - s_consumed_bytes;
    __set_PRIMASK(primask);
    return available;
}

bool fx2lafw_capture_overrun(void)
{
    return capture_available() >= CAPTURE_BUFFER_SIZE;
}

bool fx2lafw_capture_poll_chunk(const uint8_t **out_data, size_t *out_length)
{
    uint32_t avail = capture_available();
    if (avail >= FX2LAFW_CAPTURE_CHUNK_SIZE && avail < CAPTURE_BUFFER_SIZE) {
        *out_data = &s_buffer[s_consumed_bytes % CAPTURE_BUFFER_SIZE];
        *out_length = FX2LAFW_CAPTURE_CHUNK_SIZE;

        return true;
    }

    return false;
}

void fx2lafw_capture_consume_chunk(void)
{
    s_consumed_bytes += FX2LAFW_CAPTURE_CHUNK_SIZE;
}

void DMA2_Stream5_IRQHandler(void)
{
    if (LL_DMA_IsActiveFlag_TC5(CAPTURE_DMA)) {
        LL_DMA_ClearFlag_TC5(CAPTURE_DMA);
        ++s_dma_wraps;
        /* Disabling a stream also sets TC; that is not a full buffer wrap. */
        if (LL_DMA_IsEnabledStream(CAPTURE_DMA, CAPTURE_DMA_STREAM)) {
            board_capture_debug_wrap((s_dma_wraps & 1U) != 0U);
        }
    }
    if (LL_DMA_IsActiveFlag_TE5(CAPTURE_DMA)) {
        LL_DMA_ClearFlag_TE5(CAPTURE_DMA);
    }
}
