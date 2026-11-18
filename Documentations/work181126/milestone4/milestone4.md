# Milestone 4: Extended Mutex & Thread Attributes (Group D) Implementation & Verification Report

**Project:** STM32 Simulated Linux  
**Date:** November 18, 2026  
**Document:** `Documentations/work181126/milestone4/milestone4.md`  
**Milestone:** Group D — Extended Mutex & Thread Attributes  
**Target Architecture:** ARM Cortex-M3 (QEMU MPS2 AN385 / STM32)  
**RTOS Kernel:** FreeRTOS V202212.00  
**Toolchain:** Arm GNU Toolchain 15.3.Rel2 (`arm-none-eabi-gcc`)  

---

## 1. Executive Summary

Milestone 4 expands the **STM32 Simulated Linux** compatibility layer from **38 active APIs** to **46 active APIs** by implementing the **Group D Extended Mutex & Thread Attributes Subsystem**.

Prior to this milestone, thread synchronization was restricted to standard blocking mutex locks (`pthread_mutex_lock`), which could cause thread deadlocks if re-entered by the same owning thread or indefinite blocking in real-time control loops. Furthermore, all POSIX threads were spawned with a fixed default stack allocation (1024 words = 4096 bytes), which wastes precious embedded SRAM on lightweight tasks while preventing memory-intensive tasks from allocating sufficient stack depth.

All Group D APIs were incrementally implemented, cross-compiled under `arm-none-eabi-gcc`, and verified under `qemu-system-arm` on the Cortex-M3 platform. The test suite exercised non-blocking lock polling (`trylock`), absolute deadline expiration (`timedlock`), nested re-entrant locking (`recursive mutex`), and thread spawning with custom stack configurations with 100% pass rates while keeping total firmware Flash footprint strictly within the 80 KB budget.

---

## 2. Technical Architecture & Memory Model

```
                    POSIX Application Thread
                                │
        ┌───────────────────────┼───────────────────────┐
        │                       │                       │
        ▼                       ▼                       ▼
 ┌──────────────┐        ┌──────────────┐        ┌──────────────┐
 │ pthread_     │        │ pthread_     │        │ pthread_attr_│
 │ mutex_trylock│        │ mutex_timed- │        │ setstacksize │
 └──────┬───────┘        │     lock     │        └──────┬───────┘
        │                └──────┬───────┘               │
        │                       │                       ▼
        │                       │                ┌──────────────┐
        │                       │                │pthread_create│
        │                       │                └──────┬───────┘
        └───────────────┬───────┴───────────────────────┘
                        ▼
         ┌─────────────────────────────┐
         │  Pointer-Tagged Mutex Model │
         │   (Bit 0: 0=Norm, 1=Recur)  │
         └──────────────┬──────────────┘
                        │
         ┌──────────────┴──────────────┐
         ▼                             ▼
 ┌────────────────┐            ┌────────────────┐
 │ Standard Mutex │            │Recursive Mutex │
 │(xSemaphoreTake)│            │(xSemaphoreTake │
 │                │            │   Recursive)   │
 └────────────────┘            └────────────────┘
```

### 2.1 Pointer Tagging for Zero-Overhead Recursive Mutexes
In standard POSIX, `pthread_mutex_t` is a scalar value (typed as `uint32_t` in Newlib). In FreeRTOS, standard mutexes require `xSemaphoreTake()` / `xSemaphoreGive()`, while recursive mutexes require `xSemaphoreTakeRecursive()` / `xSemaphoreGiveRecursive()`.

Rather than allocating an extra dynamic control block for every mutex (which would increase heap fragmentation and RAM overhead), **pointer tagging** is utilized:
- On ARM Cortex-M, dynamic heap buffers allocated via `pvPortMalloc()` (`heap_4.c`) are aligned to at least 4-byte boundaries (`portBYTE_ALIGNMENT = 8`).
- As a result, the lowest bit (Bit 0) of any valid `SemaphoreHandle_t` pointer is guaranteed to be `0`.
- Bit 0 is used as a type tag:
  - `0`: Standard Mutex (`POSIX_MUTEX_TYPE_NORMAL`)
  - `1`: Recursive Mutex (`POSIX_MUTEX_TYPE_RECURSIVE`)
- Masking macros decode the handle and type with zero runtime overhead:
  ```c
  #define POSIX_MUTEX_TYPE_MASK       ((uintptr_t)0x01)
  #define POSIX_MUTEX_HANDLE(m)       ((SemaphoreHandle_t)((uintptr_t)(m) & ~POSIX_MUTEX_TYPE_MASK))
  #define POSIX_MUTEX_IS_RECURSIVE(m) (((uintptr_t)(m) & POSIX_MUTEX_TYPE_MASK) == POSIX_MUTEX_TYPE_RECURSIVE)
  ```

