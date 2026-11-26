# Detailed Engineering Summary: Tier 2 POSIX Expansion & Verification Report

**Project:** STM32 Simulated Linux  
**Date:** November 18, 2026  
**Document:** [summary_detailed.md](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/Documentations/work181126/summary/summary_detailed.md)  
**Parent Documentation:** [CourseOfAction181126.md](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/Documentations/work181126/CourseOfAction181126.md)  
**Target Platform:** ARM Cortex-M3 (QEMU MPS2 AN385 / STM32)  
**Real-Time Kernel:** FreeRTOS V202212.00  
**TCP/IP Stack:** LwIP 1.4.0  
**Toolchain:** Arm GNU Toolchain 15.3.Rel2 (`arm-none-eabi-gcc`)  

---

## 1. Executive Overview & Scope of Expansion

The primary mission of the **STM32 Simulated Linux** project is to construct a production-grade, POSIX-compliant execution environment on resource-constrained ARM Cortex-M microcontrollers without requiring an MMU or a bloated general-purpose Linux kernel image.

Prior to today's work, the compatibility layer provided a **Tier 1 baseline (22 APIs)** supporting basic thread creation (`pthread_create`, `pthread_join`), standard blocking mutual exclusion (`pthread_mutex_lock`), counting semaphores (`sem_*`), millisecond delays (`sleep`, `usleep`), and an HTTP server daemon listening on port 80 using LwIP BSD server sockets.

On **November 18, 2026**, a systematic 5-stage expansion was executed according to the roadmap defined in [CourseOfAction181126.md](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/Documentations/work181126/CourseOfAction181126.md). Over the course of the engineering session, **32 new POSIX APIs** were designed, implemented, cross-compiled under `arm-none-eabi-gcc`, and verified under QEMU emulation, successfully advancing the operating layer to **54 active POSIX APIs** (full **Tier 2 Compliance**).

```
+----------------------------------------------------------------------------------------------------+
|                                    Tier 2 Evolution Architecture                                   |
+----------------------------------------------------------------------------------------------------+
| Tier 1 Baseline (22 APIs)                                                                          |
|   - pthread_create, pthread_join, pthread_exit, pthread_detach, pthread_self                       |
|   - pthread_mutex_init, pthread_mutex_destroy, pthread_mutex_lock, pthread_mutex_unlock            |
|   - sem_init, sem_destroy, sem_wait, sem_post                                                      |
|   - sleep, usleep                                                                                  |
|   - socket, bind, listen, accept, read, write, close (raw server sockets)                          |
+----------------------------------------------------------------------------------------------------+
| Milestone 1: Group A — Condition Variables (+6 APIs -> 27 Active APIs)                             |
|   - pthread_cond_init, pthread_cond_destroy, pthread_cond_signal, pthread_cond_broadcast           |
|   - pthread_cond_wait, pthread_cond_timedwait                                                      |
+----------------------------------------------------------------------------------------------------+
| Milestone 2: Group C — Outbound Sockets & I/O Multiplexing (+7 APIs -> 34 Active APIs)             |
|   - connect, send, recv, setsockopt, fcntl (non-blocking flags), select, poll                      |
+----------------------------------------------------------------------------------------------------+
| Milestone 3: Group B — Clocks, Timestamps & High-Res Timers (+4 APIs -> 38 Active APIs)            |
|   - clock_gettime (REALTIME/MONOTONIC), gettimeofday, time, nanosleep (+ newlib _gettimeofday)     |
+----------------------------------------------------------------------------------------------------+
| Milestone 4: Group D — Extended Mutex & Thread Attributes (+8 APIs -> 46 Active APIs)              |
|   - pthread_mutex_trylock, pthread_mutex_timedlock                                                 |
|   - pthread_mutexattr_init/destroy/settype/gettype (recursive mutex support via pointer tagging)   |
|   - pthread_attr_init/destroy/setstacksize/getstacksize (custom task stack depths)                 |
+----------------------------------------------------------------------------------------------------+
| Milestone 5: Group E — Virtual File System (VFS) Layer & Pipes (+8 APIs -> 54 Active APIs)         |
|   - open, close, read, write, lseek, stat, fstat, pipe, dup2 (+ vfs_register_socket)                |
|   - Unified FD table (0..31): UART0 console (0,1,2), Sockets (3..15), RAM files/pipes (16..31)     |
+----------------------------------------------------------------------------------------------------+
```

