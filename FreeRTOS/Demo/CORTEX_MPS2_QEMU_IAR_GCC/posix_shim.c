/*
 * ============================================================================
 * File: posix_shim.c
 * Description: Implementation of the POSIX Compatibility Layer on FreeRTOS
 * Target Platform: ARM Cortex-M (QEMU MPS2 AN385 / Cortex-M3 / STM32)
 *
 * Maps standard UNIX/POSIX APIs to native FreeRTOS kernel primitives:
 *   - Thread Lifecycle    : pthread_create, pthread_join, pthread_exit,
 *                           pthread_detach, pthread_self
 *   - Mutual Exclusion    : pthread_mutex_init, pthread_mutex_lock,
 *                           pthread_mutex_unlock, pthread_mutex_destroy
 *   - Condition Variables : pthread_cond_init, pthread_cond_wait,
 *                           pthread_cond_timedwait, pthread_cond_signal,
 *                           pthread_cond_broadcast, pthread_cond_destroy
 *   - Semaphores          : sem_init, sem_wait, sem_post, sem_destroy
 *   - Timing & Delays     : sleep, usleep
 *
 * Implementation Design:
 *   - All thread descriptors are managed via a singly-linked list (g_thread_list).
 *   - Mutexes leverage FreeRTOS Priority Inheritance to prevent Priority Inversion.
 *   - Condition variables use a per-waiter semaphore queue for precise wakeups.
 *   - Semaphores wrap FreeRTOS counting semaphores with 65535 max count.
 *   - Critical sections use taskENTER_CRITICAL() / taskEXIT_CRITICAL() (BASEPRI).
 * ============================================================================
 */

#include "posix_shim.h"
#include <stdarg.h>

/* ARM Cortex-M MPS2 Hardware & CMSIS Core Definitions */
#include "SMM_MPS2.h"

/* FreeRTOS Kernel Includes */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* ============================================================================
 * Internal Thread Registry Data Structures
 * ============================================================================
 *
 * Every simulated POSIX thread is represented in SRAM by a dynamic control
 * structure (posix_thread_t) allocated from the FreeRTOS heap (heap_4.c).
 * The global list g_thread_list tracks all live threads.
 */

typedef struct posix_thread {
    TaskHandle_t       xTask;          /* Native FreeRTOS task control block handle */
    void              *(*start_routine)(void *); /* Pointer to user entry function */
    void              *arg;            /* Parameter argument passed to start_routine */
    void              *retval;         /* Exit status value passed to pthread_exit() */
    SemaphoreHandle_t  join_sem;       /* Binary semaphore signaling thread completion */
    volatile int       detached;       /* 1 = Detached (auto-cleanup); 0 = Joinable */
    struct posix_thread *next;         /* Singly-linked list pointer to next thread node */
} posix_thread_t;

/*
 * Head pointer of the global active thread registry.
 * All mutations and reads must be guarded by Cortex-M priority-mask critical
 * sections (taskENTER_CRITICAL / taskEXIT_CRITICAL) to prevent race conditions.
 */
static posix_thread_t *g_thread_list = NULL;

/* ----------------------------------------------------------------------------
 * Function: add_thread
 * Objective: Prepends a newly allocated thread descriptor to the global list.
 * Complexity: O(1)
 * Critical Section: Enters critical section to safely update g_thread_list head.
 * ----------------------------------------------------------------------------
 */
static void add_thread(posix_thread_t *t)
{
    /* Disable interrupts up to configMAX_SYSCALL_INTERRUPT_PRIORITY */
    taskENTER_CRITICAL();

    /* Intrusive insertion at head of linked list */
    t->next       = g_thread_list;
    g_thread_list = t;

    /* Restore interrupt masking level */
    taskEXIT_CRITICAL();
}

/* ----------------------------------------------------------------------------
 * Function: find_thread
 * Objective: Locates the posix_thread_t node associated with a FreeRTOS TaskHandle.
 * Returns: Pointer to matched posix_thread_t node, or NULL if not found.
 * Note: Must be invoked or used within a protected context to ensure pointer validity.
 * ----------------------------------------------------------------------------
 */
static posix_thread_t *find_thread(TaskHandle_t xTask)
{
    taskENTER_CRITICAL();

    posix_thread_t *curr = g_thread_list;
    while (curr != NULL) {
        if (curr->xTask == xTask) {
            break; /* Found matching thread control block */
        }
        curr = curr->next;
    }

    taskEXIT_CRITICAL();
    return curr;
}

