# Comprehensive Architectural Guide: `main_blinky.c` & Supporting Subsystems

**Target Environment**: STM32 Simulated Linux on ARM Cortex-M3 (MPS2 AN385 / QEMU)  
**Core Technologies**: FreeRTOS Kernel V202212.00, Custom POSIX Abstraction Shim, LwIP TCP/IP Stack  

---

# Section 1: Detailed Analysis of ALL Functions in `main_blinky.c`

`main_blinky.c` implements the **POSIX Compatibility Shim Layer** on top of FreeRTOS, global thread tracking data structures, application worker tasks, and an LwIP POSIX socket HTTP web server.

---

### 1. `add_thread(posix_thread_t *t)`
* **Code Signature**: `static void add_thread(posix_thread_t *t)`
* **Objective**: Thread-safely insert a newly created POSIX thread structure (`t`) into the head of the global active thread registry `g_thread_list`.
* **Inputs**:
  * `t` (`posix_thread_t *`): Pointer to an allocated POSIX thread metadata structure. Must not be `NULL`.
* **Outputs**: None (`void`).
* **Under-the-Hood FreeRTOS Mechanics**:
  * Calls `taskENTER_CRITICAL()` to disable interrupts on the Cortex-M3 core by setting the `BASEPRI` register.
  * Appends `t` to `g_thread_list`: `t->next = g_thread_list; g_thread_list = t;`.
  * Calls `taskEXIT_CRITICAL()` to restore interrupt execution.
* **Side Effects & Memory State**: Mutates the head pointer of global linked list `g_thread_list`. Interrupts are temporarily disabled during list insertion.

---

### 2. `find_thread(TaskHandle_t xTask)`
* **Code Signature**: `static posix_thread_t *find_thread(TaskHandle_t xTask)`
* **Objective**: Traverses the global `g_thread_list` registry to find the matching `posix_thread_t` wrapper for a native FreeRTOS `TaskHandle_t`.
* **Inputs**:
  * `xTask` (`TaskHandle_t`): The native FreeRTOS task handle (TCB address pointer).
* **Outputs**:
  * `posix_thread_t *`: Pointer to the matched POSIX thread structure, or `NULL` if not found.
* **Under-the-Hood FreeRTOS Mechanics**:
  * Wraps pointer traversal inside `taskENTER_CRITICAL()` and `taskEXIT_CRITICAL()` to prevent concurrent modification of `g_thread_list` by another thread calling `pthread_create()` or `pthread_exit()`.
* **Side Effects**: None. Read-only registry lookup.

---

### 3. `remove_thread(posix_thread_t *t)`
* **Code Signature**: `static void remove_thread(posix_thread_t *t)`
* **Objective**: Safely unlink a POSIX thread metadata structure (`t`) from the global `g_thread_list` registry.
* **Inputs**:
  * `t` (`posix_thread_t *`): Pointer to the POSIX thread structure to unlink.
* **Outputs**: None (`void`).
* **Under-the-Hood FreeRTOS Mechanics**:
  * Executes linked list pointer unlinking inside a critical section (`taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()`).
* **Side Effects & Memory State**: Modifies `g_thread_list` linked list links. Does not free memory allocated for `t` (caller handles memory reclamation).

---

### 4. `posix_thread_wrapper(void *pvParameters)`
* **Code Signature**: `void posix_thread_wrapper(void *pvParameters)`
* **Objective**: Bridge function serving as the entry point for all native FreeRTOS tasks created via `pthread_create()`.
* **Inputs**:
  * `pvParameters` (`void *`): Pointer to the `posix_thread_t` structure created during `pthread_create()`.
* **Outputs**: None (`void`). Does not return directly.
* **Step-by-Step Logic**:
  1. Casts `pvParameters` to `posix_thread_t *t`.
  2. Executes the user's POSIX function pointer: `void *retval = t->start_routine(t->arg);`.
  3. Immediately passes `retval` to `pthread_exit(retval)`.
* **FreeRTOS Mechanics**: Executes on the dedicated task stack (`PSP`) allocated by `xTaskCreate()`.

---

