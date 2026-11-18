# STM32 Simulated Linux: Detailed Immediate Course of Action
**Date:** November 18, 2026  
**Document Reference:** `Documentations/CourseOfAction181126.md`  
**Target Milestone:** Transition from Tier 1 (22 Active APIs) to Tier 2 (~50 APIs)  
**Execution Environment:** ARM Cortex-M3 (MPS2 AN385) in QEMU with FreeRTOS V202212.00 & LwIP 1.4.0  

---

## 1. Context & Objectives

The **STM32 Simulated Linux** platform has stabilized at **Tier 1 POSIX Compliance (22 APIs)**, featuring:
- POSIX thread lifecycle (`pthread_create`, `join`, `exit`, `detach`, `self`).
- Mutual exclusion locks (`pthread_mutex_*`) with priority inheritance.
- Counting semaphores (`sem_*`).
- Standard millisecond/tick timing (`sleep`, `usleep`).
- BSD Sockets in OS Mode (`socket`, `bind`, `listen`, `accept`, `read`, `write`, `close`).
- QEMU LAN9118 interrupt-driven network driver (`ethernetif.c`) and demo HTTP server on port 80/8080.
- Flash code footprint of ~66.8 KB and RAM footprint of ~151.7 KB.

### Primary Goal of this Immediate Plan
To advance the compatibility layer towards **Tier 2 Compliance (~50 APIs)**. This enables running real-world multithreaded Linux daemons, IoT client protocols (MQTT, CoAP, HTTP Client), and non-blocking I/O event loops on microcontroller hardware without an MMU.

---

## 2. Immediate Work Breakdown: 5 Strategic Milestones

```
+----------------------------------------------------------------------------------------+
|                      TIER 2 EXPANSION ROADMAP - IMMEDIATE PLAN                         |
+----------------------------------------------------------------------------------------+
|  Milestone 1: Condition Variables (Group A - 5 APIs)                                   |
|    pthread_cond_init, wait, timedwait, signal, broadcast                                |
+----------------------------------------------------------------------------------------+
|  Milestone 2: Outbound Client & Non-Blocking Sockets (Group C - 7 APIs)                |
|    connect, send, recv, select, poll, setsockopt, fcntl                                |
+----------------------------------------------------------------------------------------+
|  Milestone 3: High-Resolution Clocks & Timers (Group B - 4 APIs)                       |
|    clock_gettime, gettimeofday, nanosleep, time                                        |
+----------------------------------------------------------------------------------------+
|  Milestone 4: Extended Mutex & Attributes (Group D - 4 APIs)                           |
|    pthread_mutex_trylock, timedlock, pthread_mutexattr_init, pthread_attr_setstacksize |
+----------------------------------------------------------------------------------------+
|  Milestone 5: Virtual File System (VFS) Prototype (Group E - 8 APIs)                   |
|    Unified FD Table: UART0 (0,1,2), Sockets (3..15), In-Memory Buffers/Pipes           |
+----------------------------------------------------------------------------------------+
```

---

### Milestone 1: POSIX Condition Variables (Group A — 5 APIs)
*Target Files:* [`FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.h`](../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.h), [`posix_shim.c`](../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c)

Condition variables enable threads to safely suspend execution until a shared predicate changes, preventing CPU waste from busy polling.

#### 1. Data Structure Design
```c
typedef struct posix_cond_waiter {
    SemaphoreHandle_t sem;
    struct posix_cond_waiter *next;
} posix_cond_waiter_t;

typedef struct {
    posix_cond_waiter_t *waiters_head;
    posix_cond_waiter_t *waiters_tail;
    SemaphoreHandle_t lock; /* Protects internal waiter queue */
} posix_cond_t;

typedef uintptr_t pthread_cond_t;
typedef void pthread_condattr_t;
```

#### 2. Functional Implementations
1. **`pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)`**:
   - Allocate `posix_cond_t` from FreeRTOS heap (`pvPortMalloc`).
   - Create binary or counting mutex `lock` to guard the waiter linked list.
2. **`pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)`**:
   - Create local binary semaphore for the calling thread.
   - Enqueue waiter onto `cond->waiters_tail` under `cond->lock`.
   - Atomically release user mutex: `pthread_mutex_unlock(mutex)`.
   - Block indefinitely on thread-specific semaphore: `xSemaphoreTake(waiter.sem, portMAX_DELAY)`.
   - Re-acquire user mutex before returning: `pthread_mutex_lock(mutex)`.
   - Free waiter resources.
3. **`pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime)`**:
   - Same flow as `wait`, but converts `abstime` delta to system ticks (`pdMS_TO_TICKS`).
   - Calls `xSemaphoreTake(waiter.sem, ticks)`. If timeout expires, removes node from list and returns `ETIMEDOUT`.
4. **`pthread_cond_signal(pthread_cond_t *cond)`**:
   - Dequeues the first waiter node from `cond->waiters_head`.
   - Wakes the waiting thread via `xSemaphoreGive(waiter->sem)`.
