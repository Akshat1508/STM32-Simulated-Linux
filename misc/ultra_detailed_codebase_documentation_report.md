# Ultra-Detailed Technical Codebase Documentation Report: STM32 Simulated Linux
 
**Target Architecture**: ARM Cortex-M3 (MPS2 AN385 Emulator Model on QEMU)  
**Target Core Frequency**: 25 MHz (SysTick Rate: 1000 Hz / 1 ms resolution)  
**Kernel & Stacks**: FreeRTOS Real-Time Kernel (V202212.00), LwIP POSIX TCP/IP Stack, Custom `pthread` Compatibility Shim  

---

# Part 1: Project Prerequisites & Core Concepts

## 1. FreeRTOS & FreeRTOS-Plus Architecture

### 1.1 Microcontroller RTOS Fundamentals & Execution Paradigm
FreeRTOS is a deterministic, real-time operating system kernel designed specifically for microcontrollers where memory and CPU clock cycles are strictly constrained. Unlike general-purpose OS kernels (such as Linux or Windows) that prioritize throughput and fair time-sharing across user space applications, FreeRTOS guarantees **deterministic, bounded response times** to real-time hardware events.

In this project, the target board is the **ARM Cortex-M3 MPS2 AN385** emulated inside QEMU (`qemu-system-arm -machine mps2-an385`). The ARM Cortex-M3 core operates under a dual-stack pointer hardware mechanism:
1. **Main Stack Pointer (`MSP`)**: Used during system boot, OS kernel execution, and hardware Interrupt Service Routines (ISRs).
2. **Process Stack Pointer (`PSP`)**: Used exclusively by application tasks during user thread execution.

```
                    ARM Cortex-M3 Execution Paradigm
  +-------------------------------------------------------------------+
  |                      Hardware Interrupts / ISRs                   |
  |    (Ethernet IRQ 13, SysTick Timer, PendSV Exception, SVC)        |
  |                        Executes using MSP                         |
  +-------------------------------------------------------------------+
                                   |
                Context Switch / Exception Return (0xFFFFFFFD)
                                   v
  +-------------------------------------------------------------------+
  |                       FreeRTOS Tasks (PSP)                        |
  |   +-------------------+  +-------------------+  +---------------+ |
  |   | POSIX Web Server  |  |  Worker Thread 1  |  |  LWIP_RX Task | |
  |   |  (Priority 2)     |  |   (Priority 2)    |  | (Priority 5)  | |
  |   +-------------------+  +-------------------+  +---------------+ |
  +-------------------------------------------------------------------+
```

### 1.2 Task Scheduling, Control Blocks, & Context Switching
A task in FreeRTOS is an independent C execution context running inside an infinite loop (`for(;;)` or `while(1)`). Each task is represented in RAM by a **Task Control Block (`TCB_t`)**, which maintains:
* `pxTopOfStack`: Pointer to the top of the task's private stack region.
* `xStateListItem`: Linked list node placing the task in `Ready`, `Blocked`, or `Suspended` lists.
* `uxPriority`: Integer priority level (`0` to `configMAX_PRIORITIES - 1`).
* `pxStack`: Pointer to the base of the stack allocation.
* `pcTaskName`: Human-readable ASCII string assigned during task creation.

#### Preemptive Priority Scheduling Mechanics
The FreeRTOS scheduler operates on a **preemptive, priority-based policy with time-slicing**. 
* **SysTick Handler (`xPortSysTickHandler`)**: Executed every 1 ms (1000 Hz). Increments `xTickCount` and scans the `xDelayedTaskList` to move unblocked tasks into the `pxReadyTasksLists`. If a newly unblocked task has a higher priority than the current running task, `xNeedSysTickSwitch` is set to `pdTRUE`.
* **PendSV Exception (`xPortPendSVHandler`)**: The actual context switch is deferred to the PendSV (Pended System Call) exception, which runs at the lowest interrupt priority to ensure hardware ISRs are never delayed by a context switch.

#### Cortex-M3 Assembly Context Switch Lifecycle
1. **Hardware Push**: Upon an exception trigger, the Cortex-M3 hardware automatically pushes 8 registers onto the current stack (`PSP`): `xPSR`, `PC`, `LR`, `R12`, `R3`, `R2`, `R1`, `R0`.
2. **Software Push (`xPortPendSVHandler`)**: The PendSV ISR executes using `MSP`:
   * Saves the current `PSP` value into register `R0`.
   * Manually pushes remaining Core registers (`R4` through `R11`) onto the task's stack.
   * Saves the updated `PSP` back into `pxCurrentTCB->pxTopOfStack`.
3. **TCB Swap**: Calls `vTaskSwitchContext()` to select the highest priority ready task and set `pxCurrentTCB`.
4. **Software Pop**: Loads the new `pxCurrentTCB->pxTopOfStack` into `R0`, pops registers `R4` through `R11` from the new task's stack, and updates `PSP`.
5. **Hardware Pop & Return**: Executes `bx r14` with EXC_RETURN value `0xFFFFFFFD`, causing the Cortex-M3 hardware to pop the remaining registers from `PSP` and resume execution of the new task.

### 1.3 Heap Memory Allocation & Management (`heap_4.c`)
Dynamic memory in FreeRTOS is isolated from standard C `malloc`/`free` calls to avoid non-deterministic fragmentation and thread-safety violations. This project uses `heap_4.c`:
* **Algorithm**: First-fit memory allocation with automatic block coalescing.
* **Heap Array**: Allocated statically as `uint8_t ucHeap[configTOTAL_HEAP_SIZE]` in RAM (`configTOTAL_HEAP_SIZE` configured in `FreeRTOSConfig.h`).
* **Coalescing**: When `vPortFree()` releases a memory block, it immediately checks if adjacent memory blocks are free and merges them into a single contiguous block, preventing heap fragmentation during repeated thread creation/deletion.