### 5. `pthread_create(...)`
* **Code Signature**: `int pthread_create(pthread_t *thread, const void *attr, void *(*start_routine)(void *), void *arg)`
* **Objective**: POSIX thread creation shim. Allocates thread metadata, creates a join synchronization semaphore, registers the thread, and spawns a native FreeRTOS task.
* **Inputs**:
  * `thread` (`pthread_t *`): Output pointer to receive the thread handle (`posix_thread_t *`).
  * `attr` (`const void *`): Thread creation attributes (Unused, passed as `NULL`).
  * `start_routine` (`void *(*)(void *)`): User thread entry function.
  * `arg` (`void *`): Parameter passed to entry function.
* **Outputs**:
  * `int`: Returns `0` on success, or `-1` on memory/task creation failure.
* **Step-by-Step Logic**:
  1. Dynamically allocates `sizeof(posix_thread_t)` using `pvPortMalloc()`.
  2. Populates `start_routine`, `arg`, and sets `detached = 0`.
  3. Creates a binary semaphore `t->join_sem = xSemaphoreCreateBinary()`.
  4. Calls `add_thread(t)` to add to global registry.
  5. Calls FreeRTOS `xTaskCreate(posix_thread_wrapper, "POSIX_Task", 1024, t, tskIDLE_PRIORITY + 2, &t->xTask)`.
  6. Assigns `*thread = (pthread_t)t` and returns `0`.
* **FreeRTOS Mechanics**: Allocates task TCB and 1024-word stack space from FreeRTOS heap (`heap_4.c`).

---

### 6. `pthread_join(pthread_t thread, void **retval)`
* **Code Signature**: `int pthread_join(pthread_t thread, void **retval)`
* **Objective**: Blocks the calling thread until the targeted thread finishes execution, retrieves its exit status pointer, and frees thread resources.
* **Inputs**:
  * `thread` (`pthread_t`): Handle of target thread to join.
  * `retval` (`void **`): Address where target thread's return value will be stored (or `NULL`).
* **Outputs**:
  * `int`: `0` on success, `-1` if target thread is invalid or detached.
* **Step-by-Step Logic**:
  1. Casts `thread` to `posix_thread_t *t`. Checks if `t` is valid and non-detached.
  2. Calls `xSemaphoreTake(t->join_sem, portMAX_DELAY)` to block until target calls `pthread_exit()`.
  3. Copies `t->retval` to `*retval` if `retval != NULL`.
  4. Unlinks `t` via `remove_thread(t)`.
  5. Deletes `join_sem` via `vSemaphoreDelete(t->join_sem)`.
  6. Frees dynamic wrapper via `vPortFree(t)` and returns `0`.
* **FreeRTOS Mechanics**: The calling task is moved from `Ready` to `Blocked` list in FreeRTOS kernel until `xSemaphoreGive` is executed by the exiting thread.

---

### 7. `pthread_detach(pthread_t thread)`
* **Code Signature**: `int pthread_detach(pthread_t thread)`
* **Objective**: Marks a thread as detached, so its resources are automatically freed upon exit without requiring another thread to call `pthread_join()`.
* **Inputs**:
  * `thread` (`pthread_t`): Target thread handle.
* **Outputs**:
  * `int`: `0` on success, `-1` if thread invalid.
* **Logic & State Changes**: Sets `t->detached = 1` inside a critical section (`taskENTER_CRITICAL()`).

---

### 8. `pthread_exit(void *retval)`
* **Code Signature**: `void pthread_exit(void *retval)`
* **Objective**: Terminates the calling POSIX thread, saves exit value, signals joiners, or cleans up memory if detached.
* **Inputs**:
  * `retval` (`void *`): Exit status pointer.
* **Outputs**: Does not return (`void`).
* **Step-by-Step Logic**:
  1. Queries current thread metadata: `posix_thread_t *t = find_thread(xTaskGetCurrentTaskHandle())`.
  2. Stores `t->retval = retval`.
  3. If `t->detached == 0`: Executes `xSemaphoreGive(t->join_sem)` to unblock joining threads waiting in `pthread_join()`.
  4. If `t->detached == 1`: Unlinks `remove_thread(t)`, deletes `t->join_sem`, and frees memory `vPortFree(t)`.
  5. Calls FreeRTOS `vTaskDelete(NULL)` to destroy current native task.

---

### 9. `pthread_self(void)`
* **Code Signature**: `pthread_t pthread_self(void)`
* **Objective**: Queries the handle of the currently executing POSIX thread.
* **Inputs**: None.
* **Outputs**: `pthread_t` (Handle of current thread or `NULL`).
* **Mechanics**: Gets `xTaskGetCurrentTaskHandle()` and passes it to `find_thread()`.

