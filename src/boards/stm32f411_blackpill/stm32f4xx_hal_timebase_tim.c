/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f4xx_hal_timebase_tim.c
  * @brief   HAL time base based on the hardware TIM.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_tim.h"
#include "board.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

static ErrorStatus timebase_init(uint32_t priority)
{
  if (priority >= (1UL << __NVIC_PRIO_BITS))
  {
    return ERROR;
  }

  LL_RCC_ClocksTypeDef clocks;
  LL_RCC_GetSystemClocksFreq(&clocks);
  uint32_t timer_clock = clocks.PCLK2_Frequency;
  /* board_init() selects TIMPRE=0: divided APB2 clocks are doubled for TIM11. */
  if (LL_RCC_GetAPB2Prescaler() != LL_RCC_APB2_DIV_1)
  {
    timer_clock *= 2U;
  }
  if (timer_clock < 1000000U || timer_clock % 1000000U != 0U)
  {
    return ERROR;
  }

  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_TIM11);
  LL_TIM_DisableIT_UPDATE(TIM11);
  LL_TIM_DisableCounter(TIM11);
  LL_TIM_SetPrescaler(TIM11, timer_clock / 1000000U - 1U);
  LL_TIM_SetAutoReload(TIM11, 999U);
  LL_TIM_SetCounter(TIM11, 0U);
  LL_TIM_GenerateEvent_UPDATE(TIM11);
  LL_TIM_ClearFlag_UPDATE(TIM11);

  uwTickPrio = priority;
  NVIC_SetPriority(TIM1_TRG_COM_TIM11_IRQn, priority);
  NVIC_ClearPendingIRQ(TIM1_TRG_COM_TIM11_IRQn);
  NVIC_EnableIRQ(TIM1_TRG_COM_TIM11_IRQn);
  LL_TIM_EnableIT_UPDATE(TIM11);
  LL_TIM_EnableCounter(TIM11);
  return SUCCESS;
}

void board_timebase_init(void)
{
  if (timebase_init(TICK_INT_PRIORITY) != SUCCESS)
  {
    Error_Handler();
  }
}

uint32_t board_millis(void)
{
  return uwTick;
}

/**
  * @brief  This function configures the TIM11 as a time base source.
  *         The time source is configured  to have 1ms time base with a dedicated
  *         Tick interrupt priority.
  * @note   This function is called  automatically at the beginning of program after
  *         reset by HAL_Init() or at any time when clock is configured, by HAL_RCC_ClockConfig().
  * @param  TickPriority: Tick interrupt priority.
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
  /* Compatibility for HAL_GetTick() users; all TIM11 setup is LL. */
  return timebase_init(TickPriority) == SUCCESS ? HAL_OK : HAL_ERROR;
}

/**
  * @brief  Suspend Tick increment.
  * @note   Disable the tick increment by disabling TIM11 update interrupt.
  * @param  None
  * @retval None
  */
void HAL_SuspendTick(void)
{
  /* Disable TIM11 update Interrupt */
  LL_TIM_DisableIT_UPDATE(TIM11);
}

/**
  * @brief  Resume Tick increment.
  * @note   Enable the tick increment by Enabling TIM11 update interrupt.
  * @param  None
  * @retval None
  */
void HAL_ResumeTick(void)
{
  /* Enable TIM11 Update interrupt */
  LL_TIM_EnableIT_UPDATE(TIM11);
}

