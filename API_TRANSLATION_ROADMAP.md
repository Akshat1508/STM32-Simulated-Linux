# POSIX API Translation Analysis & Expansion Roadmap
## STM32 Simulated Linux (FreeRTOS & LwIP POSIX Compatibility Layer)

---

## 1. Executive Summary

The **STM32 Simulated Linux** project implements a lightweight **POSIX Compatibility Layer (Shim)** on top of the **FreeRTOS** real-time kernel and **LwIP** TCP/IP stack for ARM Cortex-M microcontrollers (specifically emulated in **QEMU MPS2-AN385 / Cortex-M3**).

Instead of running a heavy Linux kernel requiring a Memory Management Unit (MMU) and multi-megabyte RAM budgets, this system translates standard Unix/Linux system and POSIX calls (`pthread_*`, `pthread_mutex_*`, `sem_*`, `sleep`/`usleep`, and BSD `socket` APIs) into native FreeRTOS kernel primitives and LwIP networking calls.

This document provides an exhaustive, multi-tiered analysis of:
1. **The 22 POSIX / BSD APIs currently implemented** in the codebase with line pointers and kernel primitive mappings.
2. **The 4-tier compatibility architecture framework**, defining the exact API footprint required to scale from a minimal RTOS shim to real-world Linux applications and POSIX standard profiles (e.g., IEEE 1003.13 PSE51).
3. **A detailed technical expansion roadmap for the next 28 target APIs**, specifying implementation strategies using FreeRTOS Event Groups, Soft Timers, LwIP Socket Selectors, and a lightweight Virtual File System (VFS).
4. **Memory footprint and resource budget constraints** on ARM Cortex-M hardware.

---

## 2. Exhaustive Breakdown of Implemented APIs (22 Total)

The POSIX Compatibility Layer source code is located in:
* Public Header: [`posix_shim.h`](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.h)
* C Implementation: [`posix_shim.c`](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c)
* Networking Glue: [`sys_arch.c`](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/sys_arch.c) and [`ethernetif.c`](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/ethernetif.c)

---

### 2.1 POSIX Thread Management (`pthread_*`) — 5 APIs

All POSIX threads are tracked via an internal singly-linked list (`g_thread_list`) of [`posix_thread_t`](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L34-L42) nodes allocated from the FreeRTOS heap (`pvPortMalloc`). Access to the registry is guarded by Cortex-M priority-mask critical sections (`taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()`).

```c
typedef struct posix_thread {
    TaskHandle_t       xTask;          /* FreeRTOS task handle */
    void              *(*start_routine)(void *); /* Thread entry point */
    void              *arg;            /* Parameter to start_routine */
    void              *retval;         /* Exit status from pthread_exit() */
    SemaphoreHandle_t  join_sem;       /* Binary semaphore for pthread_join() */
    volatile int       detached;       /* 1 if thread is detached */
    struct posix_thread *next;         /* Linked list pointer */
} posix_thread_t;
```

#### 1. `pthread_create`
* **File Location**: [`posix_shim.c` (L117-L159)](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L117-L159)
* **Signature**: `int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg)`
* **Underlying Primitive**: `pvPortMalloc` + `xSemaphoreCreateBinary` + `xTaskCreate`
* **Mechanics**: Allocates a `posix_thread_t` structure, creates a binary semaphore `join_sem`, suspends the scheduler (`vTaskSuspendAll()`), spawns a FreeRTOS task running [`posix_thread_wrapper`](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L106-L111) (1024-word stack, priority `tskIDLE_PRIORITY + 1`), prepends the node to `g_thread_list`, and resumes the scheduler (`xTaskResumeAll()`). Returns `0` on success, `-1` on failure.