---

## 2. Synchronization & Concurrency: POSIX Compatibility Layer

### 2.1 Theoretical Architecture of the `pthread` Wrapper
Standard embedded C programs written for FreeRTOS use non-portable native kernel calls like `xTaskCreate()`, `xSemaphoreTake()`, and `vTaskDelay()`. To create an environment simulating POSIX-compliant Linux execution, a **POSIX Shim Abstraction Layer** was designed in `main_blinky.c`.

```
           POSIX Application Code (e.g. web_server_thread)
       +-------------------------------------------------------+
       | pthread_create() | pthread_mutex_lock() | sem_wait()  |
       +-------------------------------------------------------+
                                  |
                                  v
                POSIX Compatibility Layer (main_blinky.c)
       +-------------------------------------------------------+
       | Maps POSIX handles to posix_thread_t and FreeRTOS     |
       | Mutex/Semaphore primitives with dynamic tracking      |
       +-------------------------------------------------------+
                                  |
                                  v
                   FreeRTOS Core Kernel Primitives
       +-------------------------------------------------------+
       |  xTaskCreate()  | xSemaphoreCreateMutex() | xQueue... |
       +-------------------------------------------------------+
```

### 2.2 Thread Lifecycle & Registry Management (`posix_thread_t`)
Every POSIX thread created via `pthread_create()` is encapsulated by a dynamic structure:
```c
typedef struct posix_thread {
    TaskHandle_t xTask;              // Native FreeRTOS Task Handle
    void *(*start_routine)(void *);  // POSIX thread entry function pointer
    void *arg;                       // User parameter passed to entry function
    void *retval;                    // Exit return status pointer
    SemaphoreHandle_t join_sem;      // Binary semaphore for blocking pthread_join
    volatile int detached;           // Flag indicating if thread is detached (1) or joinable (0)
    struct posix_thread *next;       // Linked list pointer for registry lookup
} posix_thread_t;
```

#### Thread Invariants & State Transitions
1. **Creation**: `pthread_create()` allocates `posix_thread_t`, creates `join_sem` (initialized as empty), registers the thread in `g_thread_list` inside a critical section, and calls `xTaskCreate(posix_thread_wrapper, ...)` passing the struct pointer as `pvParameters`.
2. **Execution Wrapper**: `posix_thread_wrapper()` executes on the newly spawned FreeRTOS task context. It executes `t->start_routine(t->arg)` and passes the return pointer directly to `pthread_exit(retval)`.
3. **Termination (`pthread_exit`)**:
   * If `detached == 0`: Stores `retval`, signals `xSemaphoreGive(t->join_sem)`, and enters a suspended state awaiting cleanup by `pthread_join()`.
   * If `detached == 1`: Immediately unlinks `t` from `g_thread_list`, deletes `join_sem`, frees memory `vPortFree(t)`, and calls `vTaskDelete(NULL)`.
4. **Joining (`pthread_join`)**: Blocks the calling thread by attempting `xSemaphoreTake(t->join_sem, portMAX_DELAY)`. Once taken, extracts `t->retval`, unlinks `t`, deletes `join_sem`, frees memory `vPortFree(t)`, and returns.

### 2.3 IPC & Synchronization Primitives

#### POSIX Mutexes (`pthread_mutex_t`)
* Built on top of `xSemaphoreCreateMutex()`.
* **Priority Inheritance Protocol**: If a High Priority task attempts to lock a mutex held by a Low Priority task, the kernel temporarily elevates the Low Priority task's priority to match the High Priority task. This prevents **Priority Inversion**, where an intermediate Medium Priority task pre-empts the Low Priority task and indefinitely delays the High Priority task.

#### POSIX Counting Semaphores (`sem_t`)
* Built on `xSemaphoreCreateCounting(65535, initial_value)`.
* `sem_wait()` calls `xSemaphoreTake(sem, portMAX_DELAY)`, decrementing the resource counter or blocking the thread if count is 0.
* `sem_post()` calls `xSemaphoreGive(sem)`, incrementing the count and unblocking waiting threads.

#### FreeRTOS Queues (`QueueHandle_t`)
* Thread-safe, fixed-capacity ring buffers copying message payloads by value.
* Feature two internal blocked-task lists: `xTasksWaitingToSend` and `xTasksWaitingToReceive`. Used internally as the backing structure for LwIP mailboxes (`sys_mbox_t`).

---

## 3. Networking & File Systems Architecture

### 3.1 LwIP TCP/IP Stack Operating Modes & Architecture
LwIP (Lightweight IP) is an open-source TCP/IP stack designed to run in embedded systems without sacrificing protocol compliance. LwIP supports two operating paradigms:
1. **Raw API / Callback Mode (`NO_SYS = 1`)**: Single-threaded event-driven mode. Fast but lacks thread safety and POSIX socket compatibility.
2. **Sequential & POSIX Socket API Mode (`NO_SYS = 0`)**: Multithreaded mode utilized in this project. The LwIP core runs inside its own dedicated kernel task (`tcpip_thread`). User application threads interact with the stack using thread-safe POSIX socket shims (`socket()`, `bind()`, `listen()`, `accept()`, `read()`, `write()`).

