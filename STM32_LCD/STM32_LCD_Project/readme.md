# STM32U575ZI_NUCLEO_RVA35HI TBS

The default IDE is set to STM32CubeIDE, to change IDE open the STM32U575ZI_NUCLEO_RVA35HI.ioc with STM32CubeMX and select from the supported IDEs (EWARM, MDK-ARM and STM32CubeIDE). Supports flashing of the STM32U575ZI-NUCLEO board directly from TouchGFX Designer using GCC and STM32CubeProgrammer. Flashing the board requires STM32CubeProgrammer which can be downloaded from the ST webpage.

This TBS is configured for 240 x 320 pixels 16 bpp screen resolution.

Performance testing can be done using the GPIO pins:

- VSYNC_FREQ  - PF13 (CN10 - pin 2)
- RENDER_TIME - PF14 (CN10 - pin 8)
- FRAME_RATE  - PF15 (CN10 - pin 12)
- MCU_ACTIVE  - PG7  (CN10 - pin 14)

The USER button (B1 on STM32U575ZI-NUCLEO board, which is connected to PC13 on the MCU) is implemented in the TBS and is mapped to the button key 1.