/* ----------------------------------------------------------------------------
 * Function: remove_thread
 * Objective: Unlinks a thread descriptor from the global registry list.
 * Complexity: O(N) where N is the number of active threads.
 * Note: Unlinks pointers only; does not free the memory block (caller handles free).
 * ----------------------------------------------------------------------------
 */
static void remove_thread(posix_thread_t *t)
{
    taskENTER_CRITICAL();

    posix_thread_t **curr = &g_thread_list;
    while (*curr != NULL) {
        if (*curr == t) {
            *curr = t->next; /* Unlink target node from list */
            break;
        }
        curr = &((*curr)->next);
    }

    taskEXIT_CRITICAL();
}

/* ----------------------------------------------------------------------------
 * Function: posix_thread_wrapper
 * Objective: Universal task entry bridge function for all POSIX threads.
 * Execution Context: Executes in task mode using Process Stack Pointer (PSP).
 * Mechanics:
 *   1. Extracts user start_routine and argument from posix_thread_t.
 *   2. Executes user routine: retval = start_routine(arg).
 *   3. Enforces clean exit by passing retval directly to pthread_exit(retval).
 * ----------------------------------------------------------------------------
 */
static void posix_thread_wrapper(void *pvParameters)
{
    posix_thread_t *t = (posix_thread_t *)pvParameters;

    /* Invoke the user's thread entry function */
    void *retval = t->start_routine(t->arg);

    /* Guarantee termination and join signaling via pthread_exit */
    pthread_exit(retval);
}

/* ============================================================================
 * POSIX Thread API Implementations
 * ============================================================================
 */

/*
 * pthread_create — Allocates thread descriptor, creates join semaphore,
 * registers the thread, and spawns a native FreeRTOS task.
 */
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg)
{
    (void)attr; /* Attributes (stack depth, priority) reserved for future expansion */

    if (thread == NULL || start_routine == NULL) {
        return -1;
    }

    /* Allocate thread control block from FreeRTOS heap */
    posix_thread_t *t = pvPortMalloc(sizeof(posix_thread_t));
    if (t == NULL) {
        return -1; /* Heap allocation failure */
    }

    /* Populate thread state fields */
    t->start_routine = start_routine;
    t->arg           = arg;
    t->retval        = NULL;
    t->detached      = 0; /* Default: joinable thread */

    /* Create binary semaphore for join synchronization (initially empty) */
    t->join_sem = xSemaphoreCreateBinary();
    if (t->join_sem == NULL) {
        vPortFree(t);
        return -1;
    }

    /*
     * Suspend the scheduler temporarily so the newly spawned task cannot
     * start executing and call find_thread() before add_thread() completes.
     */
    vTaskSuspendAll();

    /*
     * Spawn the FreeRTOS task:
     * - Entry Point: posix_thread_wrapper
     * - Stack Depth: 1024 words (4096 bytes) allocated from heap_4.c
     * - Priority: tskIDLE_PRIORITY + 1 (low-medium application priority)
     */
    BaseType_t result = xTaskCreate(posix_thread_wrapper,
                                    "POSIX_Thread",
                                    1024,
                                    t,
                                    tskIDLE_PRIORITY + 1,
                                    &(t->xTask));
    if (result != pdPASS) {
        xTaskResumeAll();
        vSemaphoreDelete(t->join_sem);
        vPortFree(t);
        return -1;
    }

    /* Register the thread node in the global active list */
    add_thread(t);

    /* Return the opaque pointer handle to the caller */
    *thread = (pthread_t)(uintptr_t)t;

    /* Resume the scheduler to allow the new task to execute when ready */
    xTaskResumeAll();
    return 0;
}

/*
 * pthread_join — Blocks calling thread on join_sem until target terminates,
 * retrieves exit status, unlinks from registry, and frees heap resources.
 */
int pthread_join(pthread_t thread, void **retval)
{
    posix_thread_t *t = (posix_thread_t *)(uintptr_t)thread;
    if (t == NULL) {
        return -1;
    }

    /*
     * Block indefinitely (portMAX_DELAY) until the target thread calls
     * pthread_exit() and gives the join_sem binary semaphore.
     */
    if (xSemaphoreTake(t->join_sem, portMAX_DELAY) != pdPASS) {
        return -1;
    }

    /* Retrieve exit status if requested */
    if (retval != NULL) {
        *retval = t->retval;
    }

    /* Unlink node from global registry */
    remove_thread(t);

    /* Delete completion synchronization semaphore */
    vSemaphoreDelete(t->join_sem);

    /* Free heap memory allocation */
    vPortFree(t);
    return 0;
}