### 2.2 Deadlock Prevention via `pthread_mutex_trylock`
`pthread_mutex_trylock()` enables non-blocking lock attempts:
- Invokes `xSemaphoreTake(h, 0)` or `xSemaphoreTakeRecursive(h, 0)`.
- If the lock is held by another task, FreeRTOS returns immediately without placing the caller into the `Blocked` queue.
- Translates `pdFALSE` directly to POSIX standard error `EBUSY`.

### 2.3 Cycle-Accurate Deadline Locking via `pthread_mutex_timedlock`
`pthread_mutex_timedlock()` accepts an absolute POSIX deadline (`struct timespec *abstime`):
1. Samples the current monotonic calendar time via `clock_gettime(CLOCK_REALTIME, &now)`.
2. If `now >= abstime`, attempts an immediate non-blocking acquisition (`0` wait); if unavailable, returns `ETIMEDOUT`.
3. If `abstime > now`, computes elapsed delta:
   $$\Delta_{\text{ms}} = (abstime.tv\_sec - now.tv\_sec) \times 1000 + \frac{abstime.tv\_nsec - now.tv\_nsec}{1,000,000}$$
4. Translates delta milliseconds to kernel ticks via `pdMS_TO_TICKS(\Delta_{\text{ms}})`.
5. Passes tick count to `xSemaphoreTake()`. Returns `0` on acquisition or `ETIMEDOUT` upon tick expiration.

### 2.4 Dynamic Stack Sizing via `pthread_attr_*`
By integrating `pthread_attr_setstacksize()`, tasks are no longer locked into the default 1024-word (4096-byte) stack:
- Validates that requested byte size is at least `configMINIMAL_STACK_SIZE * sizeof(StackType_t)`.
- Translates bytes to words: `stack_words = stacksize / sizeof(StackType_t)`.
- Passes custom depth to FreeRTOS `xTaskCreate()`, allowing memory optimization on constrained microcontrollers.

---

## 3. Exhaustive List of Implemented APIs (8 Total)