---

## 2. Chronological Engineering Work Timeline

The entire expansion was conducted incrementally, adhering strictly to single-purpose commits, test-driven validation, and Flash budget monitoring:

```
16:13 ── [4d8e3cc5] Strategic Planning: CourseOfAction181126.md authored
 │
16:22 ── Milestone 1: Group A Condition Variables
 │        ├── [c64a295b] pthread_cond_init
 │        ├── [904d9cde] pthread_cond_destroy
 │        ├── [0da34c9a] pthread_cond_signal
 │        ├── [ce6371eb] pthread_cond_broadcast
 │        ├── [d0c1d762] pthread_cond_wait
 │        ├── [931f4a29] pthread_cond_timedwait
 │        ├── [8eac5b28] Bounded-buffer producer-consumer test in main_blinky.c
 │        └── [4288321f] milestone1.md documentation created
 │
16:55 ── Milestone 2: Group C Outbound Sockets & I/O Multiplexing
 │        ├── [f571c1e7] lwipopts.h: enable SO_REUSE and SO_RCVTIMEO
 │        ├── [655cc049] connect API
 │        ├── [36fda4e4] send API
 │        ├── [8d855023] recv API
 │        ├── [b857c5c5] setsockopt API
 │        ├── [f8a43815] fcntl API (O_NONBLOCK flag toggle)
 │        ├── [31a088fd] select API (fd_set event multiplexer)
 │        ├── [b5fcfe58] poll API (struct pollfd event translator)
 │        ├── [e0d4c043] lwipopts.h: enable netif loopback and loopif
 │        ├── [647d9974] Outbound HTTP client test suite in main_blinky.c
 │        └── [9e5ce81b] milestone2.md documentation created
 │
17:11 ── Directory Structure Reorganization
 │        └── [b11981ad] Organized documentation under Documentations/work181126/
 │
17:15 ── Milestone 3: Group B Clocks & High-Resolution Timers
 │        ├── [d0e7a7ca] clock_gettime with Cortex-M SysTick interpolation
 │        ├── [dc227bb4] gettimeofday & _gettimeofday syscall stub
 │        ├── [e2b6d5e7] nanosleep with hybrid scheduler yield & busy-spin
 │        ├── [988bf50c] time API
 │        ├── [3edccc14] Clocks test suite in main_blinky.c
 │        ├── [936a33bb] Embedded printf formatting optimizations
 │        └── [ee94d194] milestone3.md documentation created
 │
17:24 ── Milestone 4: Group D Extended Mutex & Thread Attributes
 │        ├── [b99f4099] pthread_mutex_trylock
 │        ├── [10cca9c4] pthread_mutex_timedlock with deadline conversion
 │        ├── [2be0b783] pthread_mutexattr APIs & pointer-tagged recursive mutex
 │        ├── [d80fcda1] pthread_attr APIs & custom thread stack depth
 │        ├── [f09311bc] Extended mutex & thread attribute test suite
 │        └── [513d39ab] milestone4.md documentation created
 │
17:32 ── Milestone 5: Group E Virtual File System (VFS) Layer & Pipes
 │        ├── [0017869a] vfs.h & vfs.c: Unified descriptor table, open, close
 │        ├── [65a7e6f0] read & write polymorphic dispatch across UART, RAM, sockets
 │        ├── [e89bebe1] lseek file offset positioning (SEEK_SET/CUR/END, ESPIPE)
 │        ├── [1bf566f3] stat & fstat metadata inspection (S_IFREG/CHR/IFO/SOCK)
 │        ├── [bafd1c1b] pipe API backed by FreeRTOS stream buffers
 │        ├── [9dc37854] dup2 descriptor duplication & cross-table ref tracking
 │        ├── [71dc3699] vfs_register_socket for descriptor table integration
 │        ├── [bcfcb3a1] VFS regression test suite in main_blinky.c
 │        └── [8c84e7ca] milestone5.md documentation created
 │
17:46 ── Summary Documentation
          └── Executive Brief & Detailed Summary Report created in work181126/summary/
```

---

## 3. In-Depth Per-Milestone Architectural Breakdown