/*
 * pthread_exit — Terminates the active thread, stores return status,
 * unblocks joiners, or immediately performs self-reclamation if detached.
 */
void pthread_exit(void *retval)
{
    /* Query the active task's thread node */
    posix_thread_t *t = find_thread(xTaskGetCurrentTaskHandle());
    if (t != NULL) {
        t->retval = retval;

        if (t->detached) {
            /* Detached threads self-clean: no joiner will free resources */
            remove_thread(t);
            vSemaphoreDelete(t->join_sem);
            vPortFree(t);
        } else {
            /* Signal any waiting pthread_join() caller */
            xSemaphoreGive(t->join_sem);
        }
    }

    /* Delete the underlying FreeRTOS task and transition to Dead state */
    vTaskDelete(NULL);

    /* Unreachable code to prevent compiler warnings */
    while (1);
}

/*
 * pthread_detach — Marks thread as detached so resources are auto-freed on exit.
 */
int pthread_detach(pthread_t thread)
{
    posix_thread_t *t = (posix_thread_t *)(uintptr_t)thread;
    if (t == NULL) {
        return -1;
    }
    t->detached = 1; /* Set detached state flag */
    return 0;
}

/*
 * pthread_self — Returns the opaque POSIX handle for the currently running thread.
 */
pthread_t pthread_self(void)
{
    posix_thread_t *t = find_thread(xTaskGetCurrentTaskHandle());
    return (pthread_t)(uintptr_t)t;
}

/* ============================================================================
 * POSIX Mutex API Implementations
 *
 * pthread_mutex_t is defined as a pointer-sized scalar holding a FreeRTOS
 * SemaphoreHandle_t created as a Priority-Inheritance Mutex.
 * ============================================================================
 */

/*
 * pthread_mutex_init — Creates a FreeRTOS priority-inheritance mutex.
 */
int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    (void)attr; /* Default standard mutex behavior */
    if (mutex == NULL) {
        return -1;
    }

    /* Create FreeRTOS mutex semaphore with priority inheritance */
    SemaphoreHandle_t xMutex = xSemaphoreCreateMutex();
    if (xMutex == NULL) {
        return -1; /* Heap exhausted */
    }

    *mutex = (pthread_mutex_t)(uintptr_t)xMutex;
    return 0;
}

/*
 * pthread_mutex_lock — Acquires ownership of mutex, blocking if locked.
 */
int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    if (mutex == NULL) {
        return -1;
    }

    SemaphoreHandle_t xMutex = (SemaphoreHandle_t)(uintptr_t)*mutex;
    if (xMutex == NULL) {
        return -1;
    }

    /* Block indefinitely until mutex lock is acquired */
    return (xSemaphoreTake(xMutex, portMAX_DELAY) == pdPASS) ? 0 : -1;
}

/*
 * pthread_mutex_unlock — Releases mutex ownership, waking any waiting thread.
 */
int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    if (mutex == NULL) {
        return -1;
    }

    SemaphoreHandle_t xMutex = (SemaphoreHandle_t)(uintptr_t)*mutex;
    if (xMutex == NULL) {
        return -1;
    }

    /* Release semaphore to next waiting task */
    return (xSemaphoreGive(xMutex) == pdPASS) ? 0 : -1;
}

/*
 * pthread_mutex_destroy — Deletes mutex semaphore and clears handle.
 */
int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    if (mutex == NULL) {
        return -1;
    }

    SemaphoreHandle_t xMutex = (SemaphoreHandle_t)(uintptr_t)*mutex;
    if (xMutex != NULL) {
        vSemaphoreDelete(xMutex);
        *mutex = 0; /* Clear handle to prevent use-after-free */
    }
    return 0;
}

