# Project Gantt Chart: STM32 Simulated Linux

This document presents a 6-week Gantt chart and project timeline detailing the development of the **STM32 Simulated Linux** compatibility layer on top of FreeRTOS running in QEMU.

The timeline spans **01-06-2026 to 12-07-2026**, organized into 6 distinct phases covering literature research, architectural design, QEMU/FreeRTOS environment setup, POSIX thread shims, documentation, networking stack integration, and the POSIX socket web server demonstration.

---

## 1. Gantt Chart Diagram

![Gantt Chart](gantt_chart.png)

---

## 2. Master Schedule Table

| Task ID | Phase | Full Task Name | Key Outputs / File References |
| :---: | :--- | :--- | :--- |
| **t0** | Phase 1 | Literature Research | Survey of POSIX RTOS shimming & Cortex-M memory bounds |
| **t1** | Phase 1 | Research & Feasibility Study | Cortex-M3 RAM budget & POSIX shim evaluation |
| **t2** | Phase 1 | Feasibility Report & Heatmap Creation | Feasibility report & compatibility heatmap creation |
| **t3** | Phase 2 | Initial QEMU & FreeRTOS Environment Setup | FreeRTOS Kernel & QEMU MPS2 target setup |
| **t4** | Phase 2 | Memory Layout & UART Console Redirection | Linker script `mps2_m3.ld` & UART bindings in [`main.c`](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main.c) |
| **t5** | Phase 3 | Basic POSIX Thread Translation Shim | `pthread_create` mapping to FreeRTOS `xTaskCreate` |
| **t6** | Phase 3 | Build Fixes & Type Casting Debug | Toolchain cross-compilation fix & Makefile adjustments |
| **t7** | Phase 3 | System Overview & Architecture Overview | Architectural documentation in [`SYSTEM_OVERVIEW.md`](SYSTEM_OVERVIEW.md) |
| **t8** | Phase 4 | Thread Lifecycle (`pthread_join`, `exit`, `detach`, `self`) | Thread registry & lifecycle management in [`main_blinky.c`](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main_blinky.c) |
| **t9** | Phase 4 | Mutex & Counting Semaphore Synchronization | `pthread_mutex_t` & custom counting semaphore `sem_t` |
| **t10** | Phase 4 | Timing Primitives (`sleep`, `usleep`) | Timing mapping to `vTaskDelay` scheduler ticks |
| **t11** | Phase 5 | LwIP TCP/IP Stack Integration | LwIP OS layer adaptation in [`sys_arch.c`](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/sys_arch.c) |
| **t12** | Phase 5 | SMSC9118 Ethernet Driver & NVIC Interrupts | Hardware Ethernet driver in [`ethernetif.c`](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/ethernetif.c) |
| **t13** | Phase 6 | POSIX Socket Shim Wrapper Layers | BSD Socket APIs (`socket`, `bind`, `listen`, `accept`) |
| **t14** | Phase 6 | HTTP Web Server Demo Application | Simulated POSIX Web Server listening on port 80/8080 |

---

## 3. Detailed Week-by-Week Milestone Breakdown

### Phase 1: Design, Heatmap & Feasibility Report (01-06 to 12-06-2026)
* **Focus**: Conducting literature research, preliminary architectural design, evaluating Cortex-M micro-kernel limits, and publishing feasibility reports.
* **Key Tasks**:
  * Performed literature research on POSIX API shimming over real-time operating systems.
  * Evaluated memory constraints and RAM budget (~70-80 KB) of the target board.
  * Mapped API conversion strategies, priority scaling, and stack depth allocations for simulating Linux threads inside FreeRTOS.
  * Created the methodology evaluation heatmap comparing POSIX Shimming vs. Manual Rewriting and Full Emulation.
  * Published initial project guidelines and repository setup documentation in [README.md](README.md).

### Phase 2: QEMU Setup & FreeRTOS Installation (11-06 to 19-06-2026)
* **Focus**: Provisioning the build toolchain, importing FreeRTOS real-time kernel, and configuring QEMU emulation.
* **Key Tasks**:
  * Imported the FreeRTOS real-time kernel and template configuration for the Cortex-M3 MPS2 AN385 platform.
  * Configured linker script `mps2_m3.ld` defining memory layouts for FLASH (`4096K`) and SRAM (`8192K`).
  * Overrode CPU standard output bindings in [main.c](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main.c) to pipe debug prints directly to the QEMU terminal window via UART0 registers (`0x40004000UL`).

### Phase 3: POSIX Thread Shim, Build Fixes & System Overview (22-06 to 28-06-2026)
* **Focus**: Developing the core `pthread_create` translation shim, fixing build environment issues, and documenting architecture.
* **Key Tasks**:
  * Designed the initial lightweight `pthread_create` translation mapping POSIX thread requests directly to FreeRTOS `xTaskCreate` calls.
  * Fixed type-casting constraints inside `pthread_create` and updated Makefile compiler flags to resolve cross-platform build errors.
  * Created [SYSTEM_OVERVIEW.md](SYSTEM_OVERVIEW.md) as a comprehensive codebase map of execution pathways and directory structures.

### Phase 4: Core POSIX Shim Layers (29-06 to 05-07-2026)
* **Focus**: Implementing thread lifecycle management, synchronization locks, and timing primitives.
* **Key Tasks**:
  * Implemented thread lifecycle control functions in [main_blinky.c](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main_blinky.c) (`pthread_join`, `pthread_exit`, `pthread_detach`, `pthread_self`).
  * Integrated Mutual Exclusion locks (`pthread_mutex_t`) mapping directly to FreeRTOS Mutex primitives (`xSemaphoreCreateMutex`).
  * Developed a counting semaphore library (`sem_t`) to support inter-thread signaling without native Unix headers.
  * Mapped POSIX timing delays (`sleep`, `usleep`) to FreeRTOS scheduler ticks (`vTaskDelay`).

### Phase 5: LwIP Stack & Driver (06-07 to 10-07-2026)
* **Focus**: Integrating LwIP TCP/IP stack and coding hardware network interface drivers.
* **Key Tasks**:
  * Integrated LwIP source files with custom memory parameters configured in [lwipopts.h](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/lwipopts.h).
  * Built the OS adaptation layer [sys_arch.c](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/sys_arch.c) mapping LwIP threads/queues to FreeRTOS.
  * Programmed the SMSC9118 Ethernet hardware driver in [ethernetif.c](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/ethernetif.c) to handle physical frames.
  * Configured interrupt service routines (NVIC IRQ 13) to process incoming packet queues asynchronously via a dedicated task.

### Phase 6: Sockets & Web Application (09-07 to 12-07-2026)
* **Focus**: Implementing BSD socket APIs and demonstrating the simulated POSIX web server.
* **Key Tasks**:
  * Mapped standard Linux BSD sockets (`socket`, `bind`, `listen`, `accept`, `read`, `write`, `close`) to LwIP's built-in socket API.
  * Developed a simulated POSIX HTTP Web Server daemon inside [main_blinky.c](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main_blinky.c) listening on virtual port 80 (forwarded to host port 8080) responding to HTTP GET requests.