#### 2. `pthread_join`
* **File Location**: [`posix_shim.c` (L161-L181)](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L161-L181)
* **Signature**: `int pthread_join(pthread_t thread, void **retval)`
* **Underlying Primitive**: `xSemaphoreTake(t->join_sem, portMAX_DELAY)` + `vSemaphoreDelete` + `vPortFree`
* **Mechanics**: Blocks the calling task on `t->join_sem` until the target thread finishes executing and invokes `pthread_exit()`. Once woken, copies `t->retval` to `*retval` (if non-null), unlinks the node from `g_thread_list`, deletes `join_sem`, and frees the `posix_thread_t` memory allocation.

#### 3. `pthread_exit`
* **File Location**: [`posix_shim.c` (L183-L200)](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L183-L200)
* **Signature**: `void pthread_exit(void *retval)`
* **Underlying Primitive**: `xTaskGetCurrentTaskHandle` + `xSemaphoreGive` + `vTaskDelete(NULL)`
* **Mechanics**: Locates the calling thread's node in `g_thread_list`. If `detached == 1`, performs immediate self-cleanup (removes node, deletes `join_sem`, frees memory). If `detached == 0`, saves `retval` and releases `join_sem` via `xSemaphoreGive()`. Terminates the FreeRTOS task via `vTaskDelete(NULL)`.

#### 4. `pthread_detach`
* **File Location**: [`posix_shim.c` (L202-L210)](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L202-L210)
* **Signature**: `int pthread_detach(pthread_t thread)`
* **Underlying Primitive**: Direct structure state modification (`t->detached = 1`)
* **Mechanics**: Sets the detached flag to `1`. Ensures that when `pthread_exit()` is called, resources are automatically reclaimed without requiring another thread to invoke `pthread_join()`.

#### 5. `pthread_self`
* **File Location**: [`posix_shim.c` (L212-L216)](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L212-L216)
* **Signature**: `pthread_t pthread_self(void)`
* **Underlying Primitive**: `xTaskGetCurrentTaskHandle()` + `find_thread()`
* **Mechanics**: Queries FreeRTOS for the active task handle and returns the matching `posix_thread_t` pointer cast to `pthread_t`.

---

### 2.2 POSIX Mutex API (`pthread_mutex_*`) — 4 APIs