/* ============================================================================
 * POSIX Condition Variable API Implementations
 *
 * Condition variables allow threads to atomically release a mutex and sleep
 * until signaled, avoiding CPU-wasting busy polling loops.
 *
 * Internal Design:
 *   - Each condition variable maintains a linked list of waiter nodes.
 *   - Each waiter carries its own binary semaphore for precise, targeted wakeup.
 *   - An internal FreeRTOS mutex (cond_lock) serializes access to the waiter queue.
 *   - pthread_cond_signal dequeues and wakes exactly one waiter (FIFO order).
 *   - pthread_cond_broadcast wakes all waiters by iterating the entire queue.
 *
 * Memory Layout (posix_cond_t allocated from heap_4.c):
 *   +------------------------------------------------------+
 *   | cond_lock (SemaphoreHandle_t) — guards waiter list    |
 *   | head -> [waiter_node] -> [waiter_node] -> NULL        |
 *   | tail -> [last waiter_node]                            |
 *   +------------------------------------------------------+
 * ============================================================================
 */

/*
 * Internal waiter node: one per thread blocked in pthread_cond_wait/timedwait.
 * Allocated on the waiting thread's stack (not heap) since the node's lifetime
 * is bounded by the wait call that creates it.
 */
typedef struct posix_cond_waiter {
    SemaphoreHandle_t           sem;    /* Binary semaphore for this waiter */
    struct posix_cond_waiter   *next;   /* Next waiter in FIFO queue */
} posix_cond_waiter_t;

/*
 * Internal condition variable control block, allocated from FreeRTOS heap.
 * Stored opaquely inside the pthread_cond_t scalar via pointer cast.
 */
typedef struct {
    SemaphoreHandle_t       cond_lock;      /* Mutex guarding the waiter queue */
    posix_cond_waiter_t    *head;           /* First waiter (dequeue end) */
    posix_cond_waiter_t    *tail;           /* Last waiter (enqueue end) */
} posix_cond_t;

/*
 * pthread_cond_init — Allocates condition variable control block with
 * an internal lock and empty waiter queue.
 */
int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    (void)attr; /* Attributes reserved for future expansion */
    if (cond == NULL) {
        return -1;
    }

    /* Allocate condition variable control block from FreeRTOS heap */
    posix_cond_t *cv = pvPortMalloc(sizeof(posix_cond_t));
    if (cv == NULL) {
        return -1; /* Heap exhausted */
    }

    /* Create internal mutex to serialize waiter queue operations */
    cv->cond_lock = xSemaphoreCreateMutex();
    if (cv->cond_lock == NULL) {
        vPortFree(cv);
        return -1;
    }

    /* Initialize empty waiter queue */
    cv->head = NULL;
    cv->tail = NULL;

    /* Store opaque pointer in pthread_cond_t */
    *cond = (pthread_cond_t)(uintptr_t)cv;
    return 0;
}

/*
 * pthread_cond_wait — Atomically releases mutex, blocks on condition variable,
 * then re-acquires mutex upon wakeup.
 *
 * Sequence:
 *   1. Create a stack-local binary semaphore as a personal wakeup handle.
 *   2. Lock the internal waiter queue and enqueue this waiter at the tail.
 *   3. Release the user mutex (atomic relative to signal — the cond_lock
 *      ensures no signal is lost between enqueue and mutex release).
 *   4. Unlock the internal waiter queue.
 *   5. Block on the personal semaphore until signaled.
 *   6. Re-acquire the user mutex before returning to the caller.
 *   7. Clean up the personal semaphore.
 */
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    if (cond == NULL || mutex == NULL) {
        return -1;
    }

    posix_cond_t *cv = (posix_cond_t *)(uintptr_t)*cond;
    if (cv == NULL) {
        return -1;
    }

    /* Step 1: Create thread-local binary semaphore (initially empty) */
    posix_cond_waiter_t waiter;
    waiter.sem  = xSemaphoreCreateBinary();
    waiter.next = NULL;
    if (waiter.sem == NULL) {
        return -1;
    }

    /* Step 2: Lock internal queue and enqueue this waiter */
    xSemaphoreTake(cv->cond_lock, portMAX_DELAY);

    if (cv->tail == NULL) {
        cv->head = &waiter;
        cv->tail = &waiter;
    } else {
        cv->tail->next = &waiter;
        cv->tail       = &waiter;
    }

    /* Step 3: Release user mutex while still holding cond_lock (no signal loss) */
    pthread_mutex_unlock(mutex);

    /* Step 4: Release internal lock — now signal/broadcast can proceed */
    xSemaphoreGive(cv->cond_lock);

    /* Step 5: Block until pthread_cond_signal/broadcast wakes us */
    xSemaphoreTake(waiter.sem, portMAX_DELAY);

    /* Step 6: Re-acquire user mutex before returning */
    pthread_mutex_lock(mutex);

    /* Step 7: Destroy personal semaphore */
    vSemaphoreDelete(waiter.sem);

    return 0;
}

