# STM32 Simulated Linux

A POSIX compatibility layer running on top of FreeRTOS to simulate a Linux execution environment on severely resource-constrained microcontrollers (STM32/Cortex-M).

## Features
* Simulated Linux thread creation via `pthread_create` mapped to `xTaskCreate`.
* Run and debug on QEMU ARM Cortex-M3 Emulator.
* Support for macOS compilation via the community-maintained `arm-gcc-bin@10` toolchain.

## Project Structure
The core POSIX shim and application code is located in [main_blinky.c](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main_blinky.c).

## How to Build and Run (macOS)
1. **Build the binary:**
   ```bash
   make -C FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/build/gcc clean all
   ```
2. **Execute in QEMU emulator:**
   ```bash
   qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -kernel FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/build/gcc/output/RTOSDemo.out -monitor none -nographic -serial stdio
   ```
   *(To exit QEMU, press `Ctrl + A` then release and press `X`)*

