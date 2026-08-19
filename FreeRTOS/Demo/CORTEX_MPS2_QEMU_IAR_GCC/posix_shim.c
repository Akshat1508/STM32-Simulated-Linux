/*
 * ============================================================================
 * File: posix_shim.c
 * Description: Implementation of the POSIX Compatibility Layer on FreeRTOS
 * Target Platform: ARM Cortex-M (QEMU MPS2 AN385 / Cortex-M3 / STM32)
 *
 * Maps standard UNIX/POSIX APIs to native FreeRTOS kernel primitives:
 *   - Thread Lifecycle : pthread_create, pthread_join, pthread_exit,
 *                        pthread_detach, pthread_self
 *   - Mutual Exclusion : pthread_mutex_init, pthread_mutex_lock,
 *                        pthread_mutex_unlock, pthread_mutex_destroy
 *   - Semaphores       : sem_init, sem_wait, sem_post, sem_destroy
 *   - Timing & Delays  : sleep, usleep
 *
 * Implementation Design:
 *   - All thread descriptors are managed via a singly-linked list (g_thread_list).
 *   - Mutexes leverage FreeRTOS Priority Inheritance to prevent Priority Inversion.
 *   - Semaphores wrap FreeRTOS counting semaphores with 65535 max count.
 *   - Critical sections use taskENTER_CRITICAL() / taskEXIT_CRITICAL() (BASEPRI).
 * ============================================================================
 */

#include "posix_shim.h"

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
 * POSIX Timing API Implementations
 * ============================================================================
 */

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
