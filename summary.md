# STM32 Simulated Linux: Comprehensive System Architecture, Implementation & History Report

---

## 1. Executive Summary

The **STM32 Simulated Linux** project establishes a lightweight, high-performance **POSIX Compatibility Shim Layer** on top of the **FreeRTOS** real-time kernel and **LwIP** TCP/IP network stack. Targeted for resource-constrained ARM Cortex-M microcontrollers (specifically emulated in **QEMU MPS2-AN385 / Cortex-M3**), the project enables standard multi-threaded Linux C applications, POSIX synchronization routines, and BSD socket server daemons to execute on bare-metal hardware without requiring a Hardware Memory Management Unit (MMU) or heavy Linux kernel image.

```
+-----------------------------------------------------------------------------------+
|               Simulated Linux Applications (C Daemons / HTTP Server)              |
|        pthread_create()  |  pthread_mutex_lock()  |  sem_wait()  |  socket()      |
+-----------------------------------------------------------------------------------+
                                         |
                                         v
+-----------------------------------------------------------------------------------+
|               POSIX Compatibility Layer (posix_shim.c / posix_shim.h)             |
|          Thread Registry (g_thread_list), Priority Scaling, Delay Scaling         |
+-----------------------------------------------------------------------------------+
                     |                                           |
                     v                                           v
+------------------------------------------+ +--------------------------------------+
|       FreeRTOS Kernel (V202212.00)       | |        LwIP TCP/IP Stack 1.4.0       |
| Tasks, Mutexes, Semaphores, Tick Delayed | | (OS Mode: sys_arch.c, ethernetif.c) |
+------------------------------------------+ +--------------------------------------+
                     |                                           |
                     +---------------------+---------------------+
                                           |
                                           v
+-----------------------------------------------------------------------------------+
|                Hardware Emulation: ARM Cortex-M3 MPS2-AN385 (QEMU)                |
|      UART0 (0x40004000UL) | LAN9118 Ethernet (IRQ 13) | SysTick (1000 Hz)        |
+-----------------------------------------------------------------------------------+
```

### Key Technical Achievements:
- **Zero-MMU Linux Emulation**: Standard POSIX applications compile and execute directly on bare-metal microcontrollers without virtual memory translation or OS process bloat.
- **Complete Thread Lifecycle Management**: Fully functional `pthread_create`, `pthread_join`, `pthread_exit`, `pthread_detach`, and `pthread_self` backed by a thread-safe dynamic registry (`g_thread_list`).
- **Priority-Inherited Mutexes & Semaphores**: POSIX mutex locks (`pthread_mutex_*`) featuring priority inheritance to prevent priority inversion, alongside custom counting semaphores (`sem_*`).
- **Deterministic Delays**: High-precision delay translation (`sleep`, `usleep`) to FreeRTOS scheduler ticks (`vTaskDelay`).
- **BSD Socket Networking**: Seamless BSD socket API (`socket`, `bind`, `listen`, `accept`, `read`, `write`, `close`) backed by LwIP in multithreaded OS mode (`NO_SYS = 0`).
- **Interrupt-Driven Network Hardware Driver**: Custom SMSC9118 (LAN9118) Ethernet driver (`ethernetif.c`) processing incoming frames via Cortex-M NVIC IRQ 13 interrupts.
- **Embedded Web Server**: Live POSIX HTTP server listening on TCP port 80 (forwarded to host port 8080 in QEMU), responding to standard HTTP GET requests.
- **Ultra-Compact Footprint**: Compiled code size (`text`) is **~68.9 KB**, fitting within standard microcontroller on-chip Flash limits (128 KB - 1 MB).

---

## 2. Directory Structure & Codebase Map