/*
 * pthread_cond_timedwait — Same as pthread_cond_wait but with a timeout.
 *
 * Converts the absolute timespec into a relative FreeRTOS tick duration.
 * Returns ETIMEDOUT if the semaphore is not given within the timeout window.
 *
 * On timeout, the waiter must be manually dequeued from the condition
 * variable's waiter list to prevent dangling stack pointers.
 */
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime)
{
    if (cond == NULL || mutex == NULL || abstime == NULL) {
        return -1;
    }

    posix_cond_t *cv = (posix_cond_t *)(uintptr_t)*cond;
    if (cv == NULL) {
        return -1;
    }

    /*
     * Convert absolute timespec to relative FreeRTOS ticks.
     * Since we don't have a wall-clock epoch on bare-metal, we interpret
     * abstime relative to system boot (monotonic tick base).
     */
    TickType_t abs_ticks = pdMS_TO_TICKS(
        (TickType_t)abstime->tv_sec * 1000UL +
        (TickType_t)(abstime->tv_nsec / 1000000L)
    );
    TickType_t now_ticks = xTaskGetTickCount();
    TickType_t wait_ticks;

    if (abs_ticks > now_ticks) {
        wait_ticks = abs_ticks - now_ticks;
    } else {
        wait_ticks = 0; /* Deadline already passed */
    }

    /* Create thread-local binary semaphore */
    posix_cond_waiter_t waiter;
    waiter.sem  = xSemaphoreCreateBinary();
    waiter.next = NULL;
    if (waiter.sem == NULL) {
        return -1;
    }

    /* Enqueue waiter under internal lock */
    xSemaphoreTake(cv->cond_lock, portMAX_DELAY);

    if (cv->tail == NULL) {
        cv->head = &waiter;
        cv->tail = &waiter;
    } else {
        cv->tail->next = &waiter;
        cv->tail       = &waiter;
    }

    /* Release user mutex while holding cond_lock */
    pthread_mutex_unlock(mutex);
    xSemaphoreGive(cv->cond_lock);

    /* Block with timeout */
    BaseType_t result = xSemaphoreTake(waiter.sem, wait_ticks);

    int ret = 0;
    if (result != pdPASS) {
        /*
         * Timeout expired: remove our waiter node from the queue
         * to prevent the condition variable from signaling a dead
         * stack-local semaphore after we return.
         */
        xSemaphoreTake(cv->cond_lock, portMAX_DELAY);

        posix_cond_waiter_t **pp = &cv->head;
        while (*pp != NULL) {
            if (*pp == &waiter) {
                *pp = waiter.next;
                /* Fix tail if we were the last node */
                if (cv->tail == &waiter) {
                    cv->tail = (cv->head == NULL) ? NULL : cv->head;
                    /* Walk to actual tail if head is non-null */
                    if (cv->tail != NULL) {
                        posix_cond_waiter_t *t = cv->tail;
                        while (t->next != NULL) t = t->next;
                        cv->tail = t;
                    }
                }
                break;
            }
            pp = &((*pp)->next);
        }

        xSemaphoreGive(cv->cond_lock);
        ret = ETIMEDOUT;
    }

    /* Re-acquire user mutex */
    pthread_mutex_lock(mutex);

    /* Destroy personal semaphore */
    vSemaphoreDelete(waiter.sem);

    return ret;
}

/*
 * pthread_cond_signal — Wakes exactly one waiter (FIFO: the longest-waiting).
 *
 * Dequeues the head waiter node and gives its personal binary semaphore.
 * If no threads are waiting, this is a no-op (signals are not queued).
 */
int pthread_cond_signal(pthread_cond_t *cond)
{
    if (cond == NULL) {
        return -1;
    }

    posix_cond_t *cv = (posix_cond_t *)(uintptr_t)*cond;
    if (cv == NULL) {
        return -1;
    }

    /* Lock internal queue */
    xSemaphoreTake(cv->cond_lock, portMAX_DELAY);

    posix_cond_waiter_t *w = cv->head;
    if (w != NULL) {
        /* Dequeue head waiter */
        cv->head = w->next;
        if (cv->head == NULL) {
            cv->tail = NULL; /* Queue is now empty */
        }
        w->next = NULL;

        /* Wake the dequeued waiter */
        xSemaphoreGive(w->sem);
    }

    xSemaphoreGive(cv->cond_lock);
    return 0;
}

