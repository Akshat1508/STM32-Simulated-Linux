# STM32 Simulated Linux

A POSIX compatibility layer running on top of FreeRTOS to simulate a Linux execution environment on severely resource-constrained microcontrollers (STM32/Cortex-M).

## Current Features
* Simulated Linux thread creation via `pthread_create` mapped to `xTaskCreate`.
* Running on QEMU ARM Cortex-M3 Emulator.
* Compiled natively on Windows using ARM GCC Toolchain.

## Project Structure
The core POSIX shim and application code is located in:
`FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main_blinky.c`