---

### 10. `pthread_mutex_init(...)`
* **Code Signature**: `int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr)`
* **Objective**: Initializes a POSIX mutex using a FreeRTOS native mutex.
* **Inputs**: `mutex` (`pthread_mutex_t *`), `attr` (`const void *`, unused).
* **Outputs**: `int` (`0` on success, `-1` if pointer `NULL` or creation failed).
* **FreeRTOS Mechanics**: Calls `xSemaphoreCreateMutex()`, which creates a mutex semaphore featuring **Priority Inheritance**. Casts handle to `pthread_mutex_t`.

---

### 11. `pthread_mutex_lock(pthread_mutex_t *mutex)`
* **Code Signature**: `int pthread_mutex_lock(pthread_mutex_t *mutex)`
* **Objective**: Acquires a POSIX mutex lock, blocking indefinitely if held by another thread.
* **Inputs**: `mutex` (`pthread_mutex_t *`).
* **Outputs**: `int` (`0` on success, `-1` on error).
* **FreeRTOS Mechanics**: Invokes `xSemaphoreTake((SemaphoreHandle_t)*mutex, portMAX_DELAY)`.

---

### 12. `pthread_mutex_unlock(pthread_mutex_t *mutex)`
* **Code Signature**: `int pthread_mutex_unlock(pthread_mutex_t *mutex)`
* **Objective**: Releases a held POSIX mutex lock.
* **Inputs**: `mutex` (`pthread_mutex_t *`).
* **Outputs**: `int` (`0` on success, `-1` on error).
* **FreeRTOS Mechanics**: Invokes `xSemaphoreGive((SemaphoreHandle_t)*mutex)`.

---

### 13. `pthread_mutex_destroy(pthread_mutex_t *mutex)`
* **Code Signature**: `int pthread_mutex_destroy(pthread_mutex_t *mutex)`
* **Objective**: Destroys a POSIX mutex and releases kernel resources.
* **Inputs**: `mutex` (`pthread_mutex_t *`).
* **Outputs**: `int` (`0` on success, `-1` on error).
* **FreeRTOS Mechanics**: Invokes `vSemaphoreDelete((SemaphoreHandle_t)*mutex)` and resets `*mutex = 0`.

---

### 14. `sem_init(...)`
* **Code Signature**: `int sem_init(sem_t *sem, int pshared, unsigned int value)`
* **Objective**: Initializes a POSIX counting semaphore.
* **Inputs**: `sem` (`sem_t *`), `pshared` (`int`, unused), `value` (`unsigned int`, initial count).
* **Outputs**: `int` (`0` on success, `-1` on error).
* **FreeRTOS Mechanics**: Calls `xSemaphoreCreateCounting(65535, value)`. Casts handle to `sem_t`.

---

### 15. `sem_wait(sem_t *sem)`
* **Code Signature**: `int sem_wait(sem_t *sem)`
* **Objective**: Decrements counting semaphore count, blocking if count is `0`.
* **Inputs**: `sem` (`sem_t *`).
* **Outputs**: `int` (`0` on success, `-1` on error).
* **FreeRTOS Mechanics**: Invokes `xSemaphoreTake((SemaphoreHandle_t)*sem, portMAX_DELAY)`.

---

### 16. `sem_post(sem_t *sem)`
* **Code Signature**: `int sem_post(sem_t *sem)`
* **Objective**: Increments counting semaphore count, unblocking waiting tasks.
* **Inputs**: `sem` (`sem_t *`).
* **Outputs**: `int` (`0` on success, `-1` on error).
* **FreeRTOS Mechanics**: Invokes `xSemaphoreGive((SemaphoreHandle_t)*sem)`.

---

### 17. `sem_destroy(sem_t *sem)`
* **Code Signature**: `int sem_destroy(sem_t *sem)`
* **Objective**: Destroys counting semaphore object.
* **Inputs**: `sem` (`sem_t *`).
* **Outputs**: `int` (`0` on success, `-1` on error).
* **FreeRTOS Mechanics**: Calls `vSemaphoreDelete((SemaphoreHandle_t)*sem)` and resets `*sem = NULL`.

---