/*
 * pthread_cond_broadcast — Wakes all threads currently waiting on the
 * condition variable by iterating the entire waiter queue.
 */
int pthread_cond_broadcast(pthread_cond_t *cond)
{
    if (cond == NULL) {
        return -1;
    }

    posix_cond_t *cv = (posix_cond_t *)(uintptr_t)*cond;
    if (cv == NULL) {
        return -1;
    }

    /* Lock internal queue */
    xSemaphoreTake(cv->cond_lock, portMAX_DELAY);

    /* Wake every waiter in the queue */
    posix_cond_waiter_t *w = cv->head;
    while (w != NULL) {
        posix_cond_waiter_t *next = w->next;
        w->next = NULL;
        xSemaphoreGive(w->sem);
        w = next;
    }

    /* Reset queue to empty */
    cv->head = NULL;
    cv->tail = NULL;

    xSemaphoreGive(cv->cond_lock);
    return 0;
}

/*
 * pthread_cond_destroy — Frees the internal lock and control block.
 * Must not be called while any thread is waiting on the condition variable.
 */
int pthread_cond_destroy(pthread_cond_t *cond)
{
    if (cond == NULL) {
        return -1;
    }

    posix_cond_t *cv = (posix_cond_t *)(uintptr_t)*cond;
    if (cv != NULL) {
        /* Delete internal serialization mutex */
        if (cv->cond_lock != NULL) {
            vSemaphoreDelete(cv->cond_lock);
        }

        /* Free control block heap memory */
        vPortFree(cv);
        *cond = 0; /* Clear handle to prevent use-after-free */
    }
    return 0;
}

/*
 * clock_gettime — High-resolution hardware-assisted POSIX clock.
 *
 * Combines FreeRTOS scheduler tick count with ARM Cortex-M SysTick hardware
 * down-counter (SysTick->VAL) to yield nanosecond-level time resolution.
 */
int clock_gettime(clockid_t clk_id, struct timespec *tp)
{
    if (tp == NULL) {
        errno = EFAULT;
        return -1;
    }

    if (clk_id != CLOCK_REALTIME && clk_id != CLOCK_MONOTONIC) {
        errno = EINVAL;
        return -1;
    }

    /*
     * Lock-free atomic sampling of FreeRTOS tick count and SysTick counter:
     * Reads tick count before and after sampling SysTick->VAL. If a tick interrupt
     * fired in between, loops to resample, guaranteeing race-free consistency.
     */
    uint32_t ticks1, ticks2, val;
    uint32_t load = SysTick->LOAD;
    if (load == 0) {
        load = (configCPU_CLOCK_HZ / configTICK_RATE_HZ) - 1UL;
    }

    do {
        ticks1 = (uint32_t)xTaskGetTickCount();
        val = SysTick->VAL;
        ticks2 = (uint32_t)xTaskGetTickCount();
    } while (ticks1 != ticks2);

    /* SysTick counts downwards from LOAD to 0 */
    uint32_t elapsed_cycles = (val <= load) ? (load - val) : 0;
    uint64_t sub_tick_ns = ((uint64_t)elapsed_cycles * 1000000000ULL) / (uint64_t)configCPU_CLOCK_HZ;
    uint64_t total_ns = ((uint64_t)ticks1 * (1000000000ULL / (uint64_t)configTICK_RATE_HZ)) + sub_tick_ns;

    tp->tv_sec = (time_t)(total_ns / 1000000000ULL);
    tp->tv_nsec = (long)(total_ns % 1000000000ULL);

    return 0;
}

/*
 * gettimeofday — Obtains time in seconds and microseconds.
 */
int gettimeofday(struct timeval *tv, void *tz)
{
    if (tv == NULL) {
        errno = EFAULT;
        return -1;
    }

    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return -1;
    }

    tv->tv_sec  = ts.tv_sec;
    tv->tv_usec = (suseconds_t)(ts.tv_nsec / 1000L);

    if (tz != NULL) {
        struct timezone *tzp = (struct timezone *)tz;
        tzp->tz_minuteswest = 0;
        tzp->tz_dsttime     = 0;
    }

    return 0;
}