```
STM32-Simulated-Linux/
├── FreeRTOS/
│   ├── Source/                                  # Core FreeRTOS kernel source code
│   │   ├── tasks.c                              # Task management, scheduler & TCBs
│   │   ├── queue.c                              # Ring-buffer queues & semaphores
│   │   ├── timers.c                             # Software timer service daemon
│   │   ├── list.c                               # Scheduler priority lists
│   │   ├── portable/
│   │   │   ├── GCC/ARM_CM3/port.c               # Cortex-M3 SysTick, PendSV & SVC ports
│   │   │   └── MemMang/heap_4.c                 # First-fit heap allocator with coalescing
│   └── Demo/
│       ├── Common/ethernet/lwip-1.4.0/          # LwIP 1.4.0 TCP/IP stack tree
│       └── CORTEX_MPS2_QEMU_IAR_GCC/            # Target application & platform configuration
│           ├── posix_shim.h                     # Public POSIX shim API declarations
│           ├── posix_shim.c                     # POSIX thread, mutex, semaphore & timing shim
│           ├── main.c                           # Hardware init, UART0 redirect, hook callbacks
│           ├── main_blinky.c                    # Worker concurrency demo & HTTP web server
│           ├── sys_arch.c                       # LwIP OS layer (FreeRTOS mbox/semaphore map)
│           ├── ethernetif.c                     # SMSC9118 Ethernet MAC/PHY driver (IRQ 13)
│           ├── lwipopts.h                       # LwIP configuration (NO_SYS=0, sockets=1)
│           ├── FreeRTOSConfig.h                 # Kernel tick rate, CPU clock & heap limits
│           └── build/gcc/
│               ├── Makefile                     # Cross-compilation makefile
│               ├── mps2_m3.ld                   # Linker script (Flash: 4096K, SRAM: 8192K)
│               └── startup_gcc.c                # Vector table, reset handler & IRQ mappings
├── FreeRTOS-Plus/                               # Percepio TraceRecorder / Tracealyzer port
├── misc/
│   ├── SYSTEM_OVERVIEW.md                       # Architectural overview & directory map
│   ├── MAIN_BLINKY_AND_NETWORKING_GUIDE.md      # Function-by-function implementation guide
│   ├── ultra_detailed_codebase_documentation_report.md # Deep technical architecture report
│   ├── generate_pdf.py                          # ReportLab script for GANTT_CHART.pdf
│   └── generate_results_pdf.py                  # ReportLab script for results.pdf
├── API_TRANSLATION_ROADMAP.md                   # 22 active APIs, 4-tier model & 28 roadmap APIs
├── results.md                                   # Verification logs, test matrices & RAM metrics
├── results.pdf                                  # Formatted benchmark PDF report
├── gantt_chart.md                               # 6-phase development timeline
├── GANTT_CHART.pdf                              # Visual timeline PDF
├── gantt_chart.png                              # Rendered Gantt chart image
└── README.md                                    # Project overview, quickstart & execution guide
```

---

## 3. Deep-Dive Technical Implementation

### 3.1 Hardware Initialization & Console Output Redirection
1. **Boot Vector ([`startup_gcc.c`](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/build/gcc/startup_gcc.c))**: 
   - `isr_vector[]` places the initial stack pointer (`&_estack`) and `Reset_Handler` at Flash origin `0x00000000`.
   - Binds NVIC IRQ position 13 directly to `EthernetISR`.
2. **UART0 Redirection ([`main.c`](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main.c))**:
   - `prvUARTInit()` sets the MPS2 UART0 baud divider (`UART0_BAUDDIV = 16` at address `0x40004010UL`) and enables TX (`UART0_CTRL = 1`).
   - Overrides standard C low-level `__write()` / `_uart_putc()` functions to feed output characters directly into `UART0_DATA` (`0x40004000UL`). All standard `printf()` outputs stream cleanly to the QEMU terminal window (`-serial stdio`).
3. **Execution Dispatch**:
   - `main()` dispatches execution to `main_blinky()`, which initializes application threads and starts the FreeRTOS preemptive scheduler via `vTaskStartScheduler()`.

---

### 3.2 POSIX Thread Management Layer ([`posix_shim.c`](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c))

Each POSIX thread is tracked by a dynamically allocated registry node:

```c
typedef struct posix_thread {
    TaskHandle_t       xTask;          /* Native FreeRTOS task handle */
    void              *(*start_routine)(void *); /* POSIX entry function */
    void              *arg;            /* Parameter to start_routine */
    void              *retval;         /* Exit status from pthread_exit() */
    SemaphoreHandle_t  join_sem;       /* Binary semaphore for pthread_join() */
    volatile int       detached;       /* 1 if thread is detached */
    struct posix_thread *next;         /* Linked list pointer */
} posix_thread_t;
```