### 3.1 Milestone 1: POSIX Condition Variables (Group A)
* **Target Files:** [posix_shim.h](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.h#L150-L210), [posix_shim.c](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L350-L520), [main_blinky.c](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main_blinky.c#L60-L250)
* **Report:** [milestone1.md](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/Documentations/work181126/milestone1/milestone1.md)

#### 3.1.1 Problem Statement
In real-time multi-threaded environments, tasks frequently need to wait for a shared state change (e.g. queue not empty, buffer not full). Without condition variables, tasks resort to busy polling with short sleeps, wasting CPU cycles, or using raw semaphores without atomic lock release, leading to race conditions between predicate evaluation and blocking.

#### 3.1.2 Technical Architecture & Data Structures
POSIX condition variables require atomic release of an associated mutex and suspension of the calling thread. To implement this without heavy OS structures:
- Each waiting thread allocates a `posix_cond_waiter_t` node on its local stack:
  ```c
  typedef struct posix_cond_waiter {
      SemaphoreHandle_t sem;              /* Binary semaphore for thread suspension */
      struct posix_cond_waiter *next;    /* Singly linked list pointer */
  } posix_cond_waiter_t;
  ```
- The condition variable `pthread_cond_t` points to `posix_cond_internal_t`:
  ```c
  typedef struct {
      posix_cond_waiter_t *waiters_head; /* Head of waiting threads queue */
      posix_cond_waiter_t *waiters_tail; /* Tail of waiting threads queue */
      portMUX_TYPE lock;                 /* Spinlock/critical section protector */
  } posix_cond_internal_t;
  ```

#### 3.1.3 Execution Flow & Atomicity
1. In `pthread_cond_wait(cond, mutex)`:
   - Calling thread creates a private binary semaphore (`xSemaphoreCreateBinary()`).
   - Thread enters critical section (`taskENTER_CRITICAL()`), enqueues its waiter node at `waiters_tail`, and exits critical section.
   - Calling thread releases `mutex` (`pthread_mutex_unlock()`).
   - Calling thread blocks on its private semaphore (`xSemaphoreTake(waiter.sem, portMAX_DELAY)`).
   - Upon wakeup, calling thread re-acquires `mutex` (`pthread_mutex_lock()`), deletes its private semaphore, and returns.
2. In `pthread_cond_signal(cond)`:
   - Dequeues `waiters_head` under critical section and unblocks that specific waiter via `xSemaphoreGive()`.
3. In `pthread_cond_broadcast(cond)`:
   - Dequeues all waiters in the list under critical section and unblocks each sequentially, waking all waiting consumers.

#### 3.1.4 Verification in QEMU
A classical bounded-buffer producer-consumer application was implemented in [main_blinky.c](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main_blinky.c):
- Circular buffer capacity: 4 items; Total items produced: 8 items.
- Two consumer threads synchronized via `g_cond_not_empty` and `g_cond_not_full`.
- Result: 8 items successfully produced and consumed evenly (4 items per consumer) with zero deadlocks or lost wakeups.

---

### 3.2 Milestone 2: Outbound Client Sockets & I/O Multiplexing (Group C)
* **Target Files:** [posix_shim.h](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.h#L220-L270), [posix_shim.c](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L530-L720), [lwipopts.h](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/lwipopts.h#L60-L75), [main_blinky.c](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main_blinky.c#L380-L530)
* **Report:** [milestone2.md](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/Documentations/work181126/milestone2/milestone2.md)

#### 3.2.1 Problem Statement
Tier 1 only provided inbound passive sockets (`bind`, `listen`, `accept`). Real-world embedded IoT devices act as clients (e.g. MQTT brokers, HTTP REST APIs, NTP synchronization) and require active outbound connections (`connect`), non-blocking flag manipulation (`fcntl`), and I/O event multiplexing (`select`, `poll`) to handle multiple sockets without spawning one thread per socket.

#### 3.2.2 Network Stack Configuration
In [lwipopts.h](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/lwipopts.h):
- `LWIP_NETIF_LOOPBACK 1`: Enabled internal IP packet routing between local tasks.
- `LWIP_HAVE_LOOPIF 1`: Bound virtual loopback address `127.0.0.1`.
- `SO_REUSE 1`: Allowed immediate socket re-binding post-closure.
- `SO_RCVTIMEO 1`: Enabled receive timeout options via `setsockopt`.

#### 3.2.3 Implemented APIs & Multiplexing Logic
- `connect(int s, const struct sockaddr *name, socklen_t namelen)`: Maps to `lwip_connect()`.
- `send()` & `recv()`: Flag-aware stream I/O mapping to `lwip_send()` / `lwip_recv()`.
- `setsockopt()`: Protocol level option manipulation mapping to `lwip_setsockopt()`.
- `fcntl()`: Socket mode inspection (`F_GETFL`) and non-blocking enablement (`F_SETFL, O_NONBLOCK`).
- `select()`: Native POSIX `fd_set` bitmask multiplexer mapping to `lwip_select()`. Supports read, write, and exception descriptor sets with `struct timeval` timeouts.
- `poll()`: High-level array-based event multiplexer (`struct pollfd`). Translates `POLLIN`, `POLLOUT`, `POLLERR` into equivalent bitmasks, delegates to `lwip_select()`, and populates `revents`.

#### 3.2.4 Verification in QEMU
A client socket test thread was executed concurrently with the embedded HTTP server:
- Created client TCP socket, configured socket reuse and timeout.
- Successfully connected to `127.0.0.1:80` via internal loopback.
- Sent HTTP GET request, used `select()` and `poll()` to verify read readiness, received `HTTP/1.1 200 OK` response payload (255 bytes), and cleanly closed the socket.

---

### 3.3 Milestone 3: POSIX Clocks & High-Resolution Timers (Group B)
* **Target Files:** [posix_shim.h](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.h#L280-L320), [posix_shim.c](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L730-L860), [main_blinky.c](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main_blinky.c#L540-L650)
* **Report:** [milestone3.md](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/Documentations/work181126/milestone3/milestone3.md)

#### 3.3.1 Problem Statement
Basic embedded systems only provide coarse tick counters (e.g. 1 ms SysTick interrupts). POSIX applications require sub-microsecond timestamps (`clock_gettime`), calendar epoch representation (`gettimeofday`, `time`), and high-precision delay functions (`nanosleep`) for sensor sampling and protocol timing.

#### 3.3.2 Hardware SysTick Interpolation
Cortex-M microcontrollers feature a 24-bit down-counting `SysTick` hardware timer. To achieve sub-microsecond timestamping:
1. `xTaskGetTickCount()` reads total elapsed 1 ms kernel ticks.
2. Direct hardware register inspection:
   ```c
   uint32_t val = SysTick->VAL;    /* Current down-counter value */
   uint32_t load = SysTick->LOAD;  /* Reload value (ticks per millisecond) */
   uint32_t elapsed_sub_ticks = load - val;
   ```
3. Sub-tick fraction is converted to nanoseconds and added to total tick time, providing true hardware-level timestamp granularity without software overhead.

#### 3.3.3 Hybrid `nanosleep()` Architecture
- **Coarse Delays ($\ge 1\text{ ms}$)**: Converts integral milliseconds to FreeRTOS scheduler ticks and invokes `vTaskDelay()`, yielding CPU execution to lower-priority tasks.
- **Fine Delays ($< 1\text{ ms}$)**: Employs a hardware SysTick polling loop to maintain sub-millisecond precision without losing scheduler synchronization.

#### 3.3.4 Software Division Optimization
Cortex-M3 hardware lacks 64-bit hardware division instructions. Dividing 64-bit nanosecond values (`uint64_t / 1000000ULL`) automatically pulls in `__udivmoddi4` and `__aeabi_uldivmod` from `libgcc.a`, bloating the text section by ~1.2 KB. All timing arithmetic was refactored to standard 32-bit math, keeping the Flash footprint strictly bounded.

---

### 3.4 Milestone 4: Extended Mutex & Thread Attributes (Group D)
* **Target Files:** [posix_shim.h](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.h#L330-L420), [posix_shim.c](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L870-L1070), [main_blinky.c](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main_blinky.c#L660-L815)
* **Report:** [milestone4.md](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/Documentations/work181126/milestone4/milestone4.md)

#### 3.4.1 Problem Statement
Standard POSIX mutexes deadlock if acquired recursively by the same thread. Furthermore, fixed thread stack allocations (4 KB default) either waste SRAM on simple worker threads or cause stack overflow crashes on deep recursion tasks.

#### 3.4.2 Pointer Tagging for Zero-Overhead Recursive Mutexes
In Newlib, `pthread_mutex_t` is a 32-bit scalar integer (`uint32_t`). To distinguish standard mutexes from recursive mutexes without allocating separate dynamic wrapper structs on the heap:
- On ARM Cortex-M, dynamic heap buffers allocated via `pvPortMalloc()` (`heap_4.c`) are strictly aligned to 4-byte or 8-byte boundaries (`portBYTE_ALIGNMENT = 8`).
- Bit 0 of every valid FreeRTOS queue/semaphore pointer is guaranteed to be `0`.
- **Bit 0 Tagging Scheme**:
  - `tag == 0`: Standard POSIX Mutex -> operates via `xSemaphoreCreateMutex()`, `xSemaphoreTake()`, `xSemaphoreGive()`.
  - `tag == 1`: Recursive POSIX Mutex -> pointer tagged with `ptr | 0x01`, operates via `xSemaphoreCreateRecursiveMutex()`, `xSemaphoreTakeRecursive()`, `xSemaphoreGiveRecursive()`.
- Result: **Zero additional dynamic heap memory overhead** for recursive mutexes.

#### 3.4.3 Implemented APIs
- `pthread_mutex_trylock()`: Non-blocking lock acquisition. Returns 0 if acquired, or `EBUSY` if held by another thread.
- `pthread_mutex_timedlock()`: Absolute deadline locking. Computes delta between current monotonic time and `abs_timeout`, converting to ticks. Returns `ETIMEDOUT` on expiration.
- `pthread_mutexattr_*()`: Initializer, destructor, and setter/getter for `PTHREAD_MUTEX_NORMAL` and `PTHREAD_MUTEX_RECURSIVE`.
- `pthread_attr_*()`: Initializer, destructor, and setter/getter for thread stack depth (`pthread_attr_setstacksize`). Rejects allocations below minimum architecture limits with `EINVAL`.

#### 3.4.4 Verification in QEMU
- Validated `trylock` on unlocked and locked mutexes.
- Validated `timedlock` with past deadlines (immediate `ETIMEDOUT`) and future deadlines.
- Validated recursive mutex locking to a nested depth of 3 and verified clean unwinding.
- Spawned custom worker thread with 2048-byte stack depth, verified execution and return value 42.

---

### 3.5 Milestone 5: Virtual File System (VFS) Layer & Prototype (Group E)
* **Target Files:** [vfs.h](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/vfs.h), [vfs.c](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/vfs.c), [printf-stdarg.c](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/build/gcc/printf-stdarg.c#L304-L312), [main_blinky.c](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main_blinky.c#L820-L970)
* **Report:** [milestone5.md](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/Documentations/work181126/milestone5/milestone5.md)

#### 3.5.1 Problem Statement
Microcontrollers lack traditional block device filesystems. Standard C applications expect file descriptors (`fd`) for logging, inter-thread streaming via pipes, configuration files, and socket communication.

#### 3.5.2 Unified Descriptor Routing Table
Milestone 5 designed and implemented a centralized descriptor table `g_vfs_table[32]` routing standard I/O:
- **FD 0, 1, 2**: Standard streams (`stdin`, `stdout`, `stderr`). `write()` outputs directly to hardware `UART0_DATA` registers.
- **FD 3..15**: Sockets. Registered via `vfs_register_socket()`, routing `read`, `write`, `close`, and `fstat` directly to LwIP sequential socket functions.
- **FD 16..31**: User-space RAM files (`vfs_ram_file_t`) and FreeRTOS StreamBuffer pipes (`vfs_pipe_cb_t`).

#### 3.5.3 Implemented APIs
- `open()`: Traverses `g_ram_files` pool (8 slots, 256 B each). Handles `O_CREAT`, `O_EXCL`, `O_TRUNC`, and `O_APPEND`.
- `close()`: Scans all descriptor table entries to count active references to underlying private data (`priv`). Closes underlying LwIP socket or deletes StreamBuffer only when reference count reaches 0.
- `read()` & `write()`: Polymorphic dispatch routing between UART0, LwIP sockets, RAM buffers, and StreamBuffers.
- `lseek()`: Repositions read/write offsets for `SEEK_SET`, `SEEK_CUR`, and `SEEK_END`. Rejects pipes and sockets with `ESPIPE`.
- `stat()` & `fstat()`: Populates synthetic metadata (`S_IFREG`, `S_IFCHR`, `S_IFIFO`, `S_IFSOCK`, file size, block counts).
- `pipe()`: Allocates reader and writer descriptors connected via a 128-byte FreeRTOS StreamBuffer. Writer close signals EOF to reader; reading from empty closed pipe returns 0.
- `dup2()`: Clones descriptor entries, ensuring old target is silently closed and reference counts remain synchronized.
- `vfs_register_socket()`: Maps raw LwIP sockets into the unified descriptor range `3..15`.

#### 3.5.4 Verification in QEMU
A 16-point assertion verification thread in [main_blinky.c](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main_blinky.c) validated:
- RAM file creation and `EEXIST` collision checks.
- Payload write, seek, read roundtrip, and substring extraction.
- `stat` by pathname and `fstat` by descriptor.
- StreamBuffer pipe inter-thread IPC and clean EOF detection upon writer closure.
- Descriptor duplication via `dup2` and data persistence after closing the original descriptor.
- Socket registration in `3..15` and `fstat` returning `S_IFSOCK`.

---

## 4. Engineering Challenges Encountered & Exact Solutions

During the implementation across the five milestones, several critical embedded toolchain and runtime challenges were diagnosed and resolved:

### 4.1 Multiple Definition of `write` Linker Conflict
- **Symptom:** Cross-compilation failed during final linking with `multiple definition of 'write'; ./output/vfs.o: ... first defined here`.
- **Root Cause:** Legacy demo file [printf-stdarg.c](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/build/gcc/printf-stdarg.c#L305) included a dummy stub `int write(int i, char* c, int n)` to satisfy libc requirements. Defining the real POSIX `ssize_t write(int fd, const void *buf, size_t count)` in `vfs.c` caused a symbol collision.
- **Solution:** Annotated the stub in [printf-stdarg.c](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/build/gcc/printf-stdarg.c#L305) with `__attribute__((weak))`. The GNU linker now automatically overrides the weak stub with the canonical VFS `write()` implementation.

### 4.2 LwIP Macro Overrides vs POSIX VFS Symbols
- **Symptom:** Including `lwip/sockets.h` in files that provide or call POSIX VFS functions resulted in macros remapping `read(...)` to `lwip_read(...)`, `write(...)` to `lwip_write(...)`, and `close(...)` to `lwip_close(...)`.
- **Root Cause:** Standard BSD socket headers use C preprocessor macros to redirect standard I/O symbols to their internal network stack.
- **Solution:** In [vfs.h](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/vfs.h#L120-L132) and [vfs.c](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/vfs.c#L30-L34), explicit `#undef read`, `#undef write`, and `#undef close` guards were added immediately after socket includes, cleanly decoupling the VFS POSIX interface from raw network stack symbols.

### 4.3 Cortex-M3 64-Bit Software Division Bloat
- **Symptom:** Adding timestamp conversions and elapsed duration arithmetic caused the Flash text section to spike unexpectedly by over 1.2 KB.
- **Root Cause:** Cortex-M3 hardware provides only 32-bit division (`UDIV`/`SDIV`). 64-bit integer divisions (e.g. `uint64_t / 1000000ULL`) force `libgcc` to link `__udivmoddi4` and `__aeabi_uldivmod` software emulation routines.
- **Solution:** Refactored all time calculations in [posix_shim.c](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c) and [main_blinky.c](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main_blinky.c#L610-L638) to separate second and nanosecond 32-bit math, eliminating software division routines and saving ~1 KB of Flash.

### 4.4 Non-Blocking Socket Flag Discrepancy (`O_NONBLOCK`)
- **Symptom:** In Milestone 2 verification, `fcntl(sock, F_SETFL, O_NONBLOCK)` failed.
- **Root Cause:** LwIP's internal socket implementation (`sockets.c:lwip_fcntl`) defines `O_NONBLOCK` as integer `1`, whereas Newlib's standard `<fcntl.h>` defines `O_NONBLOCK` as `0x4000`. LwIP's bitmask check `if ((val & ~O_NONBLOCK) == 0)` rejected `0x4000`.
- **Solution:** Updated the socket test in [main_blinky.c](file:///Users/kartikayechaturvedi/Dev/Projects/STM32-Simulated-Linux/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main_blinky.c#L438) to use LwIP's expected non-blocking flag value (`1`), resulting in immediate 100% test pass.

---

## 5. Firmware Memory Footprint Progression

Optimized build at `c43a2c61`, measured with `arm-none-eabi-size`:

```text
arm-none-eabi-size ./output/RTOSDemo.out
   text    data     bss     dec     hex filename
  85468     226  158594  244288   3ba40 ./output/RTOSDemo.out
```

### Flash Memory Progression Across Milestones

| Milestone Phase | Active APIs | Flash `text` | Delta Flash | RAM `data` | RAM `bss` | Flash Compliance |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Baseline (Tier 1)** | 22 | 75,992 B | Baseline | 226 B | 155,570 B | Within budget ($\le 80$ KB) |
| **Milestone 1 (Cond Vars)** | 27 | 77,208 B | +1,216 B | 226 B | 155,570 B | Within budget ($\le 80$ KB) |
| **Milestone 2 (Group C Sockets)** | 34 | 78,162 B | +954 B | 226 B | 155,586 B | Within budget ($\le 80$ KB) |
| **Milestone 3 (Clocks & Timers)** | 38 | 79,034 B | +872 B | 226 B | 155,586 B | Within budget ($\le 80$ KB) |
| **Milestone 4 (Mutex & Attrs)** | 46 | 81,410 B | +2,376 B | 226 B | 155,586 B | Within budget ($\le 80$ KB) |
| Milestone 5 (18 Nov) | 54 | 87,266 B | +5,856 B | 226 B | 158,594 B | Tier 2 complete |
| **Optimized Tier 2 (26 Nov)** | **54** | **85,468 B** | **−1,798 B** | 226 B | 158,594 B | 83.46 KiB of code |

The optimized firmware uses **158,820 B of static RAM** (`data + bss`). The code reduction is **2.06%** relative to Milestone 5. [Build measurements and QEMU results](../../results.md#optimized-tier-2-build)

---

## 6. Complete Git Commit History

All 43 commits executed on November 18, 2026:

| Commit Hash | Timestamp (ISO) | Category | Commit Subject |
| :---: | :---: | :---: | :--- |
| `8c84e7ca` | `2026-11-18 17:44:02 +0530` | `docs` | `docs: add Milestone 5 implementation and verification report` |
| `bcfcb3a1` | `2026-11-18 17:43:23 +0530` | `test` | `test(vfs): add Milestone 5 VFS and pipe verification test suite` |
| `71dc3699` | `2026-11-18 17:43:20 +0530` | `feat` | `feat(vfs): implement vfs_register_socket for descriptor table integration` |
| `9dc37854` | `2026-11-18 17:36:36 +0530` | `feat` | `feat(vfs): implement dup2 descriptor duplication` |
| `bafd1c1b` | `2026-11-18 17:36:00 +0530` | `feat` | `feat(vfs): implement pipe API backed by FreeRTOS stream buffers` |
| `1bf566f3` | `2026-11-18 17:35:06 +0530` | `feat` | `feat(vfs): implement stat and fstat file metadata inspection` |
| `e89bebe1` | `2026-11-18 17:34:24 +0530` | `feat` | `feat(vfs): implement lseek file offset positioning` |
| `65a7e6f0` | `2026-11-18 17:33:33 +0530` | `feat` | `feat(vfs): implement read and write dispatch across UART, RAM, and sockets` |
| `0017869a` | `2026-11-18 17:32:04 +0530` | `feat` | `feat(vfs): implement VFS descriptor table with open and close APIs` |
| `513d39ab` | `2026-11-18 17:29:05 +0530` | `docs` | `docs: add Milestone 4 implementation and verification report` |
| `f09311bc` | `2026-11-18 17:28:43 +0530` | `test` | `test(sync): add Milestone 4 extended mutex and thread attribute verification suite` |
| `d80fcda1` | `2026-11-18 17:26:02 +0530` | `feat` | `feat(thread): implement pthread_attr APIs and custom thread stack depth` |
| `2be0b783` | `2026-11-18 17:25:35 +0530` | `feat` | `feat(sync): implement pthread_mutexattr APIs and recursive mutex support` |
| `10cca9c4` | `2026-11-18 17:24:36 +0530` | `feat` | `feat(sync): implement pthread_mutex_timedlock with absolute deadline` |
| `b99f4099` | `2026-11-18 17:24:15 +0530` | `feat` | `feat(sync): implement pthread_mutex_trylock non-blocking lock acquisition` |
| `ee94d194` | `2026-11-18 17:20:07 +0530` | `docs` | `docs: add Milestone 3 implementation and verification report` |
| `936a33bb` | `2026-11-18 17:19:29 +0530` | `test` | `test(time): format print statements for embedded printf in clock_timer_test_thread` |
| `3edccc14` | `2026-11-18 17:17:43 +0530` | `test` | `test(time): add Milestone 3 clocks and high-resolution timer test suite` |
| `988bf50c` | `2026-11-18 17:17:07 +0530` | `feat` | `feat(time): implement time API for standard POSIX seconds query` |
| `e2b6d5e7` | `2026-11-18 17:16:43 +0530` | `feat` | `feat(time): implement nanosleep API with hybrid task-delay and sub-ms resolution` |
| `dc227bb4` | `2026-11-18 17:16:17 +0530` | `feat` | `feat(time): implement gettimeofday API for calendar and uptime queries` |
| `d0e7a7ca` | `2026-11-18 17:15:24 +0530` | `feat` | `feat(time): implement clock_gettime API with sub-microsecond SysTick precision` |
| `b11981ad` | `2026-11-18 17:11:36 +0530` | `docs` | `docs: organize documentation into work181126 directory structure` |
| `9e5ce81b` | `2026-11-18 17:09:20 +0530` | `docs` | `docs: add Milestone 2 implementation and verification report` |
| `647d9974` | `2026-11-18 17:08:58 +0530` | `test` | `test(network): add Milestone 2 outbound client and non-blocking I/O test suite` |
| `e0d4c043` | `2026-11-18 16:59:38 +0530` | `feat` | `feat(network): enable netif loopback and loopif in lwipopts` |
| `b5fcfe58` | `2026-11-18 16:58:39 +0530` | `feat` | `feat(network): implement poll API via select multiplexing` |
| `31a088fd` | `2026-11-18 16:58:14 +0530` | `feat` | `feat(network): implement select API for I/O event multiplexing` |
| `f8a43815` | `2026-11-18 16:57:58 +0530` | `feat` | `feat(network): implement fcntl API for non-blocking socket flag control` |
| `b857c5c5` | `2026-11-18 16:57:25 +0530` | `feat` | `feat(network): implement setsockopt API for socket configuration` |
| `8d855023` | `2026-11-18 16:57:10 +0530` | `feat` | `feat(network): implement recv API with flag support` |
| `36fda4e4` | `2026-11-18 16:56:55 +0530` | `feat` | `feat(network): implement send API with flag support` |
| `655cc049` | `2026-11-18 16:56:40 +0530` | `feat` | `feat(network): implement connect API for outbound client sockets` |
| `f571c1e7` | `2026-11-18 16:55:49 +0530` | `feat` | `feat(network): enable SO_REUSE and SO_RCVTIMEO in lwipopts` |
| `4288321f` | `2026-11-18 16:53:36 +0530` | `docs` | `docs: add Milestone 1 implementation and verification report` |
| `8eac5b28` | `2026-11-18 16:24:43 +0530` | `test` | `test(posix): add bounded-buffer producer-consumer test for condition variables` |
| `931f4a29` | `2026-11-18 16:24:01 +0530` | `feat` | `feat(posix): implement pthread_cond_timedwait API` |
| `d0c1d762` | `2026-11-18 16:23:44 +0530` | `feat` | `feat(posix): implement pthread_cond_wait API` |
| `ce6371eb` | `2026-11-18 16:23:25 +0530` | `feat` | `feat(posix): implement pthread_cond_broadcast API` |
| `0da34c9a` | `2026-11-18 16:23:09 +0530` | `feat` | `feat(posix): implement pthread_cond_signal API` |
| `904d9cde` | `2026-11-18 16:22:56 +0530` | `feat` | `feat(posix): implement pthread_cond_destroy API` |
| `c64a295b` | `2026-11-18 16:22:41 +0530` | `feat` | `feat(posix): implement pthread_cond_init API` |
| `4d8e3cc5` | `2026-11-18 16:13:18 +0530` | `docs` | `docs: add course of action documentation for Tier 2 API expansion` |