/*
 * _gettimeofday — Newlib C runtime syscall integration for gettimeofday.
 */
int _gettimeofday(struct timeval *tv, void *tz)
{
    return gettimeofday(tv, tz);
}

/*
 * nanosleep — High-resolution hybrid sleep with FreeRTOS task delay and SysTick spin.
 */
int nanosleep(const struct timespec *req, struct timespec *rem)
{
    if (req == NULL) {
        errno = EFAULT;
        return -1;
    }

    if (req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
    }

    /* Total whole milliseconds */
    uint64_t total_ms = (uint64_t)req->tv_sec * 1000ULL + (uint64_t)(req->tv_nsec / 1000000L);
    uint32_t residual_ns = (uint32_t)(req->tv_nsec % 1000000L);

    /* Coarse delay: yield CPU to other tasks via scheduler */
    if (total_ms > 0) {
        TickType_t ticks = pdMS_TO_TICKS(total_ms);
        if (ticks > 0) {
            vTaskDelay(ticks);
        }
    }

    /* Fine sub-millisecond delay: busy loop using SysTick counter */
    if (residual_ns > 0) {
        uint32_t cycles_to_wait = ((uint64_t)residual_ns * (uint64_t)configCPU_CLOCK_HZ) / 1000000000ULL;
        uint32_t load = SysTick->LOAD;
        if (load == 0) {
            load = (configCPU_CLOCK_HZ / configTICK_RATE_HZ) - 1UL;
        }

        uint32_t last_val = SysTick->VAL;
        uint32_t elapsed_cycles = 0;

        while (elapsed_cycles < cycles_to_wait) {
            uint32_t current_val = SysTick->VAL;
            if (current_val <= last_val) {
                elapsed_cycles += (last_val - current_val);
            } else {
                /* Counter reloaded / wrapped around */
                elapsed_cycles += (last_val + (load - current_val));
            }
            last_val = current_val;
        }
    }

    if (rem != NULL) {
        rem->tv_sec  = 0;
        rem->tv_nsec = 0;
    }

    return 0;
}

/*
 * time — Retrieves elapsed time in whole seconds.
 */
time_t time(time_t *tloc)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        ts.tv_sec = (time_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
    }

    if (tloc != NULL) {
        *tloc = ts.tv_sec;
    }

    return ts.tv_sec;
}

/*
 * sleep — Suspends thread execution for given seconds.
 */
unsigned int sleep(unsigned int seconds)
{
    /* Convert seconds to kernel scheduler ticks */
    vTaskDelay(pdMS_TO_TICKS(seconds * 1000UL));
    return 0;
}

/*
 * usleep — Suspends thread execution for given microseconds.
 */
int usleep(useconds_t useconds)
{
    TickType_t ticks = pdMS_TO_TICKS(useconds / 1000UL);

    /* Guarantee at least a 1-tick delay for non-zero duration to force yield */
    if (ticks == 0 && useconds > 0) {
        ticks = 1;
    }

    vTaskDelay(ticks);
    return 0;
}

/* ============================================================================
 * POSIX Counting Semaphore API Implementations
 *
 * sem_t wraps a FreeRTOS counting semaphore (xSemaphoreCreateCounting).
 * ============================================================================
 */

/*
 * sem_init — Initializes a counting semaphore with initial value.
 */
int sem_init(sem_t *sem, int pshared, unsigned int value)
{
    (void)pshared; /* Single unified address space on bare-metal MCU */
    if (sem == NULL) {
        return -1;
    }

    /* Max count = 65535, initial count = value */
    SemaphoreHandle_t xSem = xSemaphoreCreateCounting(65535, value);
    if (xSem == NULL) {
        return -1;
    }

    *sem = (sem_t)xSem;
    return 0;
}

/*
 * sem_wait — Decrements count or blocks until count > 0.
 */
int sem_wait(sem_t *sem)
{
    if (sem == NULL) {
        return -1;
    }

    SemaphoreHandle_t xSem = (SemaphoreHandle_t)*sem;
    if (xSem == NULL) {
        return -1;
    }

    /* Decrement resource count or block indefinitely */
    return (xSemaphoreTake(xSem, portMAX_DELAY) == pdPASS) ? 0 : -1;
}

/*
 * sem_post — Increments count and wakes a waiting task.
 */