### 18. `sleep(unsigned int seconds)`
* **Code Signature**: `unsigned int sleep(unsigned int seconds)`
* **Objective**: Suspends task execution for requested seconds.
* **Inputs**: `seconds` (`unsigned int`).
* **Outputs**: `unsigned int` (Always returns `0`).
* **FreeRTOS Mechanics**: Converts seconds to ticks (`pdMS_TO_TICKS(seconds * 1000)`) and calls `vTaskDelay()`. Moves calling task to kernel `xDelayedTaskList`.

---

### 19. `usleep(useconds_t useconds)`
* **Code Signature**: `int usleep(useconds_t useconds)`
* **Objective**: Suspends task execution for requested microseconds.
* **Inputs**: `useconds` (`useconds_t`).
* **Outputs**: `int` (Always returns `0`).
* **FreeRTOS Mechanics**: Calculates `pdMS_TO_TICKS(useconds / 1000)`. Ensures minimum 1-tick delay if `useconds > 0` and calls `vTaskDelay()`.

---

### 20. `worker_thread_mutex(void *arg)`
* **Code Signature**: `void *worker_thread_mutex(void *arg)`
* **Objective**: Test worker task demonstrating thread synchronization and race condition protection across shared variable `g_shared_counter`.
* **Inputs**: `arg` (`void *`): Pointer to `thread_config_t` structure containing thread ID and iterations count.
* **Outputs**: `void *`: Exit status integer `(void*)(uintptr_t)thread_id`.
* **Step-by-Step Logic**:
  1. Casts `arg` to `thread_config_t *config`.
  2. Enters loop running `config->iterations` times.
  3. Locks `pthread_mutex_lock(&g_counter_mutex)`.
  4. Reads `g_shared_counter`, calls `usleep(50000)` (50 ms artificial delay to test mutex hold duration), increments counter `g_shared_counter = temp + 1`.
  5. Unlocks `pthread_mutex_unlock(&g_counter_mutex)`.
  6. Sleeps `usleep(10000)` (10 ms delay).
  7. Frees allocated config `vPortFree(config)` and returns thread ID.

---

### 21. `worker_thread_semaphore(void *arg)`
* **Code Signature**: `void *worker_thread_semaphore(void *arg)`
* **Objective**: Test worker task demonstrating IPC signaling wait.
* **Inputs**: `arg` (`void *`, unused).
* **Outputs**: `void *`: Returns `NULL`.
* **Logic**: Blocks on `sem_wait(&g_job_semaphore)`. Unblocks when `main_posix_app` posts to semaphore, sleeps 1 second, and exits.

---

### 22. `lwip_init_done_callback(void *arg)`
* **Code Signature**: `static void lwip_init_done_callback(void *arg)`
* **Objective**: Callback executed when LwIP `tcpip_init()` finishes starting the core `tcpip_thread`.
* **Inputs**: `arg` (`void *`, pointer to `struct netif`).
* **Outputs**: None (`void`).
* **Logic**: Calls `netif_add()`, `netif_set_default()`, and `netif_set_up()` to activate network interface.

---

### 23. `web_server_thread(void *arg)`
* **Code Signature**: `void *web_server_thread(void *arg)`
* **Objective**: POSIX Socket HTTP Web Server task listening on port 80.
* **Inputs**: `arg` (`void *`, unused).
* **Outputs**: `void *`: Returns `NULL`.
* **Step-by-Step Logic**:
  1. Initializes LwIP TCP/IP stack: `tcpip_init(NULL, NULL)`.
  2. Sets static IP (`10.0.2.15`), netmask (`255.255.255.0`), and gateway (`10.0.2.2`).
  3. Registers network interface: `netif_add(&main_netif, ..., ethernetif_init, tcpip_input)`.
  4. Activates netif: `netif_set_default(&main_netif)` and `netif_set_up(&main_netif)`.
  5. Creates TCP socket: `server_fd = socket(AF_INET, SOCK_STREAM, 0)`.
  6. Binds to port 80: `bind(server_fd, &address, sizeof(address))`.
  7. Listens for connections: `listen(server_fd, 5)`.
  8. Enters infinite loop calling `client_fd = accept(server_fd, ...)` (blocks until HTTP client connects).
  9. Reads HTTP request string (`read(client_fd, buffer, ...)`).
  10. Writes HTML HTTP 200 OK response page (`write(client_fd, response, ...)`).
  11. Closes connection (`close(client_fd)`).

---