#### Detailed Lifecycle Mechanics:
- **`pthread_create(thread, attr, start_routine, arg)`**:
  - Dynamically allocates a `posix_thread_t` node from `heap_4.c` (`pvPortMalloc`).
  - Initializes `join_sem` as a binary semaphore (`xSemaphoreCreateBinary`).
  - Temporarily suspends the FreeRTOS scheduler (`vTaskSuspendAll()`) to eliminate race conditions during task registration.
  - Calls `xTaskCreate(posix_thread_wrapper, "POSIX_Thread", 1024, t, tskIDLE_PRIORITY + 1, &t->xTask)`.
  - Appends node `t` to `g_thread_list` inside a priority-mask critical section (`taskENTER_CRITICAL()`).
  - Resumes the scheduler (`xTaskResumeAll()`) and assigns `*thread = (pthread_t)t`.
- **`posix_thread_wrapper(pvParameters)`**:
  - FreeRTOS task wrapper executing on the task's private Process Stack Pointer (`PSP`).
  - Executes `void *retval = t->start_routine(t->arg)`.
  - Automatically calls `pthread_exit(retval)` upon routine completion.
- **`pthread_join(thread, retval)`**:
  - Blocks the caller by executing `xSemaphoreTake(t->join_sem, portMAX_DELAY)`.
  - Upon unblocking, extracts `t->retval`, unlinks `t` from `g_thread_list`, deletes `join_sem`, and frees memory via `vPortFree(t)`.
- **`pthread_exit(retval)`**:
  - Locates the calling thread node via `find_thread(xTaskGetCurrentTaskHandle())`.
  - If `detached == 0`: Stores `retval` and signals `xSemaphoreGive(t->join_sem)`.
  - If `detached == 1`: Immediately frees all metadata and unlinks from the registry.
  - Destroys the active task via `vTaskDelete(NULL)`.
- **`pthread_detach(thread)`**:
  - Sets `t->detached = 1`, instructing `pthread_exit()` to auto-reclaim memory without requiring another thread to join.
- **`pthread_self()`**:
  - Traverses `g_thread_list` inside a critical section to find the thread node corresponding to `xTaskGetCurrentTaskHandle()`.

---

### 3.3 Synchronization & Timing Shim ([`posix_shim.c`](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c))

- **POSIX Mutexes (`pthread_mutex_*`)**:
  - `pthread_mutex_t` stores a FreeRTOS `SemaphoreHandle_t` initialized via `xSemaphoreCreateMutex()`.
  - `pthread_mutex_lock()` and `pthread_mutex_unlock()` map directly to `xSemaphoreTake(..., portMAX_DELAY)` and `xSemaphoreGive(...)`.
  - **Priority Inheritance**: The kernel dynamically boosts the priority of any task holding a mutex if a higher-priority task contends for the lock, completely eliminating **Priority Inversion**.
- **POSIX Counting Semaphores (`sem_*`)**:
  - Defined as opaque pointer `sem_t`, wrapping `xSemaphoreCreateCounting(65535, initial_value)`.
  - `sem_wait()` decrements the count or blocks indefinitely if count is 0.
  - `sem_post()` increments the count and unblocks the highest-priority waiting task.
- **Timing & Delays (`sleep`, `usleep`)**:
  - `sleep(seconds)` converts seconds to ticks (`pdMS_TO_TICKS(seconds * 1000UL)`) and calls `vTaskDelay()`.
  - `usleep(useconds)` calculates tick counts and guarantees at least a 1-tick delay for any non-zero microsecond delay.
  - Blocked tasks are placed in the kernel's `xDelayedTaskList`, consuming zero CPU cycles while sleeping.

---

### 3.4 Networking Stack & Ethernet Driver Subsystems

```
[ Physical / QEMU Ethernet Frame ]
               |
               v
 [ LAN9118 Hardware FIFO Registers (0x40000000) ]
               |
               v (Asserts NVIC IRQ 13)
 [ EthernetISR() in ethernetif.c ]
               |
               v (vTaskNotifyGiveFromISR)
 [ LWIP_RX Task (Priority 5, ethernetif_input_task) ]
               |
               v (pbuf_alloc + smsc9220_receive_by_chunks)
 [ netif->input() -> etharp_input() -> ip4_input() -> tcp_input() ]
               |
               v (sys_mbox_post to socket mailbox)
 [ web_server_thread unblocks from accept() / read() ]
```