5. **`pthread_cond_broadcast(pthread_cond_t *cond)`**:
   - Dequeues all waiter nodes in a loop, unblocking every waiting task.
6. **`pthread_cond_destroy(pthread_cond_t *cond)`**:
   - Asserts waiter queue is empty, deletes `cond->lock`, and frees memory.

#### 3. Verification Test in `main_blinky.c`
- Implement a classic **Bounded Buffer Producer-Consumer** task pair:
  - Producer generates data tokens and signals `pthread_cond_signal`.
  - Consumer waits on `pthread_cond_wait` until buffer is non-empty.

---

### Milestone 2: Outbound Client & Non-Blocking Sockets (Group C — 7 APIs)
*Target Files:* [`posix_shim.h`](../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.h), [`posix_shim.c`](../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c), [`lwipopts.h`](../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/lwipopts.h)

Currently, only server sockets in blocking mode are demonstrated (`listen`/`accept`). Adding client and multiplexing primitives allows bidirectional networking.

#### 1. API Extensions
1. **`connect(int s, const struct sockaddr *name, socklen_t namelen)`**:
   - Wrapper for `lwip_connect()`. Allows STM32 tasks to initiate outbound TCP connections to external services.
2. **`send(int s, const void *dataptr, size_t len, int flags)`** & **`recv(int s, void *mem, size_t len, int flags)`**:
   - Wrappers for `lwip_send()` and `lwip_recv()`, supporting `MSG_DONTWAIT` and `MSG_PEEK`.
3. **`fcntl(int fd, int cmd, ...)`**:
   - Intercepts `F_GETFL` and `F_SETFL` commands to toggle `O_NONBLOCK` using `lwip_fcntl()`.
4. **`setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen)`**:
   - Exposes `lwip_setsockopt()` for configuring `SO_REUSEADDR`, `SO_RCVTIMEO`, `SO_SNDTIMEO`, and `TCP_NODELAY`.
5. **`select(int maxfdp1, fd_set *readset, fd_set *writeset, fd_set *exceptset, struct timeval *timeout)`**:
   - Maps directly to `lwip_select()`. Enables single-threaded event-driven networking loops (e.g., managing multiple client connections simultaneously without dedicated threads).
6. **`poll(struct pollfd *fds, nfds_t nfds, int timeout)`**:
   - Lightweight shim translating `struct pollfd` array into `fd_set` bitmasks for `lwip_select()`.

#### 2. Verification Test in `main_blinky.c`
- **Outbound HTTP Client / Echo Client**: A worker thread establishes a TCP client connection via `connect()`, transmits an HTTP request or ping, and reads the response using non-blocking `select()` + `recv()`.

---

### Milestone 3: High-Resolution Clocks & Timers (Group B — 4 APIs)
*Target Files:* [`posix_shim.h`](../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.h), [`posix_shim.c`](../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c)

#### 1. Hardware-Assisted Microsecond Resolution
FreeRTOS `xTaskGetTickCount()` provides 1 ms resolution (at 1000 Hz `configTICK_RATE_HZ`). By sampling the ARM Cortex-M SysTick current value register (`SysTick->VAL`), microsecond and sub-microsecond precision is achieved:

$$\text{Time}_{\mu s} = (\text{TickCount} \times 1000) + \frac{(\text{SysTick->LOAD} - \text{SysTick->VAL}) \times 1000}{\text{SysTick->LOAD}}$$

#### 2. API Implementations
1. **`clock_gettime(clockid_t clk_id, struct timespec *tp)`**:
   - Supports `CLOCK_REALTIME` and `CLOCK_MONOTONIC`.
   - Computes seconds (`tv_sec`) and nanoseconds (`tv_nsec`).
2. **`gettimeofday(struct timeval *tv, struct timezone *tz)`**:
   - Populates `tv->tv_sec` and `tv->tv_usec`.
3. **`nanosleep(const struct timespec *req, struct timespec *rem)`**:
   - Converts `req` to FreeRTOS ticks. Yields CPU if duration $\ge 1$ ms; falls back to busy microsecond loop for sub-millisecond residual delays.
4. **`time(time_t *tloc)`**:
   - Returns cumulative uptime in seconds.

---

### Milestone 4: Extended Mutex & Thread Attributes (Group D — 4 APIs)
*Target Files:* [`posix_shim.h`](../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.h), [`posix_shim.c`](../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c)

1. **`pthread_mutex_trylock(pthread_mutex_t *mutex)`**:
   - Calls `xSemaphoreTake((SemaphoreHandle_t)*mutex, 0)`.
   - Returns `0` on acquisition, or `EBUSY` immediately without blocking.
2. **`pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *abstime)`**:
   - Computes tick timeout from `abstime` and calls `xSemaphoreTake((SemaphoreHandle_t)*mutex, xTicksToWait)`.
   - Returns `ETIMEDOUT` if lock is not acquired within window.