### 24. `main_posix_app(void *arg)`
* **Code Signature**: `void *main_posix_app(void *arg)`
* **Objective**: Master POSIX application thread.
* **Inputs**: `arg` (`void *`, unused).
* **Outputs**: `void *`: Returns `NULL`.
* **Step-by-Step Logic**:
  1. Initializes `g_counter_mutex` (`pthread_mutex_init`).
  2. Initializes `g_job_semaphore` (`sem_init`).
  3. Dynamically allocates `thread_config_t` for Worker 1 and Worker 2 via `pvPortMalloc()`.
  4. Spawns Worker 1 and Worker 2 (`pthread_create(&thread1, ..., worker_thread_mutex, config1)`).
  5. Spawns Semaphore Worker (`pthread_create(&sem_thread, ..., worker_thread_semaphore, NULL)`).
  6. Spawns Web Server Thread (`pthread_create(&web_server, ..., web_server_thread, NULL)`).
  7. Sleeps 2 seconds (`sleep(2)`), then posts `sem_post(&g_job_semaphore)` to unblock Semaphore Worker.
  8. Calls `pthread_join()` sequentially on `thread1`, `thread2`, `sem_thread`, and `web_server`.
  9. Verifies `g_shared_counter == 10`.
  10. Destroys mutex and semaphore (`pthread_mutex_destroy`, `sem_destroy`).

---

### 25. `main_blinky(void)`
* **Code Signature**: `void main_blinky(void)`
* **Objective**: Entry point called by `main()` in `main.c`.
* **Inputs**: None.
* **Outputs**: None (`void`).
* **Step-by-Step Logic**:
  1. Prints booting message to UART stdout.
  2. Spawns master POSIX thread `pthread_create(&main_thread, NULL, main_posix_app, NULL)`.
  3. Detaches master thread `pthread_detach(main_thread)`.
  4. Starts real-time kernel scheduler `vTaskStartScheduler()`.

---

# Section 2: How FreeRTOS Works Under-the-Hood for `main_blinky.c`

```
  +-----------------------------------------------------------------------------+
  |                               main_blinky()                                 |
  |  1. Calls pthread_create() -> Allocates posix_thread_t via pvPortMalloc()   |
  |  2. Calls xTaskCreate()    -> Allocates TCB + Stack from ucHeap[heap_4.c]   |
  |  3. Calls vTaskStartScheduler() -> Configures SysTick & transfers to PSP    |
  +-----------------------------------------------------------------------------+
                                       |
                                       v
  +-----------------------------------------------------------------------------+
  |                           FreeRTOS Kernel Engine                            |
  |                                                                             |
  |  [SysTick Interrupt (1 ms)]                                                 |
  |     - Increments xTickCount                                                 |
  |     - Scans xDelayedTaskList for tasks blocked on sleep() / usleep()        |
  |     - Pends PendSV Exception (xPortPendSVHandler) for Context Switch        |
  |                                                                             |
  |  [Context Switching (xPortPendSVHandler)]                                   |
  |     - Saves CPU Registers (R4-R11) to current task PSP stack                |
  |     - Updates pxCurrentTCB->pxTopOfStack                                    |
  |     - Swaps pxCurrentTCB to highest-priority READY task                     |
  |     - Restores R4-R11 from new task PSP stack and executes bx r14           |
  |                                                                             |
  |  [Mutexes & Priority Inheritance]                                           |
  |     - If High Priority Task blocks on pthread_mutex_lock(&g_counter_mutex), |
  |       kernel temporarily elevates holding Low Priority Task's uxPriority    |
  |       to match High Priority Task, eliminating Priority Inversion.          |
  +-----------------------------------------------------------------------------+
```

1. **Task Memory Allocation**:
   When `pthread_create()` runs, it calls `xTaskCreate()`. FreeRTOS allocates a `TCB_t` block and a 1024-word stack array from `ucHeap[]` managed by `heap_4.c`.
2. **Scheduler Startup (`vTaskStartScheduler`)**:
   Sets SysTick timer reload value to match `configCPU_CLOCK_HZ / configTICK_RATE_HZ` (25,000 counts per ms), sets PendSV to lowest priority, configures CPU to use `PSP` for tasks, and executes `svc 0` to jump into the first task.
3. **Task Blocking & Delays**:
   When a thread calls `sleep(2)` or `usleep(50000)`, FreeRTOS executes `vTaskDelay()`. The current task is moved from `pxReadyTasksLists` to `xDelayedTaskList`. The scheduler instantly picks the next highest priority `Ready` task. The sleeping task consumes zero CPU cycles while blocked.