1. **LwIP OS Adaptation ([`sys_arch.c`](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/sys_arch.c))**:
   - Implements thread-safe mailboxes (`sys_mbox_t`) using FreeRTOS Queues (`xQueueCreate`, `xQueueSendToBack`, `xQueueReceive`).
   - Maps LwIP semaphores and mutexes to FreeRTOS primitives.
   - Maps `sys_thread_new()` to FreeRTOS `xTaskCreate()`.
2. **SMSC9118 Ethernet Driver ([`ethernetif.c`](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/ethernetif.c))**:
   - `low_level_init()` configures MAC `00:08:29:11:22:33`, MTU 1500, sets NVIC priority for IRQ 13, and spawns high-priority `LWIP_RX` task.
   - `EthernetISR()` clears the Rx interrupt, disables hardware FIFO interrupts, notifies `LWIP_RX` via `vTaskNotifyGiveFromISR()`, and requests a fast context switch via `portYIELD_FROM_ISR()`.
   - `ethernetif_input_task()` receives notifications via `ulTaskNotifyTake()`, allocates zero-copy `pbuf` memory, pulls packet chunks from FIFO, and feeds them to `netif->input()`.
3. **BSD Socket Web Server ([`main_blinky.c`](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main_blinky.c))**:
   - Initializes LwIP stack (`tcpip_init`) with static IP `10.0.2.15`.
   - Uses standard BSD socket API: `socket(AF_INET, SOCK_STREAM, 0)`, `bind()` to port 80, `listen(5)`, and `accept()`.
   - Parses HTTP GET requests and returns HTTP/1.1 200 OK responses.

---

## 4. Complete List of 22 Implemented APIs

| Category | POSIX API | Underlying FreeRTOS / LwIP Primitive | Function Description & Mechanics |
| :--- | :--- | :--- | :--- |
| **Threads** | `pthread_create()` | `pvPortMalloc` + `xTaskCreate` + `xSemaphoreCreateBinary` | Allocates thread struct, join semaphore, spawns task, and registers in `g_thread_list`. |
| **Threads** | `pthread_join()` | `xSemaphoreTake(t->join_sem)` + `vPortFree` | Blocks until target thread terminates, extracts `retval`, unlinks, and frees memory. |
| **Threads** | `pthread_exit()` | `xSemaphoreGive(t->join_sem)` + `vTaskDelete` | Signals joiners, cleans up if detached, and deletes calling FreeRTOS task. |
| **Threads** | `pthread_detach()` | Direct flag mutation (`t->detached = 1`) | Marks thread for automatic reclamation upon exit without joining. |
| **Threads** | `pthread_self()` | `xTaskGetCurrentTaskHandle()` + `find_thread()` | Returns the `posix_thread_t *` handle of the current executing task. |
| **Mutex** | `pthread_mutex_init()` | `xSemaphoreCreateMutex()` | Creates a mutex semaphore with priority inheritance. |
| **Mutex** | `pthread_mutex_lock()` | `xSemaphoreTake(*mutex, portMAX_DELAY)` | Locks mutex, blocking indefinitely if contended. |
| **Mutex** | `pthread_mutex_unlock()`| `xSemaphoreGive(*mutex)` | Releases ownership of the mutex lock. |
| **Mutex** | `pthread_mutex_destroy()`| `vSemaphoreDelete(*mutex)` | Deletes mutex semaphore and frees kernel memory. |
| **Semaphore** | `sem_init()` | `xSemaphoreCreateCounting(65535, val)` | Initializes counting semaphore with maximum count 65535. |
| **Semaphore** | `sem_wait()` | `xSemaphoreTake(*sem, portMAX_DELAY)` | Decrements semaphore count or blocks if count is 0. |
| **Semaphore** | `sem_post()` | `xSemaphoreGive(*sem)` | Increments semaphore count, unblocking waiting tasks. |
| **Semaphore** | `sem_destroy()` | `vSemaphoreDelete(*sem)` | Deletes counting semaphore handle. |
| **Timing** | `sleep()` | `vTaskDelay(pdMS_TO_TICKS(s * 1000))` | Suspends task for specified seconds. |
| **Timing** | `usleep()` | `vTaskDelay(ticks)` | Suspends task for specified microseconds (guarantees $\ge 1$ tick). |
| **Networking** | `socket()` | `lwip_socket()` | Allocates an LwIP socket descriptor. |
| **Networking** | `bind()` | `lwip_bind()` | Binds socket descriptor to IP address and port. |
| **Networking** | `listen()` | `lwip_listen()` | Places socket into TCP server listening mode. |
| **Networking** | `accept()` | `lwip_accept()` | Blocks until incoming TCP connection is established. |
| **Networking** | `read()` | `lwip_read()` | Reads bytes from incoming TCP socket payload buffers. |
| **Networking** | `write()` | `lwip_write()` | Enqueues output bytes into TCP transmit queues. |
| **Networking** | `close()` | `lwip_close()` | Performs TCP graceful FIN teardown and frees descriptor. |