int sem_post(sem_t *sem)
{
    if (sem == NULL) {
        return -1;
    }

    SemaphoreHandle_t xSem = (SemaphoreHandle_t)*sem;
    if (xSem == NULL) {
        return -1;
    }

    /* Increment count and unblock waiting thread */
    return (xSemaphoreGive(xSem) == pdPASS) ? 0 : -1;
}

/*
 * sem_destroy — Deletes counting semaphore resources.
 */
int sem_destroy(sem_t *sem)
{
    if (sem == NULL) {
        return -1;
    }

    SemaphoreHandle_t xSem = (SemaphoreHandle_t)*sem;
    if (xSem != NULL) {
        vSemaphoreDelete(xSem);
        *sem = NULL; /* Invalidate handle */
    }
    return 0;
}

/* ============================================================================
 * BSD Socket Networking & Non-Blocking I/O API Implementations (Group C)
 * ============================================================================
 */

/*
 * connect — Initiates an outbound TCP connection or sets UDP default peer.
 */
#undef connect
int connect(int s, const struct sockaddr *name, socklen_t namelen)
{
    return lwip_connect(s, name, namelen);
}

/*
 * send — Transmits data payload across a connected socket.
 */
#undef send
int send(int s, const void *dataptr, size_t size, int flags)
{
    return lwip_send(s, dataptr, size, flags);
}

/*
 * recv — Receives incoming data from a connected socket.
 */
#undef recv
int recv(int s, void *mem, size_t len, int flags)
{
    return lwip_recv(s, mem, len, flags);
}

/*
 * setsockopt — Configures socket options (SO_REUSEADDR, SO_RCVTIMEO, etc.).
 */
#undef setsockopt
int setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen)
{
    return lwip_setsockopt(s, level, optname, optval, optlen);
}

/*
 * fcntl — File descriptor control for non-blocking I/O flags.
 */
int fcntl(int fd, int cmd, ...)
{
    va_list va;
    int val = 0;

    va_start(va, cmd);
    if (cmd == F_SETFL) {
        val = va_arg(va, int);
    }
    va_end(va);

    return lwip_fcntl(fd, cmd, val);
}

/*
 * select — Synchronous I/O multiplexing across multiple socket descriptors.
 */
#undef select
int select(int maxfdp1, fd_set *readset, fd_set *writeset, fd_set *exceptset,
           struct timeval *timeout)
{
    return lwip_select(maxfdp1, readset, writeset, exceptset, timeout);
}

/*
 * poll — Waits for events on a set of file descriptors via select().
 */
int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    if (fds == NULL && nfds > 0) {
        return -1;
    }

    fd_set read_fds;
    fd_set write_fds;
    fd_set except_fds;

    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    FD_ZERO(&except_fds);

    int max_fd = -1;

    for (nfds_t i = 0; i < nfds; i++) {
        fds[i].revents = 0;
        int fd = fds[i].fd;

        if (fd < 0) {
            continue; /* Negative descriptor ignored per POSIX spec */
        }

        if (fd >= FD_SETSIZE) {
            fds[i].revents = POLLNVAL;
            continue;
        }

        if (fds[i].events & (POLLIN | POLLPRI)) {
            FD_SET(fd, &read_fds);
        }
        if (fds[i].events & POLLOUT) {
            FD_SET(fd, &write_fds);
        }
        FD_SET(fd, &except_fds);

        if (fd > max_fd) {
            max_fd = fd;
        }
    }

    struct timeval tv;
    struct timeval *ptv = NULL;

    if (timeout >= 0) {
        tv.tv_sec  = timeout / 1000;
        tv.tv_usec = (timeout % 1000) * 1000;
        ptv = &tv;
    }

    int result = lwip_select(max_fd + 1, &read_fds, &write_fds, &except_fds, ptv);
    if (result <= 0) {
        return result; /* 0 on timeout, -1 on error */
    }

    int ready_count = 0;

    for (nfds_t i = 0; i < nfds; i++) {
        int fd = fds[i].fd;
        if (fd < 0 || fd >= FD_SETSIZE) {
            continue;
        }

        if (FD_ISSET(fd, &read_fds)) {
            fds[i].revents |= POLLIN;
        }
        if (FD_ISSET(fd, &write_fds)) {
            fds[i].revents |= POLLOUT;
        }
        if (FD_ISSET(fd, &except_fds)) {
            fds[i].revents |= POLLERR;
        }

        if (fds[i].revents != 0) {
            ready_count++;
        }
    }

    return ready_count;
}