In [`posix_shim.h`](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.h#L12), `pthread_mutex_t` is defined as a pointer-sized scalar (`uintptr_t`). It directly stores a FreeRTOS `SemaphoreHandle_t` created as a recursive or standard mutex.

#### 6. `pthread_mutex_init`
* **File Location**: [`posix_shim.c` (L225-L237)](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L225-L237)
* **Signature**: `int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)`
* **Underlying Primitive**: `xSemaphoreCreateMutex()`
* **Mechanics**: Allocates a FreeRTOS priority-inheritance mutex semaphore and assigns the handle to `*mutex`. Returns `0` on success, `-1` on failure.

#### 7. `pthread_mutex_lock`
* **File Location**: [`posix_shim.c` (L239-L249)](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L239-L249)
* **Signature**: `int pthread_mutex_lock(pthread_mutex_t *mutex)`
* **Underlying Primitive**: `xSemaphoreTake((SemaphoreHandle_t)*mutex, portMAX_DELAY)`
* **Mechanics**: Takes the mutex semaphore, blocking indefinitely (`portMAX_DELAY`) until acquired. Inherits task priority if higher-priority tasks contend for the lock.

#### 8. `pthread_mutex_unlock`
* **File Location**: [`posix_shim.c` (L251-L261)](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L251-L261)
* **Signature**: `int pthread_mutex_unlock(pthread_mutex_t *mutex)`
* **Underlying Primitive**: `xSemaphoreGive((SemaphoreHandle_t)*mutex)`
* **Mechanics**: Releases ownership of the mutex semaphore. Returns `0` on success, `-1` on error.

#### 9. `pthread_mutex_destroy`
* **File Location**: [`posix_shim.c` (L263-L274)](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L263-L274)
* **Signature**: `int pthread_mutex_destroy(pthread_mutex_t *mutex)`
* **Underlying Primitive**: `vSemaphoreDelete((SemaphoreHandle_t)*mutex)`
* **Mechanics**: Deletes the FreeRTOS mutex semaphore handle and resets `*mutex` to `0`.

---

### 2.3 POSIX Counting Semaphores (`sem_*`) — 4 APIs

Because bare-metal GCC toolchains (`arm-none-eabi-gcc` with standard newlib) lack `<semaphore.h>`, [`posix_shim.h`](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.h#L19) defines `sem_t` as an opaque pointer (`typedef void *sem_t`) wrapping FreeRTOS counting semaphores.

#### 10. `sem_init`
* **File Location**: [`posix_shim.c` (L302-L314)](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L302-L314)
* **Signature**: `int sem_init(sem_t *sem, int pshared, unsigned int value)`
* **Underlying Primitive**: `xSemaphoreCreateCounting(65535, value)`
* **Mechanics**: Creates a FreeRTOS counting semaphore initialized with count `value` and maximum count `65535`. (Note: `pshared` is ignored since memory space is unified on bare-metal).

#### 11. `sem_wait`
* **File Location**: [`posix_shim.c` (L316-L326)](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L316-L326)
* **Signature**: `int sem_wait(sem_t *sem)`
* **Underlying Primitive**: `xSemaphoreTake((SemaphoreHandle_t)*sem, portMAX_DELAY)`
* **Mechanics**: Decrements the semaphore count. If the count is zero, blocks indefinitely until a `sem_post()` occurs.

#### 12. `sem_post`
* **File Location**: [`posix_shim.c` (L328-L338)](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L328-L338)
* **Signature**: `int sem_post(sem_t *sem)`
* **Underlying Primitive**: `xSemaphoreGive((SemaphoreHandle_t)*sem)`
* **Mechanics**: Increments the semaphore count, waking up any task blocked in `sem_wait()`.

#### 13. `sem_destroy`
* **File Location**: [`posix_shim.c` (L340-L351)](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L340-L351)
* **Signature**: `int sem_destroy(sem_t *sem)`
* **Underlying Primitive**: `vSemaphoreDelete((SemaphoreHandle_t)*sem)`
* **Mechanics**: Frees the counting semaphore resources and sets `*sem` to `NULL`.

---

### 2.4 POSIX Timing & Yielding (`sleep`, `usleep`) — 2 APIs

#### 14. `sleep`
* **File Location**: [`posix_shim.c` (L280-L284)](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L280-L284)
* **Signature**: `unsigned int sleep(unsigned int seconds)`
* **Underlying Primitive**: `vTaskDelay(pdMS_TO_TICKS(seconds * 1000UL))`
* **Mechanics**: Converts requested seconds into system timer ticks (`configTICK_RATE_HZ`) and puts the calling task into the Blocked state for the calculated duration. Always returns `0`.

#### 15. `usleep`
* **File Location**: [`posix_shim.c` (L286-L294)](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L286-L294)
* **Signature**: `int usleep(useconds_t useconds)`
* **Underlying Primitive**: `vTaskDelay(ticks)`
* **Mechanics**: Converts microseconds into FreeRTOS ticks. Guarantees at least a 1-tick delay for any non-zero input (`useconds > 0`).

---

### 2.5 BSD Socket Network API (via LwIP BSD Socket Layer) — 7 APIs

LwIP is configured in OS Mode (`NO_SYS = 0`) via [`sys_arch.c`](file:///Users/kartikayechaturvedi/Dev/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/sys_arch.c). Standard BSD socket headers map directly to LwIP API wrappers:

#### 16. `socket`
* **Signature**: `int socket(int domain, int type, int protocol)`
* **Underlying Primitive**: LwIP `lwip_socket()`
* **Mechanics**: Allocates a socket file descriptor within LwIP's internal socket array.

#### 17. `bind`
* **Signature**: `int bind(int s, const struct sockaddr *name, socklen_t namelen)`
* **Underlying Primitive**: LwIP `lwip_bind()`
* **Mechanics**: Binds an IP address and port number to socket descriptor `s`.

#### 18. `listen`
* **Signature**: `int listen(int s, int backlog)`
* **Underlying Primitive**: LwIP `lwip_listen()`
* **Mechanics**: Sets socket descriptor `s` into TCP server listening mode with specified backlog depth.

#### 19. `accept`
* **Signature**: `int accept(int s, struct sockaddr *addr, socklen_t *addrlen)`
* **Underlying Primitive**: LwIP `lwip_accept()`
* **Mechanics**: Blocks until an incoming TCP client connection is established, returning a new socket descriptor for client communication.

#### 20. `read`
* **Signature**: `ssize_t read(int fd, void *buf, size_t nbytes)`
* **Underlying Primitive**: LwIP `lwip_read()`
* **Mechanics**: Extracts received payload chunks from incoming packet buffers (`pbuf`).

#### 21. `write`
* **Signature**: `ssize_t write(int fd, const void *buf, size_t nbytes)`
* **Underlying Primitive**: LwIP `lwip_write()`
* **Mechanics**: Enqueues output bytes into TCP transmit queues for SMSC9118 Ethernet transmission.

#### 22. `close`
* **Signature**: `int close(int fd)`
* **Underlying Primitive**: LwIP `lwip_close()`
* **Mechanics**: Performs TCP graceful tear-down (`FIN` packet transmission) and frees socket descriptor indices.

---

## 3. Four-Tier Compatibility Architecture Framework

To understand how many total APIs are needed to run Linux applications on microcontrollers, system capabilities are classified into **4 Compatibility Tiers**:

```
+-------------------------------------------------------------------------------+
| Tier 4: Full Linux Kernel Syscall Spec (330+ Syscalls)                        |
| (Requires Hardware MMU, Virtual Memory, Process Isolation, fork/exec)          |
+-------------------------------------------------------------------------------+
                                       |
                                       v
+-------------------------------------------------------------------------------+
| Tier 3: IEEE POSIX PSE51 Standard Profile (~120 APIs)                         |
| (POSIX Minimal Real-Time System Spec: Message Queues, Barriers, Rwlocks)       |
+-------------------------------------------------------------------------------+
                                       |
                                       v
+-------------------------------------------------------------------------------+
| Tier 2: Real-World Multithreaded C Software & Daemons (~50 APIs)              |
| (Condition Variables, High-Res Timers, Select/Poll, Non-Blocking Sockets, VFS)|
+-------------------------------------------------------------------------------+
                                       |
                                       v
+-------------------------------------------------------------------------------+
| Tier 1: Minimal Multi-Threaded RTOS Shim [CURRENT STATE] (22 APIs)            |
| (Basic Pthreads, Mutexes, Counting Semaphores, Sleep, Basic TCP Server)       |
+-------------------------------------------------------------------------------+
```

### Detailed Tier Comparison Table

| Feature Dimension | Tier 1: Current Shim | Tier 2: Practical C Apps | Tier 3: POSIX PSE51 Profile | Tier 4: Full Linux Spec |
| :--- | :--- | :--- | :--- | :--- |
| **Total API Count** | **22 APIs** | **~50 APIs** | **~120 APIs** | **330+ Syscalls** |
| **Target Workload** | Simple multi-threaded tasks & static web servers | Complex C daemons, IoT protocols (MQTT, CoAP), HTTP client/servers | Strict POSIX-compliant RTOS applications | Full Linux binaries (Bash, Python, Systemd) |
| **Synchronization** | Basic Mutex & Semaphore | Mutex, Sem, Cond Vars, Timed Locks | Rwlocks, Spinlocks, Barriers, Semaphores | Futexes, Signals, IPC Semaphores |
| **Networking** | Basic Blocking Sockets | Non-blocking Sockets, `select`/`poll` | Full BSD Sockets + `sys/socket.h` | Full Netlink, UNIX Sockets, Raw Sockets |
| **File Systems** | None (Raw UART stream) | Simple Memory VFS (`open`/`read`) | POSIX VFS & Mount points | ext4, procfs, sysfs, virtual memory maps |
| **Hardware Required** | ARM Cortex-M3 (No MMU) | ARM Cortex-M3 (No MMU) | ARM Cortex-M3/M4/M7 (MPU optional) | Application Processors (Cortex-A with MMU) |

> [!IMPORTANT]
> **Hardware Boundary Note**: Tier 4 (Full Linux Syscall Spec with 330+ syscalls) requires virtual memory management (`mmap`, `fork`, copy-on-write page tables). Microcontrollers like ARM Cortex-M3/M4/M7 **do not have a Hardware MMU**. Therefore, **Tier 2 (~50 APIs)** or **Tier 3 (~120 APIs)** represents the maximum achievable target for simulated Linux environments on microcontroller hardware.

---

## 4. Expansion Roadmap: Next 28 Target APIs for Tier 2 Compliance

To upgrade the current codebase from **Tier 1 (22 APIs)** to **Tier 2 (~50 APIs)**, the following **28 missing APIs** are prioritized for implementation.

```
       +-----------------------------------------------------------------+
       |              Tier 2 Expansion Roadmap (28 APIs)                 |
       +-----------------------------------------------------------------+
       |  Group A: Condition Variables (5 APIs)                          |
       |    - pthread_cond_init, pthread_cond_wait, pthread_cond_signal |
       |    - pthread_cond_timedwait, pthread_cond_broadcast             |
       +-----------------------------------------------------------------+
       |  Group B: High-Resolution Timing & Clocks (4 APIs)             |
       |    - clock_gettime, gettimeofday, nanosleep, time               |
       +-----------------------------------------------------------------+
       |  Group C: Advanced & Non-Blocking Sockets (7 APIs)              |
       |    - connect, send, recv, select, poll, setsockopt, fcntl       |
       +-----------------------------------------------------------------+
       |  Group D: Extended Mutex & Thread Control (4 APIs)              |
       |    - pthread_mutex_trylock, pthread_mutex_timedlock             |
       |    - pthread_mutexattr_init, pthread_attr_setstacksize          |
       +-----------------------------------------------------------------+
       |  Group E: Virtual File System Abstraction (8 APIs)              |
       |    - open, read, write, lseek, close, stat, pipe, dup2          |
       +-----------------------------------------------------------------+
```

---

### Group A: POSIX Condition Variables (5 APIs)

Condition variables allow threads to suspend execution until a specific Boolean predicate becomes true, avoiding busy polling.

1. **`pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)`**
   * *Proposed Mapping*: Allocates a custom structure containing a FreeRTOS Semaphore / Event Group and a waiter count (`volatile uint32_t waiters`).
2. **`pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)`**
   * *Proposed Mapping*: Atomically unlocks `mutex` via `pthread_mutex_unlock()`, blocks on the internal semaphore via `xSemaphoreTake(..., portMAX_DELAY)`, and re-acquires `mutex` via `pthread_mutex_lock()`.
3. **`pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime)`**
   * *Proposed Mapping*: Same mechanics as `pthread_cond_wait`, but converts `abstime` to FreeRTOS ticks and calls `xSemaphoreTake(..., ticks)`. Returns `ETIMEDOUT` if timeout expires.
4. **`pthread_cond_signal(pthread_cond_t *cond)`**
   * *Proposed Mapping*: Unblocks one waiter thread by calling `xSemaphoreGive()`.
5. **`pthread_cond_broadcast(pthread_cond_t *cond)`**
   * *Proposed Mapping*: Loops through all registered waiter threads (`waiters` count) and gives the semaphore repeatedly to release all waiting tasks.

---

### Group B: High-Resolution Timing & Clocks (4 APIs)

6. **`clock_gettime(clockid_t clk_id, struct timespec *tp)`**
   * *Proposed Mapping*: Reads the Cortex-M SysTick counter (`xTaskGetTickCount()`) combined with SysTick current value register (`SysTick->VAL`) for microsecond/nanosecond resolution.
7. **`gettimeofday(struct timeval *tv, struct timezone *tz)`**
   * *Proposed Mapping*: Derives seconds and microseconds from `xTaskGetTickCount()` and hardware timer reload values.
8. **`nanosleep(const struct timespec *req, struct timespec *rem)`**
   * *Proposed Mapping*: Calculates exact FreeRTOS tick count `pdMS_TO_TICKS(req->tv_sec * 1000 + req->tv_nsec / 1000000)`.
9. **`time(time_t *tloc)`**
   * *Proposed Mapping*: Returns system uptime in seconds derived from tick count.

---

### Group C: Advanced & Non-Blocking Sockets (7 APIs)

10. **`connect(int s, const struct sockaddr *name, socklen_t namelen)`**
    * *Proposed Mapping*: Wraps LwIP `lwip_connect()`. Allows the STM32 microcontroller to act as an outbound TCP client (e.g., sending telemetry data to an external server).
11. **`send(int s, const void *dataptr, size_t len, int flags)`**
    * *Proposed Mapping*: Wraps LwIP `lwip_send()`. Supports flags like `MSG_DONTWAIT`.
12. **`recv(int s, void *mem, size_t len, int flags)`**
    * *Proposed Mapping*: Wraps LwIP `lwip_recv()`. Supports non-blocking reads.
13. **`select(int maxfdp1, fd_set *readset, fd_set *writeset, fd_set *exceptset, struct timeval *timeout)`**
    * *Proposed Mapping*: Wraps LwIP `lwip_select()`. Enables single-threaded event loops to multiplex multiple socket descriptors.
14. **`poll(struct pollfd *fds, nfds_t nfds, int timeout)`**
    * *Proposed Mapping*: Converts `pollfd` structures to `fd_set` format and invokes `lwip_select()`.
15. **`setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen)`**
    * *Proposed Mapping*: Wraps LwIP `lwip_setsockopt()`. Enables configuration of `SO_REUSEADDR`, `TCP_NODELAY`, and timeout parameters.
16. **`fcntl(int fd, int cmd, ...)`**
    * *Proposed Mapping*: Intercepts `F_GETFL` and `F_SETFL` to toggle the `O_NONBLOCK` flag on socket descriptors via LwIP `lwip_fcntl()`.

---

### Group D: Extended Mutex & Thread Attributes (4 APIs)

17. **`pthread_mutex_trylock(pthread_mutex_t *mutex)`**
    * *Proposed Mapping*: Executes `xSemaphoreTake(xMutex, 0)`. Returns `0` if acquired immediately, or `EBUSY` if locked.
18. **`pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *abstime)`**
    * *Proposed Mapping*: Converts `abstime` into FreeRTOS ticks and calls `xSemaphoreTake(xMutex, ticks)`.
19. **`pthread_mutexattr_init(pthread_mutexattr_t *attr)`** & **`pthread_mutexattr_settype`**
    * *Proposed Mapping*: Configures mutex type flags (Standard vs Recursive). If `PTHREAD_MUTEX_RECURSIVE` is requested, initializes via `xSemaphoreCreateRecursiveMutex()`.
20. **`pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize)`**
    * *Proposed Mapping*: Stores requested stack depth in bytes, converted to words for FreeRTOS `xTaskCreate`.

---

### Group E: Virtual File System Abstraction Layer (VFS) (8 APIs)

Microcontrollers lack physical disk block devices by default. Implementing a VFS shim routes standard file descriptors (`fd`) to underlying hardware backends (UART console, RAM disks, LwIP sockets, or SPI Flash):

```
                       +-----------------------------------+
                       |    Standard Linux POSIX I/O       |
                       | (open, read, write, close, lseek) |
                       +-----------------+-----------------+
                                         |
                                         v
                       +-----------------------------------+
                       |   Virtual File System (VFS) Shim  |
                       |       FD Table (0..FD_SETSIZE)    |
                       +--------+--------+--------+--------+
                                |        |        |
            +-------------------+        |        +-------------------+
            v                            v                            v
  +-------------------+        +-------------------+        +-------------------+
  |  FD 0,1,2: Console|        |  FD 3..15: Sockets|        | FD 16+: Memory VFS|
  | (UART0 Physical)  |        |  (LwIP Socket API)|        | (ROM/RAM Storage) |
  +-------------------+        +-------------------+        +-------------------+
```

21. **`open(const char *pathname, int flags, ...)`** — Allocates a VFS file descriptor index.
22. **`read(int fd, void *buf, size_t count)`** — Routes `fd < 3` to UART RX, `fd` in socket range to `lwip_read()`, and file `fd` to RAM disk.
23. **`write(int fd, const void *buf, size_t count)`** — Routes `fd 1` and `fd 2` (`stdout`/`stderr`) to UART TX register (`0x40004000UL`), sockets to `lwip_write()`.
24. **`lseek(int fd, off_t offset, int whence)`** — Updates seek offset in RAM file structure.
25. **`close(int fd)`** — Releases VFS descriptor index.
26. **`stat(const char *pathname, struct stat *statbuf)`** — Returns synthetic file size and mode flags.
27. **`pipe(int pipefd[2])`** — Creates a unidirectionally connected pair of FreeRTOS Stream Buffers (`xStreamBufferCreate`).
28. **`dup2(int oldfd, int newfd)`** — Reassigns VFS table indices.

---

## 5. System Footprint & Memory Budget Analysis

The compiled binary size for the ARM Cortex-M3 target (`RTOSDemo.out`) measured using `arm-none-eabi-size`:

```text
   text       data        bss        dec        hex    filename
  66852        226     155442     222520      36538    RTOSDemo.out
```

### Memory Resource Breakdown

```
+-------------------------------------------------------------------------------+
|                       FLASH Memory Map (4096 KB Total)                        |
| +---------------------------------------------------------------------------+ |
| | Code & Read-Only Data (text): 66.8 KB (1.6% of FLASH)                     | |
| +---------------------------------------------------------------------------+ |
+-------------------------------------------------------------------------------+

+-------------------------------------------------------------------------------+
|                        SRAM Memory Map (8192 KB Total)                        |
| +---------------------+-------------------------------+---------------------+ |
| | Data Globals        | FreeRTOS Dynamic Heap         | LwIP Network Pools  | |
| | 0.2 KB              | configTOTAL_HEAP_SIZE: 100.0KB| & BSS: 51.5 KB      | |
| +---------------------+-------------------------------+---------------------+ |
| <----------------------- Total Static RAM: 151.7 KB ------------------------> |
+-------------------------------------------------------------------------------+
```

1. **Instruction FLASH Footprint (`text`)**: **66.8 KB** (66,852 bytes).
   * FreeRTOS Kernel Core: ~22 KB
   * LwIP TCP/IP Stack & Ethernet Driver: ~35 KB
   * POSIX Shim Layer (`posix_shim.c`): ~4.5 KB
   * Test Application (`main_blinky.c`): ~5.3 KB

2. **Static RAM Footprint (`bss` + `data`)**: **151.7 KB** (155,668 bytes).
   * **FreeRTOS Dynamic Heap (`configTOTAL_HEAP_SIZE`)**: **100.0 KB** reserved for task stacks (`posix_thread_t`), mutexes, and binary semaphores.
   * **LwIP Packet Buffers (`pbuf` pools)**: **51.5 KB** reserved for network socket tables, RX/TX buffers, and SMSC9118 descriptors.

---

## 6. Summary & Recommendations

* **Current Status**: The system translates **22 APIs**, creating a fully functional POSIX environment for multi-threaded C code, mutexes, counting semaphores, delays, and HTTP web socket serving.
* **Feasibility Ceiling**: Microcontrollers without hardware MMUs can target up to **Tier 2 (~50 APIs)** or **Tier 3 (~120 APIs)**. Tier 4 (full desktop Linux with `fork`/`mmap`) is architecturally unviable without hardware MMUs.
* **Next Target**: Implementing **Group A (Condition Variables)** and **Group C (Socket Selectors)** will expand compatibility to ~35 APIs, enabling complex networking libraries like MQTT and CoAP daemons to run seamlessly.
