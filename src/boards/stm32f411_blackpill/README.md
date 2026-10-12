# STM32F411 Black Pill

Port for the STM32F411CEU6 with a 25 MHz HSE.

| Component | Current configuration |
|---|---|
| CPU / TIM1 | 96 MHz |
| USB | OTG FS, PA11/PA12, 48 MHz |
| Capture | TIM1 UPDATE → DMA2 Stream5 Channel6 → low byte of GPIOA.IDR |
| DMA ring | 32 KiB, 1024-byte blocks |
| UART | USART2 TX on PA2, 250000 baud, 8N1 |
| Millisecond time base | TIM11; SysTick is owned by FreeRTOS |
| DMA IRQ | Priority 1, no FreeRTOS calls |
| USB IRQ | Priority 5, FreeRTOS event queue |
| Memory | Flash 512 KiB, RAM 128 KiB |

Diagnostics use PA4–PA7:

- PA4 toggles after each complete DMA ring wrap.
- PA5 is high while writing a block to TinyUSB.
- PA6 is high while bytes accepted by TinyUSB have not yet been accounted for by a TX completion callback.
- PA7 is high inside our `tud_vendor_tx_cb()`.

These GPIOs are included in the captured GPIOA[7:0] byte. UART output on PA2 is also captured.
The backend captures 8-bit samples even when the 16-bit mode flag is requested.

`include/board_config.h` contains the FreeRTOS/TinyUSB integration settings.
`board.c` configures hardware resources, and `capture.c` implements the capture interface.
Existing STM32Cube files and their user sections are preserved.