### 3.2 Full End-to-End Packet Lifecycle
```
[Physical Network Interface / QEMU TAP Device]
                       | (Raw Ethernet Frames)
                       v
[SMSC9118 Controller FIFO Registers (0x40000000)]
                       |
               (Hardware Interrupt)
                       |
                       v
            [EthernetISR() (IRQ 13)]
                       | (vTaskNotifyGiveFromISR)
                       v
         [LWIP_RX Input Task (Priority 5)]
                       | (pbuf_alloc + smsc9220_receive_by_chunks)
                       v
             [netif->input() / etharp_input()]
                       |
                       v
               [ip4_input() -> tcp_input()]
                       | (sys_mbox_post to socket connection queue)
                       v
     [web_server_thread (accept() / read() on socket)]
```

1. **Hardware Arrival**: An incoming Ethernet packet arrives at the emulated SMSC9118 PHY/MAC controller. The packet length and status are pushed into the hardware Rx FIFO, asserting NVIC IRQ 13.
2. **Interrupt Service Routine**: `EthernetISR()` in `ethernetif.c` intercepts IRQ 13. It reads `get_irq_status()`, clears the interrupt flag, disables further Rx FIFO interrupts, and executes `vTaskNotifyGiveFromISR(xRxTaskHandle)`. If `LWIP_RX` has a higher priority than the interrupted task, `portYIELD_FROM_ISR()` requests an immediate context switch.
3. **Receiver Task (`LWIP_RX`)**: Unblocks from `ulTaskNotifyTake()`. Calls `smsc9220_peek_next_packet_size()` to determine payload length, allocates a zero-copy packet buffer (`pbuf_alloc(PBUF_RAW, len, PBUF_POOL)`), and transfers raw frame data from hardware registers via `smsc9220_receive_by_chunks()`.
4. **Stack Processing**: Passes `pbuf` to `netif->input(p, netif)`. LwIP checks the Ethernet frame header (ARP vs IP). IP packets are routed to `ip4_input()`, then to `tcp_input()`.
5. **POSIX Socket Delivery**: `tcp_input()` matches the TCP segment to an active listening socket control block (`tcp_pcb`). Data payload pointers are posted into the socket's receive mailbox (`sys_mbox_t`).
6. **User Space Consumption**: `web_server_thread` unblocks from `read(client_fd, ...)` or `accept()`, processes the HTTP GET request, and sends HTML content back via `write(client_fd, ...)` -> `low_level_output()`.

### 3.3 Target File System Architecture (LittleFS & Memory Integration)
In embedded Linux simulation environments, file system management is handled by **LittleFS**, a fail-safe file system designed for microcontrollers with NOR/NAND flash memory:
* **Copy-on-Write (CoW)**: LittleFS never overwrites active blocks in place. When modifying files or metadata, updated data is written to fresh flash blocks before updating root pointers. This guarantees that unexpected power loss during a write operation leaves the previous file system state intact.
* **Wear Leveling**: Dynamically tracks erase cycle counts across flash blocks, distributing writes evenly to prevent flash block burn-out.
* **POSIX Shim Abstraction**: LittleFS C APIs (`lfs_file_open`, `lfs_file_read`, `lfs_file_write`) are wrapped inside POSIX file descriptor shims (`open()`, `read()`, `write()`, `close()`), allowing HTTP web servers to serve static assets directly from emulated flash or RAM disks.

---

# Part 2: Detailed File Analysis

---

## 1. `FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main.c`