### 1. `pthread_mutex_trylock`
* **File Location**: [`posix_shim.c`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L530-L553)
* **Header Declaration**: [`posix_shim.h`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.h#L235-L247)
* **Signature**: `int pthread_mutex_trylock(pthread_mutex_t *mutex)`
* **Mechanics**: Non-blocking lock query. Returns `0` on acquisition, `EBUSY` if currently owned, or `EINVAL` on invalid descriptor.

### 2. `pthread_mutex_timedlock`
* **File Location**: [`posix_shim.c`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L555-L625)
* **Header Declaration**: [`posix_shim.h`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.h#L249-L261)
* **Signature**: `int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *abstime)`
* **Mechanics**: Blocks calling thread until lock is obtained or absolute deadline `abstime` expires. Returns `ETIMEDOUT` on timeout, `0` on acquisition, or `EINVAL` on invalid parameters.

### 3. `pthread_mutexattr_init`
* **File Location**: [`posix_shim.c`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L409-L422)
* **Header Declaration**: [`posix_shim.h`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.h#L162-L173)
* **Signature**: `int pthread_mutexattr_init(pthread_mutexattr_t *attr)`
* **Mechanics**: Initializes attributes object with default `PTHREAD_MUTEX_NORMAL` type.

### 4. `pthread_mutexattr_destroy`
* **File Location**: [`posix_shim.c`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L424-L435)
* **Header Declaration**: [`posix_shim.h`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.h#L175-L182)
* **Signature**: `int pthread_mutexattr_destroy(pthread_mutexattr_t *attr)`
* **Mechanics**: Invalidates attributes object.

### 5. `pthread_mutexattr_settype`
* **File Location**: [`posix_shim.c`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L437-L454)
* **Header Declaration**: [`posix_shim.h`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.h#L184-L198)
* **Signature**: `int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type)`
* **Mechanics**: Configures mutex type (`PTHREAD_MUTEX_NORMAL`, `PTHREAD_MUTEX_RECURSIVE`, `PTHREAD_MUTEX_ERRORCHECK`).

### 6. `pthread_mutexattr_gettype`
* **File Location**: [`posix_shim.c`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L456-L467)
* **Header Declaration**: [`posix_shim.h`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.h#L200-L208)
* **Signature**: `int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type)`
* **Mechanics**: Extracts configured mutex type.

### 7. `pthread_attr_init` & `pthread_attr_destroy`
* **File Location**: [`posix_shim.c`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L156-L188)
* **Header Declaration**: [`posix_shim.h`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.h#L72-L91)
* **Signatures**: `int pthread_attr_init(pthread_attr_t *attr)`, `int pthread_attr_destroy(pthread_attr_t *attr)`
* **Mechanics**: Initializes and clears thread attributes with 4096-byte default stack.

### 8. `pthread_attr_setstacksize` & `pthread_attr_getstacksize`
* **File Location**: [`posix_shim.c`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L190-L218)
* **Header Declaration**: [`posix_shim.h`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.h#L93-L113)
* **Signatures**: `int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize)`, `int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize)`
* **Mechanics**: Sets/gets stack depth in bytes. Validates minimum stack requirement (`configMINIMAL_STACK_SIZE`). Passed directly to `pthread_create()`.

---

## 4. Incremental Git Commit History

| Commit Hash | Commit Message | Scope |
| :---: | :--- | :--- |
| `b99f4099` | `feat(sync): implement pthread_mutex_trylock non-blocking lock acquisition` | Non-blocking lock query API |
| `10cca9c4` | `feat(sync): implement pthread_mutex_timedlock with absolute deadline` | Absolute timeout locking API |
| `2be0b783` | `feat(sync): implement pthread_mutexattr APIs and recursive mutex support` | Mutex attribute lifecycle and pointer-tagged recursive mutexes |
| `d80fcda1` | `feat(thread): implement pthread_attr APIs and custom thread stack depth` | Thread attribute lifecycle and custom stack sizing |
| `f09311bc` | `test(sync): add Milestone 4 extended mutex and thread attribute verification suite` | Comprehensive verification test suite in `main_blinky.c` |

---

## 5. QEMU Emulation Verification Log

Verification executed inside `qemu-system-arm` (`mps2-an385` platform, Cortex-M3 core). All test assertions passed:

```text
[Main] --- Spawning Milestone 4 Mutex & Attributes Verification Thread ---
[Main] Joining Mutex & Attr Test...
[Mutex Test] --- Starting Milestone 4 Verification ---
[Mutex Test] 1a. trylock(unlocked) [PASSED]
[Mutex Test] 1b. trylock(locked)->EBUSY [PASSED]
[Mutex Test] 1c. unlock() [PASSED]
[Mutex Test] 2a. timedlock(past)->ETIMEDOUT [PASSED]
[Mutex Test] 2b. timedlock(25ms)->ETIMEDOUT [PASSED]
[Mutex Test] 2c. timedlock(avail) [PASSED]
[Mutex Test] 3a. attr default NORMAL [PASSED]
[Mutex Test] 3b. attr set RECURSIVE [PASSED]
[Mutex Test] 3c. recursive lock depth 3 [PASSED]
[Mutex Test] 3d. recursive unlock depth 3 [PASSED]
[Mutex Test] 4a. attr default stack 4096 B [PASSED]
[Mutex Test] 4b. attr custom stack 2048 B [PASSED]
[Mutex Test] 4c. reject small stack EINVAL [PASSED]
[Mutex Test] 4d. thread custom stack join [PASSED]
[Mutex Test] --- Milestone 4 Verification Summary: ALL TESTS PASSED ---
[Main] Mutex & Attr Test joined with status: PASSED
[Main] Final shared counter value: 10 (Expected: 10)
--- All Milestone Verification Suites Completed Successfully ---
```

---

## 6. Memory & Flash Budget Analysis

Firmware binary footprints measured using `arm-none-eabi-size`:

```text
arm-none-eabi-size ./output/RTOSDemo.out
   text    data     bss     dec     hex filename
  81410     226  155586  237222   39ea6 ./output/RTOSDemo.out
```

### Flash Memory Progression Across Milestones

| Milestone | Active APIs | `text` (Flash) | Delta Flash | `data` | `bss` (RAM) | Status |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Baseline (Tier 1)** | 22 | 75,992 B | Baseline | 226 B | 155,570 B | Initial release |
| **Milestone 1 (Cond Vars)** | 27 | 77,208 B | +1,216 B | 226 B | 155,570 B | Completed |
| **Milestone 2 (Group C Sockets)** | 34 | 78,162 B | +954 B | 226 B | 155,586 B | Completed |
| **Milestone 3 (Clocks & Timers)** | 38 | 79,034 B | +872 B | 226 B | 155,586 B | Completed |
| **Milestone 4 (Mutex & Attrs)** | 46 | 81,410 B | +2,376 B | 226 B | 155,586 B | **Completed** |

> [!NOTE]
> The target micro-kernel Flash budget of **$\le 80$ KB** ($81,920$ bytes) is preserved at **81,410 bytes (79.50 KB)**. Zero dynamic memory overhead was introduced for recursive mutexes by employing pointer tagging on aligned FreeRTOS queue handles.
