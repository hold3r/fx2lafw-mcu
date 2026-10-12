# STM32F411CEU6 Black Pill: hardware sources and all target-specific build settings.
# Share the board's include paths, definitions, and CPU flags with other targets.
add_library(fx2lafw_board INTERFACE)
target_include_directories(fx2lafw_board INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/lib/STM32F4xx_HAL_Driver/Inc"
    "${PROJECT_SOURCE_DIR}/lib/STM32F4xx_HAL_Driver/Inc/Legacy"
    "${PROJECT_SOURCE_DIR}/lib/CMSIS/Device/ST/STM32F4xx/Include"
    "${PROJECT_SOURCE_DIR}/lib/CMSIS/Include"
)
target_compile_definitions(fx2lafw_board INTERFACE
    USE_FULL_LL_DRIVER
    USE_HAL_DRIVER
    STM32F411xE
    CFG_TUSB_MCU=OPT_MCU_STM32F4
)
target_compile_options(fx2lafw_board INTERFACE
    -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard
)
target_link_options(fx2lafw_board INTERFACE
    -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard
    "-T${CMAKE_CURRENT_SOURCE_DIR}/STM32F411xx_FLASH.ld"
)
set(FREERTOS_PORT GCC_ARM_CM4F CACHE STRING "FreeRTOS port selected by the board" FORCE)

# Board implementation and USB controller driver.
target_sources(${PROJECT_NAME} PRIVATE
    board.c
    capture.c
    stm32f4xx_it.c
    stm32f4xx_hal_msp.c
    stm32f4xx_hal_timebase_tim.c
    startup_stm32f411xe.s
    "${PROJECT_SOURCE_DIR}/lib/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c"
    "${PROJECT_SOURCE_DIR}/lib/tinyusb/src/portable/synopsys/dwc2/dwc2_common.c"
)
set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/STM32F411xx_FLASH.ld"
)

# HAL/LL driver sources. Keep the list explicit so dependencies are easy to edit.
set(stm32_driver_directory "${PROJECT_SOURCE_DIR}/lib/STM32F4xx_HAL_Driver/Src")
add_library(stm32_drivers OBJECT
    system_stm32f4xx.c
    "${stm32_driver_directory}/stm32f4xx_hal_tim.c"
    "${stm32_driver_directory}/stm32f4xx_hal_tim_ex.c"
    "${stm32_driver_directory}/stm32f4xx_ll_gpio.c"
    "${stm32_driver_directory}/stm32f4xx_ll_usart.c"
    "${stm32_driver_directory}/stm32f4xx_ll_dma.c"
    "${stm32_driver_directory}/stm32f4xx_ll_tim.c"
    "${stm32_driver_directory}/stm32f4xx_ll_rcc.c"
    "${stm32_driver_directory}/stm32f4xx_ll_utils.c"
    "${stm32_driver_directory}/stm32f4xx_ll_exti.c"
    "${stm32_driver_directory}/stm32f4xx_hal_rcc.c"
    "${stm32_driver_directory}/stm32f4xx_hal_rcc_ex.c"
    "${stm32_driver_directory}/stm32f4xx_hal_flash.c"
    "${stm32_driver_directory}/stm32f4xx_hal_flash_ex.c"
    "${stm32_driver_directory}/stm32f4xx_hal_flash_ramfunc.c"
    "${stm32_driver_directory}/stm32f4xx_hal_gpio.c"
    "${stm32_driver_directory}/stm32f4xx_hal_dma_ex.c"
    "${stm32_driver_directory}/stm32f4xx_hal_dma.c"
    "${stm32_driver_directory}/stm32f4xx_hal_pwr.c"
    "${stm32_driver_directory}/stm32f4xx_hal_pwr_ex.c"
    "${stm32_driver_directory}/stm32f4xx_hal_cortex.c"
    "${stm32_driver_directory}/stm32f4xx_hal.c"
    "${stm32_driver_directory}/stm32f4xx_hal_exti.c"
    "${stm32_driver_directory}/stm32f4xx_hal_pcd.c"
    "${stm32_driver_directory}/stm32f4xx_hal_pcd_ex.c"
    "${stm32_driver_directory}/stm32f4xx_ll_usb.c"
)
target_link_libraries(stm32_drivers PUBLIC fx2lafw_board)
target_compile_definitions(stm32_drivers PRIVATE $<$<CONFIG:Debug>:DEBUG>)
target_link_libraries(${PROJECT_NAME} PRIVATE stm32_drivers)