### 1.1 File Overview
* **File Path**: [main.c](file:///c:/Users/Alok%20Jain/Desktop/STM32/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main.c)
* **Primary Purpose**: Master hardware setup and application selector. Initializes processor serial communication (UART0), trace diagnostic infrastructure (Percepio TraceRecorder), and dispatches execution to `main_blinky()` (Simulated Linux environment).

### 1.2 Component Breakdown

#### Functions & Methods

##### `int main(void)`
* **Objective**: Primary system entry point post-reset. Initializes diagnostics, configures UART hardware, and selects application path.
* **Parameters Table**:
  | Name | Type | Direction | Description & Constraints |
  | :--- | :--- | :--- | :--- |
  | None | N/A | N/A | Called directly by `Reset_Handler`. |
* **Returns**: `int` — Always returns `0` (conceptually), but execution transfers infinitely to `vTaskStartScheduler()`.
* **Side Effects**: Reads/writes MMIO registers for UART0; modifies TraceRecorder memory control blocks.
* **Failure Modes**: If `main_blinky()` fails to start scheduler, main returns 0 and halts.

##### `static void prvUARTInit(void)`
* **Objective**: Configure MPS2 AN385 UART0 hardware registers to redirect C standard `printf()` output to standard output.
* **Parameters**: None.
* **Returns**: None (`void`).
* **Side Effects**: Writes `16` to `UART0_BAUDDIV` (`0x40004010`); sets `TX_BUFFER_MASK` (bit 0) in `UART0_CTRL` (`0x40004008`).

##### `void vApplicationMallocFailedHook(void)`
* **Objective**: Exception callback triggered when `pvPortMalloc()` fails to allocate required heap memory.
* **Parameters**: None.
* **Returns**: None (`void`).
* **Side Effects**: Disables core interrupts via `taskDISABLE_INTERRUPTS()`; locks CPU in infinite `for(;;)` loop.

##### `void vApplicationIdleHook(void)`
* **Objective**: Called continuously by the FreeRTOS Idle Task when no application tasks are ready.
* **Parameters**: None.
* **Returns**: None (`void`).
* **Side Effects**: Executes full demo idle functions if `mainCREATE_SIMPLE_BLINKY_DEMO_ONLY == 0`.

##### `void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName)`
* **Objective**: Exception callback triggered when the kernel detects stack pointer limit violation for a task.
* **Parameters Table**:
  | Name | Type | Direction | Description & Constraints |
  | :--- | :--- | :--- | :--- |
  | `pxTask` | `TaskHandle_t` | In | Native FreeRTOS handle of corrupted task. |
  | `pcTaskName` | `char *` | In | Null-terminated ASCII string name of task. |
* **Returns**: None (`void`).
* **Side Effects**: Disables interrupts and freezes CPU execution in infinite loop.

##### `void vApplicationTickHook(void)`
* **Objective**: Interrupt hook called inside `xPortSysTickHandler()` during every 1 ms SysTick interrupt.
* **Parameters**: None.
* **Returns**: None (`void`).
* **Side Effects**: Invokes `vFullDemoTickHookFunction()` if enabled.

#### Key Variables, Structs & State
* `mainCREATE_SIMPLE_BLINKY_DEMO_ONLY` (`#define 1`): Integer macro configuring compilation of `main_blinky.c`.
* `UART0_ADDRESS` (`0x40004000UL`): MMIO memory address for QEMU MPS2 serial controller.
* `UART0_DATA` (`*(volatile uint32_t*)(0x40004000UL)`): Data register for outputting ASCII characters.

### 1.3 Execution Flow & Logic
```
  [Reset_Handler] -> main()
                       |
                       +--> [configUSE_TRACE_FACILITY == 1] -> xTraceInitialize() -> xTraceEnable()
                       |
                       +--> prvUARTInit() -> Writes UART0_BAUDDIV & UART0_CTRL
                       |
                       +--> [#if mainCREATE_SIMPLE_BLINKY_DEMO_ONLY == 1]
                                 |
                                 v
                            main_blinky()
```

### 1.4 Dependencies & Interactions
* Depends on [FreeRTOS.h](file:///c:/Users/Alok%20Jain/Desktop/STM32/STM32-Simulated-Linux/FreeRTOS/Source/include/FreeRTOS.h), `task.h`, `trcRecorder.h`.
* Invokes `main_blinky()` in [main_blinky.c](file:///c:/Users/Alok%20Jain/Desktop/STM32/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main_blinky.c).

---

## 2. `FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main_blinky.c`

### 2.1 File Overview
* **File Path**: [main_blinky.c](file:///c:/Users/Alok%20Jain/Desktop/STM32/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main_blinky.c)
* **Primary Purpose**: Implements the entire POSIX Compatibility Layer (`pthread`, `pthread_mutex`, `sem_t`, `sleep`, `usleep`), maintains global thread tracking structures, executes worker thread concurrency tests, and runs the POSIX LwIP HTTP Web Server.

### 2.2 Component Breakdown

#### Functions & Methods

##### `void main_blinky(void)`
* **Objective**: Primary setup routine called by `main()`. Spawns `main_posix_app` thread and starts FreeRTOS scheduler.
* **Parameters**: None.
* **Returns**: None (`void`).
* **Side Effects**: Calls `pthread_create()`, `pthread_detach()`, and `vTaskStartScheduler()`.

##### `void *main_posix_app(void *arg)`
* **Objective**: Master POSIX application orchestrator thread.
* **Parameters**: `arg` (`void *`, unused).
* **Returns**: `void *` (Returns `NULL`).
* **Side Effects**: Initializes `g_counter_mutex` and `g_job_semaphore`, creates 4 POSIX threads (`thread1`, `thread2`, `sem_thread`, `web_server`), performs `pthread_join()` on all, and destroys IPC objects.

##### `int pthread_create(pthread_t *thread, const void *attr, void *(*start_routine)(void *), void *arg)`
* **Objective**: Allocates POSIX thread wrapper, creates join semaphore, adds thread to `g_thread_list`, and spawns native FreeRTOS task.
* **Parameters Table**:
  | Name | Type | Direction | Description & Constraints |
  | :--- | :--- | :--- | :--- |
  | `thread` | `pthread_t *` | Out | Pointer to store allocated thread wrapper handle. |
  | `attr` | `const void *` | In | Thread creation attributes (Unused/NULL). |
  | `start_routine` | `void *(*)(void *)` | In | Pointer to thread entry function. Must not be NULL. |
  | `arg` | `void *` | In | User argument pointer passed to entry function. |
* **Returns**: `int` — `0` on successful creation, `-1` on allocation failure.
* **Side Effects**: Allocates memory via `pvPortMalloc()`, creates binary semaphore `xSemaphoreCreateBinary()`, mutates `g_thread_list`, and invokes `xTaskCreate()`.

##### `int pthread_join(pthread_t thread, void **retval)`
* **Objective**: Blocks calling thread until target thread calls `pthread_exit()`, retrieves return pointer, and reclaims memory.
* **Parameters Table**:
  | Name | Type | Direction | Description & Constraints |
  | :--- | :--- | :--- | :--- |
  | `thread` | `pthread_t` | In | Opaque handle pointer to target thread. |
  | `retval` | `void **` | Out | Optional location to store target thread return pointer. |
* **Returns**: `int` — `0` on success, `-1` if target thread handle is invalid or detached.
* **Side Effects**: Blocks on `xSemaphoreTake(t->join_sem, portMAX_DELAY)`, removes `t` from `g_thread_list`, deletes `join_sem`, and frees memory `vPortFree(t)`.

##### `int pthread_detach(pthread_t thread)`
* **Objective**: Marks thread as detached, instructing kernel to auto-reclaim memory upon exit.
* **Parameters**: `thread` (`pthread_t`, Target handle).
* **Returns**: `int` — `0` on success, `-1` if thread not found.
* **Side Effects**: Mutates `t->detached = 1`.

##### `void pthread_exit(void *retval)`
* **Objective**: Terminates calling POSIX thread, stores return status, signals joiners, or frees memory if detached.
* **Parameters**: `retval` (`void *`, Return status value).
* **Returns**: None (Does not return).
* **Side Effects**: Stores `t->retval`, calls `xSemaphoreGive(t->join_sem)` or `vPortFree(t)`, and calls `vTaskDelete(NULL)`.

##### `pthread_t pthread_self(void)`
* **Objective**: Queries thread handle of current task.
* **Parameters**: None.
* **Returns**: `pthread_t` — Handle of calling thread, or `NULL`.
* **Side Effects**: Traverses `g_thread_list` inside critical section matching `xTaskGetCurrentTaskHandle()`.

##### `int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr)`
* **Objective**: Initializes POSIX mutex object.
* **Parameters**: `mutex` (`pthread_mutex_t *`), `attr` (`const void *`, unused).
* **Returns**: `int` — `0` on success, `-1` on failure.
* **Side Effects**: Assigns `*mutex = xSemaphoreCreateMutex()`.

##### `int pthread_mutex_lock(pthread_mutex_t *mutex)`
* **Objective**: Locks POSIX mutex, blocking indefinitely until acquired.
* **Parameters**: `mutex` (`pthread_mutex_t *`).
* **Returns**: `int` — `0` on success.
* **Side Effects**: Invokes `xSemaphoreTake(*mutex, portMAX_DELAY)`.

##### `int pthread_mutex_unlock(pthread_mutex_t *mutex)`
* **Objective**: Releases held POSIX mutex.
* **Parameters**: `mutex` (`pthread_mutex_t *`).
* **Returns**: `int` — `0` on success.
* **Side Effects**: Invokes `xSemaphoreGive(*mutex)`.

##### `int pthread_mutex_destroy(pthread_mutex_t *mutex)`
* **Objective**: Destroys POSIX mutex object.
* **Parameters**: `mutex` (`pthread_mutex_t *`).
* **Returns**: `int` — `0` on success.
* **Side Effects**: Calls `vSemaphoreDelete(*mutex)`.

##### `int sem_init(sem_t *sem, int pshared, unsigned int value)`
* **Objective**: Initializes POSIX counting semaphore.
* **Parameters**: `sem` (`sem_t *`), `pshared` (`int`, unused), `value` (`unsigned int`, initial resource count).
* **Returns**: `int` — `0` on success, `-1` on failure.
* **Side Effects**: Allocates counting semaphore `xSemaphoreCreateCounting(65535, value)`.

##### `int sem_wait(sem_t *sem)`
* **Objective**: Decrements counting semaphore or blocks if count is 0.
* **Parameters**: `sem` (`sem_t *`).
* **Returns**: `int` — `0` on success.
* **Side Effects**: Calls `xSemaphoreTake(*(SemaphoreHandle_t*)sem, portMAX_DELAY)`.

##### `int sem_post(sem_t *sem)`
* **Objective**: Increments counting semaphore, unblocking waiting threads.
* **Parameters**: `sem` (`sem_t *`).
* **Returns**: `int` — `0` on success.
* **Side Effects**: Calls `xSemaphoreGive(*(SemaphoreHandle_t*)sem)`.

##### `int sem_destroy(sem_t *sem)`
* **Objective**: Destroys counting semaphore object.
* **Parameters**: `sem` (`sem_t *`).
* **Returns**: `int` — `0` on success.
* **Side Effects**: Calls `vSemaphoreDelete(*(SemaphoreHandle_t*)sem)`.

##### `unsigned int sleep(unsigned int seconds)`
* **Objective**: Blocks execution for specified seconds.
* **Parameters**: `seconds` (`unsigned int`).
* **Returns**: `unsigned int` — Always returns `0`.
* **Side Effects**: Calls `vTaskDelay(pdMS_TO_TICKS(seconds * 1000))`.

##### `int usleep(useconds_t usec)`
* **Objective**: Blocks execution for specified microseconds.
* **Parameters**: `usec` (`useconds_t`).
* **Returns**: `int` — Always returns `0`.
* **Side Effects**: Converts `usec` to tick count and calls `vTaskDelay()`.

##### `void *worker_thread_mutex(void *arg)`
* **Objective**: Worker thread demonstrating mutex contention and shared counter protection.
* **Parameters**: `arg` (`void *`, string name).
* **Returns**: `void *` — Exit status integer (`(void*)(uintptr_t)42`).
* **Side Effects**: Locks/unlocks `g_counter_mutex`, increments `g_shared_counter` 5 times with delays.

##### `void *worker_thread_semaphore(void *arg)`
* **Objective**: Worker thread demonstrating IPC signaling wait.
* **Parameters**: `arg` (`void *`, unused).
* **Returns**: `void *` — Exit status integer (`(void*)(uintptr_t)99`).
* **Side Effects**: Blocks on `sem_wait(&g_job_semaphore)`.

##### `static void lwip_init_done_callback(void *arg)`
* **Objective**: Callback executed when LwIP `tcpip_init` finishes core thread startup.
* **Parameters**: `arg` (`void *`, netif pointer).
* **Returns**: None (`void`).
* **Side Effects**: Registers default network interface (`netif_add`, `netif_set_default`, `netif_set_up`).

##### `void *web_server_thread(void *arg)`
* **Objective**: POSIX Socket HTTP Web Server thread listening on port 80.
* **Parameters**: `arg` (`void *`, unused).
* **Returns**: `void *` — Returns `NULL`.
* **Side Effects**: Calls `tcpip_init`, binds socket (`bind`), listens (`listen`), accepts incoming client socket connections (`accept`), sends HTTP standard headers and HTML body, and closes client sockets.

##### Private Helper Functions
* `static void add_thread(posix_thread_t *t)`: Thread-safely appends `t` to `g_thread_list` inside `taskENTER_CRITICAL()`.
* `static posix_thread_t *find_thread(TaskHandle_t xTask)`: Searches `g_thread_list` matching `xTask` inside `taskENTER_CRITICAL()`.
* `static void remove_thread(posix_thread_t *t)`: Thread-safely unlinks `t` from `g_thread_list` inside `taskENTER_CRITICAL()`.
* `void posix_thread_wrapper(void *pvParameters)`: FreeRTOS task entry point executing user `start_routine`.

#### Key Variables & Data Structures
```c
typedef struct posix_thread {
    TaskHandle_t xTask;
    void *(*start_routine)(void *);
    void *arg;
    void *retval;
    SemaphoreHandle_t join_sem;
    volatile int detached;
    struct posix_thread *next;
} posix_thread_t;
```
* `g_thread_list`: Static pointer to head of POSIX thread tracking linked list.
* `g_shared_counter`: Shared resource variable incremented by mutex worker threads.
* `g_counter_mutex`: `pthread_mutex_t` protecting `g_shared_counter`.
* `g_job_semaphore`: `sem_t` counting semaphore used for thread unblocking.

---

## 3. `FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/sys_arch.c`

### 3.1 File Overview
* **File Path**: [sys_arch.c](file:///c:/Users/Alok%20Jain/Desktop/STM32/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/sys_arch.c)
* **Primary Purpose**: System Architecture Abstraction Layer mapping LwIP semaphores, mutexes, mailboxes (queues), and thread management directly onto native FreeRTOS kernel APIs.

### 3.2 Component Breakdown

#### Functions & Methods

* `void sys_init(void)`: No-op initialization hook.
* `err_t sys_sem_new(sys_sem_t *sem, u8_t count)`: Creates FreeRTOS counting semaphore `xSemaphoreCreateCounting(65535, count)`. Returns `ERR_OK` or `ERR_MEM`.
* `void sys_sem_free(sys_sem_t *sem)`: Deletes semaphore `vSemaphoreDelete(*sem)`.
* `void sys_sem_signal(sys_sem_t *sem)`: Unblocks semaphore `xSemaphoreGive(*sem)`.
* `u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout)`: Takes semaphore. If `timeout == 0`, blocks indefinitely (`portMAX_DELAY`). Returns elapsed time in ms or `SYS_ARCH_TIMEOUT`.
* `err_t sys_mutex_new(sys_mutex_t *mutex)`: Creates mutex `xSemaphoreCreateMutex()`. Returns `ERR_OK` or `ERR_MEM`.
* `void sys_mutex_free(sys_mutex_t *mutex)`: Deletes mutex `vSemaphoreDelete(*mutex)`.
* `void sys_mutex_lock(sys_mutex_t *mutex)`: Takes mutex `xSemaphoreTake(*mutex, portMAX_DELAY)`.
* `void sys_mutex_unlock(sys_mutex_t *mutex)`: Gives mutex `xSemaphoreGive(*mutex)`.
* `err_t sys_mbox_new(sys_mbox_t *mbox, int size)`: Creates queue `xQueueCreate(size, sizeof(void *))`. Returns `ERR_OK` or `ERR_MEM`.
* `void sys_mbox_free(sys_mbox_t *mbox)`: Deletes queue `vQueueDelete(*mbox)`.
* `void sys_mbox_post(sys_mbox_t *mbox, void *msg)`: Posts message to queue, blocking indefinitely if full (`xQueueSendToBack(*mbox, &msg, portMAX_DELAY)`).
* `err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg)`: Posts message without blocking (`xQueueSendToBack(*mbox, &msg, 0)`). Returns `ERR_OK` or `ERR_MEM`.
* `u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout)`: Receives message from queue with timeout handling (`xQueueReceive`). Returns elapsed time in ms or `SYS_ARCH_TIMEOUT`.
* `u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)`: Receives message without blocking (`xQueueReceive(..., 0)`). Returns `0` on success or `SYS_MBOX_EMPTY`.
* `sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread, void *arg, int stacksize, int prio)`: Creates LwIP system task `xTaskCreate((TaskFunction_t)thread, name, stacksize, arg, prio, &xCreatedTask)`.
* `u32_t sys_now(void)`: Returns system uptime in ms (`xTaskGetTickCount() * portTICK_PERIOD_MS`).
* `int sys_sem_valid(sys_sem_t *sem)` / `void sys_sem_set_invalid(sys_sem_t *sem)`: Semaphore handle validity queries and resets.
* `int sys_mutex_valid(sys_mutex_t *mutex)` / `void sys_mutex_set_invalid(sys_mutex_t *mutex)`: Mutex handle validity queries and resets.
* `int sys_mbox_valid(sys_mbox_t *mbox)` / `void sys_mbox_set_invalid(sys_mbox_t *mbox)`: Mailbox handle validity queries and resets.
* `sys_prot_t sys_arch_protect(void)`: Enters critical section (`taskENTER_CRITICAL()`).
* `void sys_arch_unprotect(sys_prot_t pval)`: Exits critical section (`taskEXIT_CRITICAL()`).

---

## 4. `FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/ethernetif.c`

### 4.1 File Overview
* **File Path**: [ethernetif.c](file:///c:/Users/Alok%20Jain/Desktop/STM32/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/ethernetif.c)
* **Primary Purpose**: Low-level hardware network driver interfacing LwIP with the SMSC9118 (LAN9118) Ethernet MAC/PHY controller on the emulated ARM Cortex-M3 MPS2 AN385 platform.

### 4.2 Component Breakdown

#### Functions & Methods

##### `err_t ethernetif_init(struct netif *netif)`
* **Objective**: Configures LwIP network interface structure fields (MAC address `00:08:29:11:22:33`, MTU 1500, broadcast/ethernet flags) and calls `low_level_init()`.
* **Parameters**: `netif` (`struct netif *`, pointer to LwIP network interface).
* **Returns**: `err_t` — Returns `ERR_OK`.

##### `static void low_level_init(struct netif *netif)`
* **Objective**: Initializes hardware registers, writes MAC address to hardware registers, configures NVIC interrupt priority for IRQ 13, and creates the `LWIP_RX` input task.
* **Parameters**: `netif` (`struct netif *`).
* **Returns**: None (`void`).
* **Side Effects**: Writes MAC high/low registers, sets NVIC priority `NVIC_SetPriority(13, configMAX_SYSCALL_INTERRUPT_PRIORITY)`, enables Rx status FIFO level interrupt, creates `LWIP_RX` task, and enables NVIC IRQ 13 in `nwNVIC_ISER`.

##### `static err_t low_level_output(struct netif *netif, struct pbuf *p)`
* **Objective**: Transmits raw packet buffer (`pbuf`) chunks out through SMSC9118 hardware.
* **Parameters**: `netif` (`struct netif *`), `p` (`struct pbuf *`, packet buffer chain).
* **Returns**: `err_t` — `ERR_OK` on success, `ERR_IF` on hardware send error.
* **Side Effects**: Calls `smsc9220_send_by_chunks()`.

##### `static void ethernetif_input_task(void *pvParameters)`
* **Objective**: High-priority task (`LWIP_RX`) waiting on hardware interrupt notifications. Reads packets into `pbuf` memory and forwards to `netif->input()`.
* **Parameters**: `pvParameters` (`void *`, netif pointer).
* **Returns**: None (Infinite task loop).
* **Side Effects**: Blocks on `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)`, allocates `pbuf`, invokes `smsc9220_receive_by_chunks()`, calls `netif->input()`, and re-enables Rx FIFO interrupts.

##### `void EthernetISR(void)`
* **Objective**: Hardware Interrupt Service Routine executed upon NVIC IRQ 13 line assertion.
* **Parameters**: None.
* **Returns**: None (`void`).
* **Side Effects**: Reads interrupt status, clears Rx status FIFO interrupt, disables Rx interrupt, sends task notification `vTaskNotifyGiveFromISR(xRxTaskHandle)`, and yields context via `portYIELD_FROM_ISR()`.

##### `static void prvWait_ms(uint32_t ms)`
* **Objective**: Hardware initialization delay callback.
* **Parameters**: `ms` (`uint32_t`).
* **Returns**: None (`void`).
* **Side Effects**: Calls `vTaskDelay(pdMS_TO_TICKS(ms))`.

#### Key Variables & Hardware Definitions
* `nwNVIC_ISER` (`0xE000E100UL`): ARM Cortex-M3 NVIC Interrupt Set Enable Register.
* `nwNVIC_ICER` (`0xE000E180UL`): ARM Cortex-M3 NVIC Interrupt Clear Enable Register.
* `SMSC9220_ETH_DEV`: Device configuration structure targeting `SMSC9220_BASE`.
* `xRxTaskHandle`: Task handle of the high-priority `LWIP_RX` packet receiver task.

---

## 5. Network Stack Configuration (`lwipopts.h`, `arch/cc.h`, `arch/sys_arch.h`)

### 5.1 `FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/lwipopts.h`
* **File Path**: [lwipopts.h](file:///c:/Users/Alok%20Jain/Desktop/STM32/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/lwipopts.h)
* **Primary Purpose**: Configures LwIP compile-time stack options.
* **Key Definitions**:
  * `NO_SYS 0`: Enables full OS multithreading integration.
  * `LWIP_SOCKET 1` & `LWIP_COMPAT_SOCKETS 1`: Enables standard POSIX socket functions (`socket`, `bind`, `listen`, `accept`, `read`, `write`, `close`).
  * `MEM_SIZE (16 * 1024)`: Allocates 16 KB heap for LwIP dynamic pools.
  * `PBUF_POOL_SIZE 16`, `PBUF_POOL_BUFSIZE 1514`: Allocates contiguous packet buffers matching MTU size.
  * `TCPIP_THREAD_PRIO (configMAX_PRIORITIES - 2)`: Assigns high execution priority to LwIP core stack thread.

### 5.2 `FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/arch/cc.h`
* **File Path**: [arch/cc.h](file:///c:/Users/Alok%20Jain/Desktop/STM32/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/arch/cc.h)
* **Primary Purpose**: Defines compiler data types and platform specifiers.
* **Key Definitions**: Sets `BYTE_ORDER LITTLE_ENDIAN`, maps integer types (`u8_t`, `u16_t`, `u32_t`), and defines diagnostic assertion macro `LWIP_PLATFORM_ASSERT()`.

### 5.3 `FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/arch/sys_arch.h`
* **File Path**: [arch/sys_arch.h](file:///c:/Users/Alok%20Jain/Desktop/STM32/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/arch/sys_arch.h)
* **Primary Purpose**: Maps LwIP abstract primitive types to native FreeRTOS handles:
  * `sys_sem_t` -> `SemaphoreHandle_t`
  * `sys_mutex_t` -> `SemaphoreHandle_t`
  * `sys_mbox_t` -> `QueueHandle_t`
  * `sys_thread_t` -> `TaskHandle_t`

---

## 6. Build System & Startup Assembly (`Makefile` & `startup_gcc.c`)

### 6.1 `FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/build/gcc/Makefile`
* **File Path**: [Makefile](file:///c:/Users/Alok%20Jain/Desktop/STM32/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/build/gcc/Makefile)
* **Primary Purpose**: Build automation script targeting `arm-none-eabi-gcc`.
* **Key Build Variables**:
  * `CC = arm-none-eabi-gcc`, `LD = arm-none-eabi-gcc`, `SIZE = arm-none-eabi-size`.
  * `CFLAGS`: `-mthumb -mcpu=cortex-m3 -ffreestanding -g3 -Os -ffunction-sections -fdata-sections`.
  * `LDFLAGS`: `-T ./mps2_m3.ld -specs=nano.specs -specs=nosys.specs`.
* **Compiled Subsystems**: FreeRTOS Core (`tasks.c`, `queue.c`, `heap_4.c`, `port.c`), Percepio TraceRecorder (`trcKernelPort.c`), LwIP Stack (`init.c`, `sockets.c`, `tcpip.c`), LAN9118 Driver (`smsc9220_eth_drv.c`), and POSIX Shim Application (`main.c`, `main_blinky.c`, `sys_arch.c`, `ethernetif.c`).

### 6.2 `FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/build/gcc/startup_gcc.c`
* **File Path**: [startup_gcc.c](file:///c:/Users/Alok%20Jain/Desktop/STM32/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/build/gcc/startup_gcc.c)
* **Primary Purpose**: Defines ARM Cortex-M3 vector table and system reset handlers.
* **Component Breakdown**:
  * `isr_vector[]`: Vector table stored in `.isr_vector` flash section. Contains Initial Stack Pointer (`&_estack`), `Reset_Handler`, FreeRTOS kernel handlers (`vPortSVCHandler`, `xPortPendSVHandler`, `xPortSysTickHandler`), and hardware IRQs.
  * **Modified Vector**: Position 78 (IRQ 13) bound directly to `EthernetISR`.
  * `Reset_Handler()`: Resets CPU registers and invokes `main()`.

---

## 7. Documentation & PDF Generation Tools (`README.md`, `SYSTEM_OVERVIEW.md`, `generate_pdf.py`, `generate_results_pdf.py`)

### 7.1 `README.md` & `SYSTEM_OVERVIEW.md`
* **[README.md](file:///c:/Users/Alok%20Jain/Desktop/STM32/STM32-Simulated-Linux/README.md)**: Main repository documentation providing quickstart compilation instructions (`make clean all`), QEMU invocation syntax with host port forwarding (`-netdev user,id=mynet0,hostfwd=tcp::8080-:80`), and `curl` test commands.
* **[SYSTEM_OVERVIEW.md](file:///c:/Users/Alok%20Jain/Desktop/STM32/STM32-Simulated-Linux/SYSTEM_OVERVIEW.md)**: Comprehensive architectural reference mapping hardware emulation layers to OS shims and networking protocols.

### 7.2 `generate_pdf.py` & `generate_results_pdf.py`
* **[generate_pdf.py](file:///c:/Users/Alok%20Jain/Desktop/STM32/STM32-Simulated-Linux/generate_pdf.py)**: Python script using ReportLab to render `GANTT_CHART.pdf` containing visual project roadmaps and timeline tables.
* **[generate_results_pdf.py](file:///c:/Users/Alok%20Jain/Desktop/STM32/STM32-Simulated-Linux/generate_results_pdf.py)**: Python script using ReportLab to render `results.pdf` formatting empirical POSIX shim benchmark figures and network throughput metrics.

---

# Complete Inter-Component Dependency Matrix

```
+---------------------------------------------------------------------------------------+
|                                    README.md / SYSTEM_OVERVIEW.md                    |
+---------------------------------------------------------------------------------------+
                                           |
                                           v
+---------------------------------------------------------------------------------------+
|                                    Makefile                                           |
+---------------------------------------------------------------------------------------+
         |                                 |                                 |
         v                                 v                                 v
+------------------+             +--------------------+            +--------------------+
|  startup_gcc.c   |             |       main.c       |            |    lwipopts.h      |
| (Vector Table /  |             |  (Hardware Init /  |            |   arch/cc.h        |
|  EthernetISR)    |             |   Demo Selector)   |            |   arch/sys_arch.h  |
+------------------+             +--------------------+            +--------------------+
         |                                 |                                 |
         | (IRQ 13)                        v                                 v
         |                       +--------------------+            +--------------------+
         +---------------------->|   main_blinky.c    |<---------->|    sys_arch.c      |
                                 | (POSIX Shim Layer  |            | (FreeRTOS <-> LwIP |
                                 |   & Web Server)    |            |   Primitive Map)   |
                                 +--------------------+            +--------------------+
                                           |                                 |
                                           v                                 v
                                 +--------------------+            +--------------------+
                                 |    ethernetif.c    |<---------->| FreeRTOS-Plus/LwIP |
                                 |  (SMSC9118 Driver) |            |  (TCP/IP Stack)    |
                                 +--------------------+            +--------------------+
```