---

## 5. Memory Footprint & Resource Breakdown

Binary analysis of `RTOSDemo.out` measured with `arm-none-eabi-size`:

```text
   text       data        bss        dec        hex    filename
  68950        238     155131     224319      36c3f    RTOSDemo.out
```

```
+-----------------------------------------------------------------------------------+
|                        FLASH Memory Map (4096 KB Total)                           |
| +-------------------------------------------------------------------------------+ |
| | Code & Read-Only Constants (text): ~68.9 KB (1.68% of 4096 KB Flash)          | |
| +-------------------------------------------------------------------------------+ |
+-----------------------------------------------------------------------------------+

+-----------------------------------------------------------------------------------+
|                         SRAM Memory Map (8192 KB Total)                           |
| +---------------------+-------------------------------+-------------------------+ |
| | Initialized Data    | FreeRTOS Dynamic Heap         | LwIP Buffers & BSS      | |
| | 0.2 KB (238 B)      | configTOTAL_HEAP_SIZE: 100 KB | 51.5 KB                 | |
| +---------------------+-------------------------------+-------------------------+ |
| <----------------------- Total Static RAM: 151.4 KB ----------------------------> |
+-----------------------------------------------------------------------------------+
```

- **Instruction Memory (`text`)**: **~68.9 KB** (Kernel ~22 KB, LwIP ~35 KB, POSIX Shim ~4.5 KB, Demo App ~5.3 KB).
- **RAM Memory (`bss` + `data`)**: **151.4 KB** statically allocated in QEMU (100 KB for dynamic task stacks/semaphores in `heap_4.c`, 51.5 KB for LwIP socket tables and MTU packet pools).

---

## 6. Compatibility Tiers & Future Expansion Roadmap

POSIX compatibility is classified into **4 distinct tiers**:

```
+-------------------------------------------------------------------------------+
| Tier 4: Full Linux Kernel Syscall Spec (330+ Syscalls)                        |
| [Requires Hardware MMU, Virtual Memory & fork; Unviable on microcontrollers]  |
+-------------------------------------------------------------------------------+
                                       |
                                       v
+-------------------------------------------------------------------------------+
| Tier 3: IEEE POSIX PSE51 Real-Time Profile (~120 APIs)                        |
| (Message Queues, Barriers, Read-Write Locks, POSIX Timers)                    |
+-------------------------------------------------------------------------------+
                                       |
                                       v
+-------------------------------------------------------------------------------+
| Tier 2: Real-World Multithreaded C Software & Daemons (~50 APIs)              |
| (Condition Variables, High-Res Clocks, Socket Select/Poll, Non-Blocking IO)   |
+-------------------------------------------------------------------------------+
                                       |
                                       v
+-------------------------------------------------------------------------------+
| Tier 1: Current Implementation (22 APIs) [ACTIVE CODEBASE]                    |
| (Pthreads, Mutexes, Counting Semaphores, Delays, Blocking BSD Sockets)        |
+-------------------------------------------------------------------------------+
```