4. **Mutex Priority Inheritance**:
   When `worker_thread_mutex` locks `g_counter_mutex`, FreeRTOS sets the mutex owner in the semaphore structure. If another thread with higher priority attempts `pthread_mutex_lock()`, FreeRTOS temporarily boosts the owner's priority to prevent medium-priority tasks from preempting it.

---

# Section 3: Detailed Working of Supporting Subsystems

---

## 1. `sys_arch.c` (LwIP OS Architecture Layer)
* **Path**: [sys_arch.c](file:///c:/Users/Alok%20Jain/Desktop/STM32/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/sys_arch.c)
* **Detailed Working**: LwIP is written to be OS-agnostic. `sys_arch.c` implements the mandatory wrapper functions that bridge LwIP stack expectations to native FreeRTOS calls:
  * **Semaphores**: `sys_sem_new()` calls `xSemaphoreCreateCounting(65535, count)`. `sys_arch_sem_wait()` calls `xSemaphoreTake()` with tick timeout conversion.
  * **Mutexes**: `sys_mutex_new()` calls `xSemaphoreCreateMutex()`. `sys_mutex_lock()` calls `xSemaphoreTake(..., portMAX_DELAY)`.
  * **Mailboxes (Queues)**: LwIP uses mailboxes (`sys_mbox_t`) for thread-safe message passing between sockets and `tcpip_thread`. `sys_mbox_new()` calls `xQueueCreate(size, sizeof(void*))`. `sys_arch_mbox_fetch()` calls `xQueueReceive()`.
  * **Threads**: `sys_thread_new()` calls `xTaskCreate()`.
  * **Critical Sections**: `sys_arch_protect()` and `sys_arch_unprotect()` execute `taskENTER_CRITICAL()` and `taskEXIT_CRITICAL()`.

---

## 2. `ethernetif.c` (SMSC9118 Network Interface Driver)
* **Path**: [ethernetif.c](file:///c:/Users/Alok%20Jain/Desktop/STM32/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/ethernetif.c)
* **Detailed Working**: Hardware network driver connecting LwIP to the QEMU SMSC9118 Ethernet controller:
  * **Initialization (`ethernetif_init` / `low_level_init`)**: Configures MAC address `00:08:29:11:22:33`, sets MTU to 1500, sets NVIC IRQ 13 priority to `configMAX_SYSCALL_INTERRUPT_PRIORITY`, enables hardware Rx status FIFO level interrupts, and spawns high-priority task `LWIP_RX` (`ethernetif_input_task`).
  * **Interrupt Handling (`EthernetISR`)**: Triggers when NVIC IRQ 13 asserts. Clears interrupt status, disables Rx interrupt, notifies `LWIP_RX` task via `vTaskNotifyGiveFromISR()`, and requests immediate context switch (`portYIELD_FROM_ISR()`).
  * **Packet Reception (`ethernetif_input_task`)**: Unblocks from `ulTaskNotifyTake()`. Peeks packet size from SMSC9118 FIFO (`smsc9220_peek_next_packet_size`), allocates zero-copy buffer `pbuf_alloc()`, reads packet bytes into payload `smsc9220_receive_by_chunks()`, and forwards packet up stack via `netif->input(p, netif)`.
  * **Packet Transmission (`low_level_output`)**: Called by LwIP when transmitting TCP/IP packets. Calls `smsc9220_send_by_chunks()` to stream packet chunks directly into hardware TX FIFO registers.

---

## 3. `lwipopts.h` (LwIP Configuration Header)
* **Path**: [lwipopts.h](file:///c:/Users/Alok%20Jain/Desktop/STM32/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/lwipopts.h)
* **Detailed Working**: Compile-time configuration header customizing LwIP behavior:
  * `NO_SYS 0`: Enables full OS mode with multi-threading support.
  * `LWIP_SOCKET 1` & `LWIP_COMPAT_SOCKETS 1`: Enables standard POSIX socket API function names (`socket`, `bind`, `listen`, `accept`, `read`, `write`, `close`).
  * `MEM_SIZE (16 * 1024)`: Sets LwIP internal dynamic heap allocation memory pool to 16 KB.
  * `PBUF_POOL_SIZE 16`, `PBUF_POOL_BUFSIZE 1514`: Configures packet buffer pool size to fit maximum MTU 1514-byte Ethernet frames without fragmentation.
  * `TCPIP_THREAD_PRIO (configMAX_PRIORITIES - 2)`: Gives high priority to LwIP core stack task.

---

## 4. `arch/cc.h` (Platform Type Specifications)
* **Path**: [arch/cc.h](file:///c:/Users/Alok%20Jain/Desktop/STM32/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/arch/cc.h)
* **Detailed Working**: Specifies platform compiler types and specifications:
  * `BYTE_ORDER LITTLE_ENDIAN`: Configures LwIP to match ARM Cortex-M3 little-endian byte ordering.
  * Data types: Maps `u8_t`, `u16_t`, `u32_t`, `s8_t`, `s16_t`, `s32_t` to standard C `uint8_t`, `uint16_t`, `uint32_t`, etc.
  * `LWIP_PLATFORM_ASSERT(x)`: Configures assertion failure prints to stdout.

---

## 5. `arch/sys_arch.h` (System Architecture Types)
* **Path**: [arch/sys_arch.h](file:///c:/Users/Alok%20Jain/Desktop/STM32/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/arch/sys_arch.h)
* **Detailed Working**: Maps abstract LwIP types to concrete FreeRTOS handles:
  * `typedef SemaphoreHandle_t sys_sem_t;`
  * `typedef SemaphoreHandle_t sys_mutex_t;`
  * `typedef QueueHandle_t sys_mbox_t;`
  * `typedef TaskHandle_t sys_thread_t;`
  * `typedef unsigned long sys_prot_t;`

---

## 6. `Makefile` (GCC Build System Script)
* **Path**: [Makefile](file:///c:/Users/Alok%20Jain/Desktop/STM32/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/build/gcc/Makefile)
* **Detailed Working**: Controls compilation of source code into executable binary `RTOSDemo.out`:
  * Sets cross-compiler `arm-none-eabi-gcc`.
  * Sets CFLAGS: `-mthumb -mcpu=cortex-m3 -ffreestanding -g3 -Os -ffunction-sections -fdata-sections`.
  * Sets LDFLAGS: `-T ./mps2_m3.ld -specs=nano.specs -specs=nosys.specs`.
  * Collects source files across FreeRTOS Core (`tasks.c`, `queue.c`, `heap_4.c`, `port.c`), LwIP 1.4.0 Stack (`init.c`, `sockets.c`, `tcpip.c`, `ip.c`, `etharp.c`), Driver (`smsc9220_eth_drv.c`), and Application (`main.c`, `main_blinky.c`, `sys_arch.c`, `ethernetif.c`).
  * Compiles `.c` files to `.o` object files in `./output/` directory and performs final linking into `RTOSDemo.out`.

---

## 7. `startup_gcc.c` (Vector Table & Exception Handlers)
* **Path**: [startup_gcc.c](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/build/gcc/startup_gcc.c)
* **Detailed Working**: Contains hardware vector table and reset handlers:
  * `isr_vector[]`: Placed in `.isr_vector` section. Contains Initial Stack Pointer (`&_estack`), `Reset_Handler`, FreeRTOS handlers (`vPortSVCHandler`, `xPortPendSVHandler`, `xPortSysTickHandler`), and hardware IRQs.
  * **Ethernet Binding**: Position 78 (IRQ 13) is bound to `EthernetISR`.
  * `Reset_Handler()`: Resets CPU registers and calls `main()`.

---

## 8. `FreeRTOS/Demo/Common/ethernet/lwip-1.4.0` (LwIP Stack Source Tree)
* **Path**: `FreeRTOS/Demo/Common/ethernet/lwip-1.4.0`
* **Detailed Working**: Embedded LwIP 1.4.0 source codebase directory structure:
  * `src/core/`: Contains core protocol implementations (`ip.c`, `tcp.c`, `tcp_in.c`, `tcp_out.c`, `udp.c`, `mem.c`, `memp.c`, `pbuf.c`, `lwip_timers.c`).
  * `src/api/`: Contains sequential and socket API implementations (`sockets.c`, `api_lib.c`, `api_msg.c`, `tcpip.c`).
  * `src/netif/`: Network interface implementations (`etharp.c`, `ethernetif.c`).
  * `src/include/`: Standard headers for LwIP stack protocols and APIs (`lwip/sockets.h`, `lwip/ip_addr.h`, `arch/perf.h`).