3. **`pthread_mutexattr_init(pthread_mutexattr_t *attr)` & `pthread_mutexattr_settype(...)`**:
   - Allows declaring recursive mutexes (`PTHREAD_MUTEX_RECURSIVE`), mapping to `xSemaphoreCreateRecursiveMutex()`.
4. **`pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize)`**:
   - Stores custom stack size to pass to `xTaskCreate` (overriding default 1024 words).

---

### Milestone 5: Virtual File System (VFS) Layer Design & Prototype (Group E — 8 APIs)
*Target Files:* New `vfs.h`, `vfs.c`, integration in [`posix_shim.c`](../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c)

Microcontrollers lack physical block storage by default. A lightweight VFS routing layer routes standard file descriptors (`fd`):

```
                        +------------------------------------+
                        |      POSIX Standard I/O API        |
                        | (open, read, write, close, lseek)  |
                        +-----------------+------------------+
                                          |
                                          v
                        +------------------------------------+
                        |       VFS Descriptor Table         |
                        +--------+--------+---------+--------+
                                 |        |         |
             +-------------------+        |         +--------------------+
             v                            v                              v
   +--------------------+       +--------------------+         +--------------------+
   |  FD 0, 1, 2        |       |  FD 3 .. 15        |         |  FD 16 .. 31       |
   |  Console (UART0)   |       |  LwIP Sockets      |         |  RAM File / Stream |
   +--------------------+       +--------------------+         +--------------------+
```

1. **`open()`**: Returns descriptor index with associated driver callback table (`read`, `write`, `close`, `seek`).
2. **`read()` & `write()`**: Routed to UART0 (for `fd 0,1,2`), `lwip_read/write()` (for sockets), or memory buffers.
3. **`pipe(int pipefd[2])`**: Creates an inter-thread FIFO using FreeRTOS Stream Buffers (`xStreamBufferCreate`).

---

## 3. Step-by-Step Execution Schedule

| Step | Action Item | Target Outputs | Estimated Effort |
| :---: | :--- | :--- | :---: |
| **1** | **Implement Milestone 1 (Condition Variables)** | Update `posix_shim.h` & `posix_shim.c` with `pthread_cond_*` functions. | 2 Days |
| **2** | **Verify Milestone 1 with Producer-Consumer Test** | Add producer-consumer demo in `main_blinky.c`. Boot in QEMU and inspect UART logs. | 1 Day |
| **3** | **Implement Milestone 2 (Socket Client & Multiplexing)** | Add `connect`, `select`, `fcntl`, `setsockopt` shims. Verify in `lwipopts.h`. | 2 Days |
| **4** | **Verify Milestone 2 with Non-Blocking Client/Server** | Add an event loop / client request demo in `main_blinky.c`. Verify with host `curl` and netcat. | 1.5 Days |
| **5** | **Implement Milestone 3 (SysTick High-Res Clocks)** | Combine SysTick register with FreeRTOS tick counter for `clock_gettime`. | 1 Day |
| **6** | **Implement Milestone 4 (Timed/Trylock & Attributes)** | Implement `pthread_mutex_trylock`, timedlock, and stack attribute handling. | 1 Day |
| **7** | **Draft VFS Specification & Skeleton (Milestone 5)** | Design `vfs.h` / `vfs.c` data structures and routing table. | 2 Days |
| **8** | **Documentation & Regression Testing** | Update `results.md`, regenerate verification logs, update Gantt chart. | 1 Day |

---

## 4. Verification & Testing Commands

To build and verify each milestone in QEMU:

```bash
# 1. Compile the firmware binary
cd FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/build/gcc
mkdir -p output
make clean all

# 2. Inspect Flash and RAM footprint
arm-none-eabi-size output/RTOSDemo.out

# 3. Launch QEMU with serial console and port forwarding
qemu-system-arm -machine mps2-an385 -cpu cortex-m3 \
  -kernel output/RTOSDemo.out \
  -monitor none -nographic -serial stdio \
  -netdev user,id=mynet0,hostfwd=tcp::8080-:80 \
  -net nic,model=lan9118,netdev=mynet0

# 4. Host verification of networking
curl -i http://localhost:8080
```

---

## 5. Success Criteria for Tier 2 Expansion

- [ ] Zero build regressions in `arm-none-eabi-gcc`.
- [ ] Code footprint (`text`) remains $\le 85\text{ KB}$ (well within STM32 128 KB - 512 KB Flash boundaries).
- [ ] RAM overhead of new primitives remains $\le 10\text{ KB}$ additional dynamic heap.
- [ ] Bounded buffer producer-consumer successfully synchronizes via `pthread_cond_wait`/`signal` with zero deadlocks.
- [ ] `select()` multiplexes incoming connections without thread-per-connection scaling penalties.
- [ ] `clock_gettime(CLOCK_MONOTONIC)` provides accurate monotonic time intervals verified against host wall-clock time.