### Next 28 Target APIs for Tier 2 Compliance:
1. **Group A: Condition Variables (5 APIs)**: `pthread_cond_init`, `pthread_cond_wait`, `pthread_cond_timedwait`, `pthread_cond_signal`, `pthread_cond_broadcast`
2. **Group B: High-Resolution Clocks & Timers (4 APIs)**: `clock_gettime`, `gettimeofday`, `nanosleep`, `time`
3. **Group C: Advanced & Non-Blocking Sockets (7 APIs)**: `connect`, `send`, `recv`, `select`, `poll`, `setsockopt`, `fcntl`
4. **Group D: Extended Mutex & Thread Attributes (4 APIs)**: `pthread_mutex_trylock`, `pthread_mutex_timedlock`, `pthread_mutexattr_init`, `pthread_attr_setstacksize`
5. **Group E: Virtual File System (VFS) Abstraction (8 APIs)**: `open`, `read`, `write`, `lseek`, `close`, `stat`, `pipe`, `dup2`

---

## 7. Project Evolution & Git Commit History

The project was developed across 6 primary phases between May 2026 and August 2026:

```
May 2026                July 2026                                       August 2026
+-----------------------+-----------------------------------------------+-----------------------+
| Phase 1 & 2:          | Phase 3 & 4:          Phase 5 & 6:            | Refactoring &         |
| Bare-metal FreeRTOS   | Thread Shim, Mutex,   LwIP Integration,       | POSIX Decoupling,     |
| QEMU Target &         | Semaphores, Timing &  SMSC9118 Driver, BSD    | Tier Roadmap &        |
| Feasibility Study     | Architecture Docs     Sockets & Web Server    | Documentation Polish  |
+-----------------------+-----------------------------------------------+-----------------------+
```

### Master Milestone Schedule Table:

| Task ID | Phase | Milestone / Task Name | Key Outputs & Reference Files |
| :---: | :--- | :--- | :--- |
| **t0** | Phase 1 | Literature Research | Survey of POSIX RTOS shimming & Cortex-M memory bounds |
| **t1** | Phase 1 | Feasibility Study | Cortex-M3 RAM budget & POSIX shim evaluation |
| **t2** | Phase 1 | Feasibility Report & Heatmap Creation | Feasibility report & compatibility heatmap creation |
| **t3** | Phase 2 | QEMU & FreeRTOS Environment Setup | FreeRTOS Kernel & QEMU MPS2 target setup |
| **t4** | Phase 2 | Memory Layout & UART Console Redirection | Linker script `mps2_m3.ld` & UART bindings in [`main.c`](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main.c) |
| **t5** | Phase 3 | Basic POSIX Thread Translation Shim | `pthread_create` mapping to FreeRTOS `xTaskCreate` |
| **t6** | Phase 3 | Build Fixes & Type Casting Debug | Toolchain cross-compilation fix & Makefile variable expansion (`$`) fixes |
| **t7** | Phase 3 | System Overview & Architecture Docs | Architectural documentation in [`misc/SYSTEM_OVERVIEW.md`](misc/SYSTEM_OVERVIEW.md) |
| **t8** | Phase 4 | Thread Lifecycle (`pthread_join`, `exit`, `detach`, `self`) | Decoupled thread registry in [`posix_shim.c`](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c) |
| **t9** | Phase 4 | Mutex & Counting Semaphore Synchronization | `pthread_mutex_t` & custom counting semaphore `sem_t` |
| **t10** | Phase 4 | Timing Primitives (`sleep`, `usleep`) | Timing mapping to `vTaskDelay` scheduler ticks |
| **t11** | Phase 5 | LwIP TCP/IP Stack Integration | LwIP OS layer adaptation in [`sys_arch.c`](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/sys_arch.c) |
| **t12** | Phase 5 | SMSC9118 Ethernet Driver & NVIC Interrupts | Hardware Ethernet driver in [`ethernetif.c`](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/ethernetif.c) |
| **t13** | Phase 6 | POSIX Socket Shim Wrapper Layers | BSD Socket APIs (`socket`, `bind`, `listen`, `accept`) |
| **t14** | Phase 6 | HTTP Web Server Demo Application | Simulated POSIX Web Server listening on port 80/8080 |

### Detailed Commit Log:
- **May 26, 2026 (`30af38a3`, `95576613`)** — *Akshat*: Initial bare-metal FreeRTOS kernel setup on STM32 / Cortex-M3 MPS2 in QEMU.
- **July 06, 2026 (`3ac468e0`, `61526f7f`, `b6a6c648`, `01377d11`)** — *Kartikaye*: Feasibility report + Heatmap, macOS cross-compiler fixes, and initial `SYSTEM_OVERVIEW.md`.
- **July 16–17, 2026 (`e8d84903`, `21583a04`)** — *Kartikaye & Akshat*: Full pthread lifecycle (`join`, `exit`, `detach`, `self`), mutexes, semaphores, delays, and LwIP source tree import.
- **July 21, 2026 (`d6086830`, `82b45319`, `7e70d8a6`, `c04055a4`, `ee593df9`, `8ecb8adf`, `af83833f`, `e6a7cca4`, `3d279ed9`)** — *Akshat & Kartikaye*: 6-week Gantt chart timeline, ReportLab PDF generators (`generate_pdf.py`, `generate_results_pdf.py`), results matrix, and image assets.
- **July 22–23, 2026 (`745a2ade`, `6be9ce47`, `d23f07f3`, `6d1a69b0`, `651e7a89`, `bcb78a64`, `ef9117a4`)** — *Akshat & Kartikaye*: In-depth technical guides (`MAIN_BLINKY_AND_NETWORKING_GUIDE.md`, `ultra_detailed_codebase_documentation_report.md`), CI workflow streamling, and directory consolidation into `misc/`.
- **August 04, 2026 (`6f67ba49`, `a9243cf0`, `24bcd09c`)** — *Akshat*: Decoupled POSIX compatibility layer into standalone [`posix_shim.c`](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c) and [`posix_shim.h`](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.h), resolved Makefile variable vulnerabilities, and merged `main` into `dev`.
- **August 13, 2026 (`ec27cddc`, `6bb65982`, `3975520f`)** — *Kartikaye*: Authored [`API_TRANSLATION_ROADMAP.md`](API_TRANSLATION_ROADMAP.md) detailing the 4-tier model and 28 target APIs, updated README with project achievements and roadmap, sanitized relative documentation links.

---

## 8. Build, Run & Verification Guide

### Prerequisites
- **GNU ARM Toolchain**: `arm-none-eabi-gcc` and `arm-none-eabi-size`
- **QEMU System Emulator**: `qemu-system-arm`
- **Build Tool**: GNU `make`

### Compilation Commands:
```bash
# Navigate to GCC build directory
cd FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/build/gcc

# Create output folder and build target binary
mkdir -p output
make clean all
```

### Execution in QEMU:
```bash
qemu-system-arm -machine mps2-an385 -cpu cortex-m3 \
  -kernel output/RTOSDemo.out \
  -monitor none -nographic -serial stdio \
  -netdev user,id=mynet0,hostfwd=tcp::8080-:80 \
  -net nic,model=lan9118,netdev=mynet0
```
*(To exit QEMU terminal console: press `Ctrl + A`, then release and press `X`)*.

### Runtime Verification Output:
```text
--- Booting Simulated Linux Environment ---
[Worker 1] Started. Incrementing counter 5 times...
[Worker 2] Started. Incrementing counter 5 times...
[Sem Worker] Waiting for semaphore...
[Worker 1] Finished.
[Worker 2] Finished.
[Main] Posting to semaphore...
[Main] Joining Worker 1...
[Main] Worker 1 joined with status: 1
[Main] Joining Worker 2...
[Main] Worker 2 joined with status: 2
[Main] Joining Semaphore Worker...
[Sem Worker] Semaphore received! Running task...
[Web Server] LwIP Initialized. IP address: 10.0.2.15
[Web Server] Listening on port 80...
[Sem Worker] Task completed. Exiting.
[Main] Semaphore Worker joined.
[Main] Joining Web Server...
```

### Live HTTP Socket Server Test:
From host machine terminal:
```bash
curl http://localhost:8080
```

**HTTP 200 Response:**
```html
HTTP/1.1 200 OK
Content-Type: text/html
Connection: close

<!DOCTYPE html>
<html>
<head><title>STM32 Simulated Linux</title></head>
<body>
<h1>Hello from STM32 Simulated Linux!</h1>
<p>This web page is served from a simulated POSIX socket layer running on FreeRTOS inside QEMU.</p>
</body>
</html>
```
